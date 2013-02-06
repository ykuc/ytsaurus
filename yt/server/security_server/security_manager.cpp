#include "stdafx.h"
#include "security_manager.h"
#include "private.h"
#include "account.h"
#include "account_proxy.h"

#include <ytlib/meta_state/composite_meta_state.h>

#include <server/object_server/type_handler_detail.h>

#include <server/transaction_server/transaction.h>

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/meta_state_facade.h>
#include <server/cell_master/serialization_context.h>

#include <server/transaction_server/transaction.h>

#include <server/cypress_server/node.h>

namespace NYT {
namespace NSecurityServer {

using namespace NMetaState;
using namespace NCellMaster;
using namespace NObjectServer;
using namespace NTransactionServer;
using namespace NYTree;
using namespace NCypressServer;
using namespace NSecurityClient;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = SecurityServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TSecurityManager::TAccountTypeHandler
    : public TObjectTypeHandlerBase<TAccount>
{
public:
    explicit TAccountTypeHandler(TImpl* owner);

    virtual EObjectType GetType() const override
    {
        return EObjectType::Account;
    }

    virtual EObjectTransactionMode GetTransactionMode() const override
    {
        return EObjectTransactionMode::Forbidden;
    }

    virtual EObjectAccountMode GetAccountMode() const override
    {
        return EObjectAccountMode::Forbidden;
    }

    virtual TObjectBase* Create(
        TTransaction* transaction,
        TAccount* account,
        IAttributeDictionary* attributes,
        TReqCreateObject* request,
        TRspCreateObject* response) override;

private:
    TImpl* Owner;

    virtual IObjectProxyPtr DoGetProxy(TAccount* account, TTransaction* transaction) override;
    virtual void DoDestroy(TAccount* account) override;

};

////////////////////////////////////////////////////////////////////////////////

class TSecurityManager::TImpl
    : public TMetaStatePart
{
public:
    explicit TImpl(NCellMaster::TBootstrap* bootstrap)
        : TMetaStatePart(
            bootstrap->GetMetaStateFacade()->GetManager(),
            bootstrap->GetMetaStateFacade()->GetState())
        , Bootstrap(bootstrap)
        , SysAccount(nullptr)
        , TmpAccount(nullptr)
    {
        YCHECK(bootstrap);

        {
            NCellMaster::TLoadContext context;
            context.SetBootstrap(bootstrap);

            RegisterLoader(
                "SecurityManager.Keys",
                SnapshotVersionValidator(),
                BIND(&TImpl::LoadKeys, MakeStrong(this)),
                context);
            RegisterLoader(
                "SecurityManager.Values",
                SnapshotVersionValidator(),
                BIND(&TImpl::LoadValues, MakeStrong(this)),
                context);
        }

        {
            NCellMaster::TSaveContext context;

            RegisterSaver(
                ESavePriority::Keys,
                "SecurityManager.Keys",
                CurrentSnapshotVersion,
                BIND(&TImpl::SaveKeys, MakeStrong(this)),
                context);
            RegisterSaver(
                ESavePriority::Values,
                "SecurityManager.Values",
                CurrentSnapshotVersion,
                BIND(&TImpl::SaveValues, MakeStrong(this)),
                context);
        }

        {
            auto cellId = Bootstrap->GetObjectManager()->GetCellId();
            SysAccountId = MakeWellKnownId(EObjectType::Account, cellId, 0xffffffffffffffff);
            TmpAccountId = MakeWellKnownId(EObjectType::Account, cellId, 0xfffffffffffffffe);
        }
    }

    void Initialize()
    {
        auto objectManager = Bootstrap->GetObjectManager();
        objectManager->RegisterHandler(New<TAccountTypeHandler>(this));
    }


    DECLARE_METAMAP_ACCESSORS(Account, TAccount, TAccountId);

    TAccount* CreateAccount(const Stroka& name)
    {
        if (FindAccountByName(name)) {
            THROW_ERROR_EXCEPTION("Account already exists: %s", ~name);
        }

        auto objectManager = Bootstrap->GetObjectManager();
        auto id = objectManager->GenerateId(EObjectType::Account);
        return DoCreateAccount(id, name);
    }

    void DestroyAccount(TAccount* account)
    {
        YCHECK(AccountNameMap.erase(account->GetName()) == 1);
    }

    TAccount* FindAccountByName(const Stroka& name)
    {
        auto it = AccountNameMap.find(name);
        return it == AccountNameMap.end() ? nullptr : it->second;
    }


    TAccount* GetSysAccount()
    {
        YCHECK(SysAccount);
        return SysAccount;
    }

    TAccount* GetTmpAccount()
    {
        YCHECK(TmpAccount);
        return TmpAccount;
    }


    void SetAccount(TCypressNodeBase* node, TAccount* account)
    {
        YCHECK(node);
        YCHECK(account);

        auto* oldAccount = node->GetAccount();
        if (oldAccount == account)
            return;

        auto objectManager = Bootstrap->GetObjectManager();

        bool isAccountingEnabled = IsUncommittedAccountingEnabled(node);

        if (oldAccount) {
            if (isAccountingEnabled) {
                auto* oldTransactionUsage = FindTransactionAccountUsage(node);
                if (oldTransactionUsage) {
                    *oldTransactionUsage -= node->CachedResourceUsage();
                }
            }

            objectManager->UnrefObject(oldAccount);

            if (isAccountingEnabled) {
                oldAccount->ResourceUsage() -= node->CachedResourceUsage();
            }

            --oldAccount->NodeCount();
        }

        node->SetAccount(account);
        objectManager->RefObject(account);

        if (isAccountingEnabled) {
            node->CachedResourceUsage() = node->GetResourceUsage();
            account->ResourceUsage() += node->CachedResourceUsage();

            auto* newTransactionUsage = FindTransactionAccountUsage(node);
            if (newTransactionUsage) {
                *newTransactionUsage += node->CachedResourceUsage();
            }
        }

        ++account->NodeCount();
    }

    void ResetAccount(TCypressNodeBase* node)
    {
        auto* account = node->GetAccount();
        if (!account)
            return;

        auto objectManager = Bootstrap->GetObjectManager();

        bool isAccountingEnabled = IsUncommittedAccountingEnabled(node);

        if (isAccountingEnabled) {
            auto* transactionUsage = FindTransactionAccountUsage(node);
            if (transactionUsage) {
                *transactionUsage -= node->CachedResourceUsage();
            }
        }

        node->SetAccount(nullptr);
        objectManager->UnrefObject(account);

        if (isAccountingEnabled) {
            account->ResourceUsage() -= node->CachedResourceUsage();
            node->CachedResourceUsage() = ZeroClusterResources();
        }

        --account->NodeCount();
    }


    void UpdateAccountNodeUsage(TCypressNodeBase* node)
    {
        auto* account = node->GetAccount();
        if (!account)
            return;

        if (!IsUncommittedAccountingEnabled(node))
            return;

        auto* transactionUsage = FindTransactionAccountUsage(node);

        account->ResourceUsage() -= node->CachedResourceUsage();
        if (transactionUsage) {
            *transactionUsage -= node->CachedResourceUsage();
        }

        node->CachedResourceUsage() = node->GetResourceUsage();

        account->ResourceUsage() += node->CachedResourceUsage();
        if (transactionUsage) {
            *transactionUsage += node->CachedResourceUsage();
        }
    }

    void UpdateAccountStagingUsage(
        TTransaction* transaction,
        TAccount* account,
        const TClusterResources& delta)
    {
        if (!IsStagedAccountingEnabled(transaction))
            return;
       
        account->ResourceUsage() += delta;

        auto* transactionUsage = GetTransactionAccountUsage(transaction, account);
        *transactionUsage += delta;
    }

private:
    friend class TAccountTypeHandler;

    NCellMaster::TBootstrap* Bootstrap;

    TAccountMetaMap AccountMap;
    yhash_map<Stroka, TAccount*> AccountNameMap;

    TAccountId SysAccountId;
    TAccount* SysAccount;
    
    TAccountId TmpAccountId;
    TAccount* TmpAccount;


    static bool IsUncommittedAccountingEnabled(TCypressNodeBase* node)
    {
        auto* transaction = node->GetTransaction();
        return !transaction || transaction->GetUncommittedAccountingEnabled();
    }

    static bool IsStagedAccountingEnabled(TTransaction* transaction)
    {
        return transaction->GetStagedAccountingEnabled();
    }


    TAccount* DoCreateAccount(const TAccountId& id, const Stroka& name)
    {
        auto* account = new TAccount(id);
        account->SetName(name);

        AccountMap.Insert(id, account);
        YCHECK(AccountNameMap.insert(std::make_pair(account->GetName(), account)).second);

        // Make the fake reference.
        YCHECK(account->RefObject() == 1);

        return account;
    }

    TClusterResources* FindTransactionAccountUsage(TCypressNodeBase* node)
    {
        auto* account = node->GetAccount();
        // COMPAT(babenko)
        if (!account) {
            return nullptr;
        }

        auto* transaction = node->GetTransaction();
        if (!transaction) {
            return nullptr;
        }

        return GetTransactionAccountUsage(transaction, account);
    }

    TClusterResources* GetTransactionAccountUsage(TTransaction* transaction, TAccount* account)
    {
        auto it = transaction->AccountResourceUsage().find(account);
        if (it == transaction->AccountResourceUsage().end()) {
            auto pair = transaction->AccountResourceUsage().insert(std::make_pair(account, ZeroClusterResources()));
            YCHECK(pair.second);
            return &pair.first->second;
        } else {
            return &it->second;
        }
    }


    void SaveKeys(const NCellMaster::TSaveContext& context) const
    {
        AccountMap.SaveKeys(context);
    }

    void SaveValues(const NCellMaster::TSaveContext& context) const
    {
        AccountMap.SaveValues(context);
    }

    void LoadKeys(const NCellMaster::TLoadContext& context)
    {
        AccountMap.LoadKeys(context);

        SysAccount = GetAccount(SysAccountId);
        TmpAccount = GetAccount(TmpAccountId);
    }

    void LoadValues(const NCellMaster::TLoadContext& context)
    {
        AccountMap.LoadValues(context);

        // Reconstruct account name map.
        AccountNameMap.clear();
        FOREACH (const auto& pair, AccountMap) {
            auto* account = pair.second;
            YCHECK(AccountNameMap.insert(std::make_pair(account->GetName(), account)).second);
        }
    }

    virtual void Clear() override
    {
        AccountMap.Clear();
        AccountNameMap.clear();

        // sys, 1 TB disk space
        SysAccount = DoCreateAccount(SysAccountId, SysAccountName);
        SysAccount->ResourceLimits() = TClusterResources::FromDiskSpace((i64) 1024 * 1024 * 1024 * 1024);

        // tmp, 1 TB disk space
        TmpAccount = DoCreateAccount(TmpAccountId, TmpAccountName);
        TmpAccount->ResourceLimits() = TClusterResources::FromDiskSpace((i64) 1024 * 1024 * 1024 * 1024);
    }

};

DEFINE_METAMAP_ACCESSORS(TSecurityManager::TImpl, Account, TAccount, TAccountId, AccountMap)

///////////////////////////////////////////////////////////////////////////////

TSecurityManager::TAccountTypeHandler::TAccountTypeHandler(TImpl* owner)
    : TObjectTypeHandlerBase(owner->Bootstrap, &owner->AccountMap)
    , Owner(owner)
{ }

TObjectBase* TSecurityManager::TAccountTypeHandler::Create(
    TTransaction* transaction,
    TAccount* account,
    IAttributeDictionary* attributes,
    TReqCreateObject* request,
    TRspCreateObject* response)
{
    UNUSED(transaction);
    UNUSED(account);
    UNUSED(request);
    UNUSED(response);

    auto name = attributes->Get<Stroka>("name");
    auto* newAccount = Owner->CreateAccount(name);
    return newAccount;
}

IObjectProxyPtr TSecurityManager::TAccountTypeHandler::DoGetProxy(
    TAccount* account,
    TTransaction* transaction)
{
    UNUSED(transaction);
    return CreateAccountProxy(Owner->Bootstrap, account, &Owner->AccountMap);
}

void TSecurityManager::TAccountTypeHandler::DoDestroy(TAccount* account)
{
    Owner->DestroyAccount(account);
}

///////////////////////////////////////////////////////////////////////////////

TSecurityManager::TSecurityManager(NCellMaster::TBootstrap* bootstrap)
    : Impl(New<TImpl>(bootstrap))
{ }

TSecurityManager::~TSecurityManager()
{ }

void TSecurityManager::Initialize()
{
    return Impl->Initialize();
}

TAccount* TSecurityManager::FindAccountByName(const Stroka& name)
{
    return Impl->FindAccountByName(name);
}

TAccount* TSecurityManager::GetSysAccount()
{
    return Impl->GetSysAccount();
}

TAccount* TSecurityManager::GetTmpAccount()
{
    return Impl->GetTmpAccount();
}

void TSecurityManager::SetAccount(TCypressNodeBase* node, TAccount* account)
{
    Impl->SetAccount(node, account);
}

void TSecurityManager::ResetAccount(TCypressNodeBase* node)
{
    Impl->ResetAccount(node);    
}

void TSecurityManager::UpdateAccountNodeUsage(TCypressNodeBase* node)
{
    Impl->UpdateAccountNodeUsage(node);
}

void TSecurityManager::UpdateAccountStagingUsage(
    TTransaction* transaction,
    TAccount* account,
    const TClusterResources& delta)
{
    Impl->UpdateAccountStagingUsage(transaction, account, delta);
}

DELEGATE_METAMAP_ACCESSORS(TSecurityManager, Account, TAccount, TAccountId, *Impl)

///////////////////////////////////////////////////////////////////////////////

} // namespace NSecurityServer
} // namespace NYT
