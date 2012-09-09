#include "stdafx.h"
#include "meta_state_facade.h"
#include "config.h"

#include <ytlib/misc/periodic_invoker.h>

#include <ytlib/actions/action_queue.h>

#include <ytlib/ytree/ypath_proxy.h>
#include <ytlib/ytree/ypath_client.h>

#include <ytlib/rpc/bus_channel.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>

#include <ytlib/object_client/object_service_proxy.h>

#include <ytlib/transaction_client/transaction_ypath_proxy.h>

#include <ytlib/meta_state/composite_meta_state.h>
#include <ytlib/meta_state/persistent_state_manager.h>

#include <ytlib/logging/log.h>

#include <server/cell_master/bootstrap.h>

#include <server/cypress_server/cypress_manager.h>
#include <server/cypress_server/node_detail.h>

namespace NYT {
namespace NCellMaster {

using namespace NRpc;
using namespace NMetaState;
using namespace NYTree;
using namespace NCypressServer;
using namespace NCypressClient;
using namespace NTransactionClient;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger Logger("Bootstrap");

////////////////////////////////////////////////////////////////////////////////

class TMetaStateFacade::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TCellMasterConfigPtr config,
        TBootstrap* bootstrap)
        : Config(config)
        , Bootstrap(bootstrap)
        , Root(NULL)
    {
        YCHECK(config);
        YCHECK(bootstrap);

        StateQueue = New<TFairShareActionQueue>(EStateThreadQueue::GetDomainSize(), "MetaState");

        MetaState = New<TCompositeMetaState>();

        MetaStateManager = CreatePersistentStateManager(
            Config->MetaState,
            Bootstrap->GetControlInvoker(),
            StateQueue->GetInvoker(EStateThreadQueue::Default),
            MetaState,
            Bootstrap->GetRpcServer());

        MetaStateManager->SubscribeStartLeading(BIND(&TImpl::OnStartEpoch, MakeWeak(this)));
        MetaStateManager->SubscribeStartFollowing(BIND(&TImpl::OnStartEpoch, MakeWeak(this)));

        MetaStateManager->SubscribeStopLeading(BIND(&TImpl::OnStopEpoch, MakeWeak(this)));
        MetaStateManager->SubscribeStopFollowing(BIND(&TImpl::OnStopEpoch, MakeWeak(this)));

        MetaStateManager->SubscribeActiveQuorumEstablished(BIND(&TImpl::OnActiveQuorumEstablished, MakeWeak(this)));

        for (int queueIndex = 0; queueIndex < EStateThreadQueue::GetDomainSize(); ++queueIndex) {
            GuardedInvokers.push_back(MetaStateManager->CreateGuardedStateInvoker(StateQueue->GetInvoker(queueIndex)));
        }
    }

    TCompositeMetaStatePtr GetState() const
    {
        return MetaState;
    }

    IMetaStateManagerPtr GetManager() const
    {
        return MetaStateManager;
    }

    bool IsInitialized() const
    {
        if (!Root) {
            auto cypressManager = Bootstrap->GetCypressManager();
            Root = dynamic_cast<TMapNode*>(cypressManager->GetNode(cypressManager->GetRootNodeId()));
            YCHECK(Root);
        }
        return !Root->KeyToChild().empty();
    }

    IInvokerPtr GetUnguardedInvoker(EStateThreadQueue queue = EStateThreadQueue::Default) const
    {
        return StateQueue->GetInvoker(queue);
    }

    IInvokerPtr GetUnguardedEpochInvoker(EStateThreadQueue queue = EStateThreadQueue::Default) const
    {
        return UnguardedEpochInvokers[queue];
    }

    IInvokerPtr GetGuardedInvoker(EStateThreadQueue queue = EStateThreadQueue::Default) const
    {
        return GuardedInvokers[queue];
    }

    IInvokerPtr GetGuardedEpochInvoker(EStateThreadQueue queue = EStateThreadQueue::Default) const
    {
        return GuardedEpochInvokers[queue];
    }

    void Start()
    {
        MetaStateManager->Start();
    }

private:
    TCellMasterConfigPtr Config;
    TBootstrap* Bootstrap;

    TFairShareActionQueuePtr StateQueue;
    TCompositeMetaStatePtr MetaState;
    IMetaStateManagerPtr MetaStateManager;
    std::vector<IInvokerPtr> GuardedInvokers;
    std::vector<IInvokerPtr> GuardedEpochInvokers;
    std::vector<IInvokerPtr> UnguardedEpochInvokers;

    mutable TMapNode* Root;


    void OnStartEpoch()
    {
        YCHECK(GuardedEpochInvokers.empty());
        YCHECK(UnguardedEpochInvokers.empty());

        auto cancelableContext = MetaStateManager->GetEpochContext()->CancelableContext;
        for (int queueIndex = 0; queueIndex < EStateThreadQueue::GetDomainSize(); ++queueIndex) {
            GuardedEpochInvokers.push_back(cancelableContext->CreateInvoker(GuardedInvokers[queueIndex]));
            UnguardedEpochInvokers.push_back(cancelableContext->CreateInvoker(StateQueue->GetInvoker(queueIndex)));
        }
    }

    void OnStopEpoch()
    {
        GuardedEpochInvokers.clear();
        UnguardedEpochInvokers.clear();
    }


    void OnActiveQuorumEstablished()
    {
        // NB: Initialization cannot be carried out here since not all subsystems
        // are fully initialized yet.
        // We'll post an initialization callback to the state invoker instead.
        GetUnguardedEpochInvoker()->Invoke(BIND(&TImpl::InitializeIfNeeded, MakeStrong(this)));
    }

    void InitializeIfNeeded()
    {
        if (!IsInitialized()) {
            Initialize();
        }
    }

    // TODO(babenko): move initializer to a separate class
    void Initialize()
    {
        LOG_INFO("World initialization started");

        try {
            auto objectManager = Bootstrap->GetObjectManager();
            auto cypressManager = Bootstrap->GetCypressManager();
            auto rootService = objectManager->GetRootService();

            auto cellId = objectManager->GetCellId();
            auto cellGuid = TGuid::Create();

            // Abort all existing transactions to avoid collisions with previous (failed)
            // initialization attempts.
            AbortTransactions();

            // All initialization will be happening within this transaction.
            auto transactionId = StartTransaction();

            TYsonString emptyMap("{}");
            TYsonString emptyOpaqueMap("<opaque = true>{}");

            SyncYPathSet(
                rootService,
                WithTransaction("//sys", transactionId),
                emptyMap);

            SyncYPathSet(
                rootService,
                WithTransaction("//sys/@cell_id", transactionId),
                ConvertToYsonString(cellId));

            SyncYPathSet(
                rootService,
                WithTransaction("//sys/@cell_guid", transactionId),
                ConvertToYsonString(cellGuid));

            SyncYPathSet(
                rootService,
                WithTransaction("//sys/scheduler", transactionId),
                emptyOpaqueMap);

            SyncYPathSet(
                rootService,
                WithTransaction("//sys/scheduler/lock", transactionId),
                emptyMap);

            SyncYPathCreate(
                rootService,
                WithTransaction("//sys/scheduler/orchid", transactionId),
                EObjectType::Orchid);

            SyncYPathSet(
                rootService,
                WithTransaction("//sys/operations", transactionId),
                emptyOpaqueMap);

            SyncYPathCreate(
                rootService,
                WithTransaction("//sys/holders", transactionId),
                EObjectType::NodeMap);
            SyncYPathSet(
                rootService,
                WithTransaction("//sys/holders/@opaque", transactionId),
                TYsonString("true"));

            SyncYPathSet(
                rootService,
                WithTransaction("//sys/masters", transactionId),
                emptyOpaqueMap);

            FOREACH (const auto& address, Bootstrap->GetConfig()->MetaState->Cell->Addresses) {
                auto addressPath = "/" + EscapeYPathToken(address);
                SyncYPathSet(
                    rootService,
                    WithTransaction("//sys/masters" + addressPath, transactionId),
                    emptyMap);

                SyncYPathCreate(
                    rootService,
                    WithTransaction("//sys/masters" + addressPath + "/orchid", transactionId),
                    EObjectType::Orchid,
                    BuildYsonFluently()
                        .BeginMap()
                            .Item("remote_address").Scalar(address)
                        .EndMap().GetYsonString());
            }

            SyncYPathCreate(
                rootService,
                WithTransaction("//sys/chunks", transactionId),
                EObjectType::ChunkMap);

            SyncYPathCreate(
                rootService,
                WithTransaction("//sys/lost_chunks", transactionId),
                EObjectType::LostChunkMap);

            SyncYPathCreate(
                rootService,
                WithTransaction("//sys/overreplicated_chunks", transactionId),
                EObjectType::OverreplicatedChunkMap);

            SyncYPathCreate(
                rootService,
                WithTransaction("//sys/underreplicated_chunks", transactionId),
                EObjectType::UnderreplicatedChunkMap);

            SyncYPathCreate(
                rootService,
                WithTransaction("//sys/chunk_lists", transactionId),
                EObjectType::ChunkListMap);

            SyncYPathCreate(
                rootService,
                WithTransaction("//sys/transactions", transactionId),
                EObjectType::TransactionMap);

            SyncYPathSet(
                rootService,
                WithTransaction("//tmp", transactionId),
                emptyMap);

            SyncYPathSet(
                rootService,
                WithTransaction("//home", transactionId),
                emptyMap);

            CommitTransaction(transactionId);
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "World initialization failed");
        }

        LOG_INFO("World initialization completed");
    }

    void AbortTransactions()
    {
        auto transactionIds = Bootstrap->GetTransactionManager()->GetTransactionIds();
        auto service = Bootstrap->GetObjectManager()->GetRootService();
        FOREACH (const auto& transactionId, transactionIds) {
            auto req = TTransactionYPathProxy::Abort(FromObjectId(transactionId));
            SyncExecuteVerb(service, req);
        }
    }

    TTransactionId StartTransaction()
    {
        auto service = Bootstrap->GetObjectManager()->GetRootService();
        auto req = TTransactionYPathProxy::CreateObject(RootTransactionPath);
        req->set_type(EObjectType::Transaction);
        auto rsp = SyncExecuteVerb(service, req);
        return TTransactionId::FromProto(rsp->object_id());
    }

    void CommitTransaction(const TTransactionId& transactionId)
    {
        auto service = Bootstrap->GetObjectManager()->GetRootService();
        auto req = TTransactionYPathProxy::Commit(FromObjectId(transactionId));
        SyncExecuteVerb(service, req);
    }

    // TODO(babenko): consider moving somewhere
    static TObjectId SyncYPathCreate(
        IYPathServicePtr service,
        const TYPath& path,
        EObjectType type,
        const TYsonString& attributes = TYsonString("{}"))
    {
        auto req = TCypressYPathProxy::Create(path);
        req->set_type(type);
        req->Attributes().MergeFrom(ConvertToNode(attributes)->AsMap());
        auto rsp = SyncExecuteVerb(service, req);
        return TObjectId::FromProto(rsp->object_id());
    }
};

////////////////////////////////////////////////////////////////////////////////

TMetaStateFacade::TMetaStateFacade(
    TCellMasterConfigPtr config,
    TBootstrap* bootstrap)
    : Impl(New<TImpl>(config, bootstrap))
{ }

TMetaStateFacade::~TMetaStateFacade()
{ }

TCompositeMetaStatePtr TMetaStateFacade::GetState() const
{
    return Impl->GetState();
}

IMetaStateManagerPtr TMetaStateFacade::GetManager() const
{
    return Impl->GetManager();
}

bool TMetaStateFacade::IsInitialized() const
{
    return Impl->IsInitialized();
}

IInvokerPtr TMetaStateFacade::GetUnguardedInvoker(EStateThreadQueue queue) const
{
    return Impl->GetUnguardedInvoker(queue);
}

IInvokerPtr TMetaStateFacade::GetUnguardedEpochInvoker(EStateThreadQueue queue) const
{
    return Impl->GetUnguardedEpochInvoker(queue);
}

IInvokerPtr TMetaStateFacade::GetGuardedInvoker(EStateThreadQueue queue) const
{
    return Impl->GetGuardedInvoker(queue);
}

IInvokerPtr TMetaStateFacade::GetGuardedEpochInvoker(EStateThreadQueue queue) const
{
    return Impl->GetGuardedEpochInvoker(queue);
}

void TMetaStateFacade::Start()
{
    Impl->Start();
}

TMutationPtr TMetaStateFacade::CreateMutation(EStateThreadQueue queue)
{
    return New<TMutation>(
        GetManager(),
        GetGuardedEpochInvoker(queue));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellMaster
} // namespace NYT

