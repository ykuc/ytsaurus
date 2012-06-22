#include "stdafx.h"
#include "transaction_manager.h"
#include "transaction.h"
#include "config.h"
#include "private.h"

#include <ytlib/misc/assert.h>
#include <ytlib/misc/property.h>
#include <ytlib/misc/thread_affinity.h>
#include <ytlib/misc/delayed_invoker.h>
#include <ytlib/actions/signal.h>
#include <ytlib/cypress/cypress_ypath_proxy.h>

namespace NYT {
namespace NTransactionClient {

using namespace NCypress;
using namespace NObjectServer;
using namespace NTransactionServer;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = TransactionClientLogger;

////////////////////////////////////////////////////////////////////////////////

class TTransactionManager::TTransaction
    : public ITransaction
{
public:
    TTransaction(
        NRpc::IChannelPtr cellChannel,
        const TTransactionId& parentId,
        TTransactionManagerPtr owner)
        : Owner(owner)
        , Proxy(cellChannel)
        , State(EState::Active)
        , ParentId(parentId)
        , Aborted(NewPromise<void>())
    {
        YASSERT(cellChannel);
        YASSERT(owner);
    }

    TTransaction(
        NRpc::IChannelPtr cellChannel,
        TTransactionManagerPtr owner,
        const TTransactionId& id)
        : Owner(owner)
        , Proxy(cellChannel)
        , State(EState::Active)
        , ParentId(NullTransactionId)
        , Id(id)
        , Aborted(NewPromise<void>())
    {
        YASSERT(cellChannel);
        YASSERT(owner);
    }

    ~TTransaction()
    {
        if (State == EState::Active) {
            InvokeAbort(false);
        }
    }

    void Start(IAttributeDictionary* attributes)
    {
        LOG_INFO("Starting transaction");

        auto transactionPath =
            ParentId == NullTransactionId
            ? RootTransactionPath
            : FromObjectId(ParentId);
        auto req = TTransactionYPathProxy::CreateObject(transactionPath);
        req->set_type(EObjectType::Transaction);
        if (attributes) {
            req->Attributes().MergeFrom(*attributes);
        }
        auto rsp = Proxy.Execute(req).Get();
        if (!rsp->IsOK()) {
            // No ping tasks are running, so no need to lock here.
            State = EState::Aborted;
            LOG_ERROR_AND_THROW(yexception(), "Error starting transaction\n%s",  ~rsp->GetError().ToString());
        }
        Id = TTransactionId::FromProto(rsp->object_id());

        State = EState::Active;
        LOG_INFO("Started transaction %s", ~Id.ToString());
    }

    void Attach()
    {
        LOG_INFO("Attached transaction %s", ~Id.ToString());
        State = EState::Active;
    }

    virtual TTransactionId GetId() const
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Id;
    }

    virtual void Commit() 
    {
        VERIFY_THREAD_AFFINITY(ClientThread);

        {
            TGuard<TSpinLock> guard(SpinLock);
            switch (State) {
                case EState::Committed:
                    ythrow yexception() << "Transaction is already committed";
                    break;

                case EState::Aborted:
                    ythrow yexception() << "Transaction is already aborted";
                    break;

                case EState::Active:
                    State = EState::Committed;
                    break;

                default:
                    YUNREACHABLE();
            }
        }

        LOG_INFO("Committing transaction %s", ~Id.ToString());

        auto req = TTransactionYPathProxy::Commit(FromObjectId(Id));
        auto rsp = Proxy.Execute(req).Get();
        if (!rsp->IsOK()) {
            // Let's pretend the transaction was aborted.
            // No sync here, should be safe.
            State = EState::Aborted;
            
            LOG_ERROR_AND_THROW(yexception(), "Error committing transaction %s\n%s",
                ~Id.ToString(),
                ~rsp->GetError().ToString());

            FireAbort();
            return;
        }

        LOG_INFO("Committed transaction %s", ~Id.ToString());
    }

    virtual void Abort(bool wait)
    {
        VERIFY_THREAD_AFFINITY_ANY();

        LOG_INFO("Transaction %s aborted by client",  ~Id.ToString());

        InvokeAbort(wait);
        HandleAbort();
    }

    virtual void Detach()
    {
        VERIFY_THREAD_AFFINITY(ClientThread);

        {
            TGuard<TSpinLock> guard(SpinLock);
            switch (State) {
                case EState::Committed:
                    ythrow yexception() << "Transaction is already committed";
                    break;

                case EState::Aborted:
                    ythrow yexception() << "Transaction is already aborted";
                    break;

                case EState::Active:
                    State = EState::Detached;
                    break;

                case EState::Detached:
                    return;

                default:
                    YUNREACHABLE();
            }
        }

        Owner->UnregisterTransaction(Id);
        Owner.Reset();

        LOG_INFO("Detached transaction %s", ~Id.ToString());
    }

    virtual void SubscribeAborted(const TCallback<void()>& handler)
    {
        VERIFY_THREAD_AFFINITY_ANY();
        Aborted.Subscribe(handler);
    }

    virtual void UnsubscribeAborted(const TCallback<void()>& handler)
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YUNREACHABLE();
    }

    void HandleAbort()
    {
        VERIFY_THREAD_AFFINITY_ANY();

        {
            TGuard<TSpinLock> guard(SpinLock);
            if (State != EState::Active) {
                return;
            }
            State = EState::Aborted;
        }

        FireAbort();
    }

    TTransactionId GetParentId() const
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return ParentId;
    }

private:
    DECLARE_ENUM(EState,
        (Active)
        (Aborted)
        (Committing)
        (Committed)
        (Detached)
    );

    TTransactionManagerPtr Owner;
    TObjectServiceProxy Proxy;

    //! Protects state transitions.
    TSpinLock SpinLock;
    EState State;
    TTransactionId ParentId;

    TTransactionId Id;
    TPromise<void> Aborted;

    DECLARE_THREAD_AFFINITY_SLOT(ClientThread);


    void InvokeAbort(bool wait)
    {
        // Fire and forget in case of no wait.
        auto req = TTransactionYPathProxy::Abort(FromObjectId(Id));
        auto result = Proxy.Execute(req);
        if (wait) {
            auto rsp = result.Get();
            if (!rsp->IsOK()) {
                throw yexception() <<
                    Sprintf("Error aborting transaction\n%s",
                        ~rsp->GetError().ToString());
            }
        }
    }

    void FireAbort()
    {
        Aborted.Set();
    }

};

////////////////////////////////////////////////////////////////////////////////

TTransactionManager::TTransactionManager(
    TTransactionManagerConfigPtr config,
    NRpc::IChannelPtr channel)
    : Config(config)
    , Channel(channel)
    , ObjectProxy(channel)
{
    YASSERT(channel);
}

ITransactionPtr TTransactionManager::Start(
    IAttributeDictionary* attributes,
    const TTransactionId& parentId)
{
    VERIFY_THREAD_AFFINITY_ANY();

    auto transaction = New<TTransaction>(
        Channel,
        parentId,
        this);
    transaction->Start(attributes);

    RegisterTransaction(transaction);
    SchedulePing(transaction);

    return transaction;
}

ITransactionPtr TTransactionManager::Attach(const TTransactionId& id)
{
    // Try to find it among existing
    auto transaction = FindTransaction(id);
    if (transaction) {
        return transaction;
    }

    // Not found, create a new one.
    transaction = New<TTransaction>(~Channel, this, id);
    transaction->Attach();

    RegisterTransaction(transaction);
    SendPing(transaction->GetId());

    return transaction;
}

void TTransactionManager::RegisterTransaction(TTransactionPtr transaction)
{
    TGuard<TSpinLock> guard(SpinLock);
    YCHECK(TransactionMap.insert(MakePair(transaction->GetId(), ~transaction)).second);
    LOG_DEBUG("Registered transaction %s", ~transaction->GetId().ToString());
}

void TTransactionManager::UnregisterTransaction(const TTransactionId& id)
{
    TGuard<TSpinLock> guard(SpinLock);
    TransactionMap.erase(id);
}

TTransactionManager::TTransactionPtr TTransactionManager::FindTransaction(const TTransactionId& id)
{
    TGuard<TSpinLock> guard(SpinLock);
    auto it = TransactionMap.find(id);
    if (it == TransactionMap.end()) {
        return NULL;
    }
    auto transaction = it->second.Lock();
    if (!transaction) {
        TransactionMap.erase(it);
    }
    return transaction;
}

void TTransactionManager::SchedulePing(TTransactionPtr transaction)
{
    TDelayedInvoker::Submit(
        BIND(&TThis::SendPing, MakeStrong(this), transaction->GetId()),
        Config->PingPeriod);
}

void TTransactionManager::SendPing(const TTransactionId& id)
{
    auto transaction = FindTransaction(id);
    if (!transaction) {
        return;
    }

    LOG_DEBUG("Renewing lease for transaction %s", ~id.ToString());

    auto req = TTransactionYPathProxy::RenewLease(FromObjectId(id));
    ObjectProxy.Execute(req).Subscribe(BIND(
        &TThis::OnPingResponse,
        MakeStrong(this),
        id));
}

void TTransactionManager::OnPingResponse(
    const TTransactionId& id,
    TTransactionYPathProxy::TRspRenewLeasePtr rsp)
{
    auto transaction = FindTransaction(id);
    if (!transaction) {
        return;
    }

    if (!rsp->IsOK()) {
        UnregisterTransaction(id);
        if (rsp->GetErrorCode() == EYPathErrorCode::ResolveError) {
            LOG_WARNING("Transaction %s has expired or was aborted",
                ~id.ToString());
            transaction->HandleAbort();
        } else {
            LOG_WARNING("Error renewing lease for transaction %s\n%s",
                ~id.ToString(),
                ~rsp->GetError().ToString());
        }
        return;
    }

    LOG_DEBUG("Renewed lease for transaction %s", ~id.ToString());

    SchedulePing(transaction);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionClient
} // namespace NYT
