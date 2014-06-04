#include "stdafx.h"
#include "journal_writer.h"
#include "config.h"
#include "private.h"

#include <core/concurrency/scheduler.h>
#include <core/concurrency/delayed_executor.h>
#include <core/concurrency/periodic_executor.h>
#include <core/concurrency/thread_affinity.h>
#include <core/concurrency/nonblocking_queue.h>
#include <core/concurrency/parallel_collector.h>

#include <core/misc/address.h>
#include <core/misc/variant.h>

#include <core/ytree/attribute_helpers.h>

#include <core/logging/tagged_logger.h>

#include <ytlib/object_client/object_service_proxy.h>
#include <ytlib/object_client/master_ypath_proxy.h>
#include <ytlib/object_client/helpers.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>
#include <ytlib/cypress_client/rpc_helpers.h>

#include <ytlib/chunk_client/private.h>
#include <ytlib/chunk_client/dispatcher.h>
#include <ytlib/chunk_client/chunk_owner_ypath_proxy.h>
#include <ytlib/chunk_client/data_node_service_proxy.h>
#include <ytlib/chunk_client/chunk_ypath_proxy.h>
#include <ytlib/chunk_client/chunk_list_ypath_proxy.h>

#include <ytlib/journal_client/journal_ypath_proxy.h>

#include <ytlib/transaction_client/transaction_manager.h>

#include <ytlib/hydra/rpc_helpers.h>

#include <ytlib/node_tracker_client/node_directory.h>

#include <queue>
#include <deque>

namespace NYT {
namespace NApi {

using namespace NConcurrency;
using namespace NYTree;
using namespace NYPath;
using namespace NCypressClient;
using namespace NObjectClient;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NTransactionClient;
using namespace NJournalClient;
using namespace NNodeTrackerClient;

////////////////////////////////////////////////////////////////////////////////

class TJournalWriter
    : public IJournalWriter
{
public:
    TJournalWriter(
        IClientPtr client,
        const TYPath& path,
        const TJournalWriterOptions& options,
        TJournalWriterConfigPtr config)
        : Impl_(New<TImpl>(
            client,
            path,
            options,
            config))
    { }

    ~TJournalWriter()
    {
        Impl_->Cancel();
    }

    virtual TAsyncError Open() override
    {
        return Impl_->Open();
    }

    virtual TAsyncError Write(const std::vector<TSharedRef>& records) override
    {
        return Impl_->Write(records);
    }

    virtual TAsyncError Close() override
    {
        return Impl_->Close();
    }

private:
    // NB: PImpl is used to enable external lifetime control (see TJournalWriter::dtor and TImpl::Cancel).
    class TImpl
        : public TRefCounted
    {
    public:
        TImpl(
            IClientPtr client,
            const TYPath& path,
            const TJournalWriterOptions& options,
            TJournalWriterConfigPtr config)
            : Client_(client)
            , Path_(path)
            , Options_(options)
            , Config_(config ? config : New<TJournalWriterConfig>())
            , Proxy_(Client_->GetMasterChannel())
            , Logger(ApiLogger)
        {
            Logger.AddTag(Sprintf("Path: %s",
                ~Path_));

            // Spawn the actor.
            BIND(&TImpl::ActorMain, MakeStrong(this))
                // TODO(babenko): another invoker?
                .AsyncVia(NChunkClient::TDispatcher::Get()->GetWriterInvoker())
                .Run();
        }

        TAsyncError Open()
        {
            return OpenedPromise_;
        }

        TAsyncError Write(const std::vector<TSharedRef>& records)
        {
            TGuard<TSpinLock> guard(CurrentBatchSpinLock_);

            if (!Error_.IsOK()) {
                return MakeFuture(Error_);
            }

            auto batch = EnsureCurrentBatch();
            for (const auto& record : records) {
                AppendToBatch(batch, record);
                if (IsBatchFull(batch)) {
                    FlushCurrentBatch();
                    batch = EnsureCurrentBatch();
                }
            }

            // NB: We can form a handful of batches but since flushes are monotonic,
            // the last one will do.
            return batch->FlushedPromise;
        }

        TAsyncError Close()
        {
            EnqueueCommand(TCloseCommand());
            return ClosedPromise_;
        }

        void Cancel()
        {
            EnqueueCommand(TCancelCommand());
        }

    private:
        IClientPtr Client_;
        TYPath Path_;
        TJournalWriterOptions Options_;
        TJournalWriterConfigPtr Config_;

        IInvokerPtr Invoker_;
        TObjectServiceProxy Proxy_;

        NLog::TTaggedLogger Logger;

        struct TBatch
            : public TIntrinsicRefCounted
        {
            int FirstRecordIndex = -1;

            i64 DataSize = 0;
            std::vector<TSharedRef> Records;
            TAsyncErrorPromise FlushedPromise = NewPromise<TError>();
            int FlushCounter = 0;
        };

        typedef TIntrusivePtr<TBatch> TBatchPtr;

        TSpinLock CurrentBatchSpinLock_;
        TError Error_;
        TBatchPtr CurrentBatch_;
        TDelayedExecutor::TCookie CurrentBatchFlushCookie_;

        TAsyncErrorPromise OpenedPromise_ = NewPromise<TError>();

        bool Closing_ = false;
        TAsyncErrorPromise ClosedPromise_ = NewPromise<TError>();

        TTransactionPtr UploadTransaction_;
        
        int ReplicationFactor_ = -1;
        int WriteConcern_ = -1;
        Stroka Account_;

        TChunkListId ChunkListId_;

        struct TNode
            : public TRefCounted
        {
            TNodeDescriptor Descriptor;
            TDataNodeServiceProxy LightProxy;
            TDataNodeServiceProxy HeavyProxy;
            TPeriodicExecutorPtr PingExecutor;

            bool FlushInProgress = false;
            int FirstBlockIndex = 0;
            std::queue<TBatchPtr> PendingBatches;

            explicit TNode(const TNodeDescriptor& descriptor)
                : Descriptor(descriptor)
                , LightProxy(LightNodeChannelFactory->CreateChannel(descriptor.Address))
                , HeavyProxy(HeavyNodeChannelFactory->CreateChannel(descriptor.Address))
            { }
        };

        typedef TIntrusivePtr<TNode> TNodePtr;
        typedef TWeakPtr<TNode> TNodeWeakPtr;

        TNodeDirectoryPtr NodeDirectory_ = New<TNodeDirectory>();

        struct TChunkSession
            : public TIntrinsicRefCounted
        {
            TChunkId ChunkId;
            std::vector<TNodePtr> Nodes;
            int FlushedRecordCount = 0;
        };

        typedef TIntrusivePtr<TChunkSession> TChunkSessionPtr;

        TChunkSessionPtr CurrentSession_;

        int CurrentRecordIndex_ = 0;
        std::deque<TBatchPtr> PendingBatches_;

        typedef TBatchPtr TBatchCommand;

        struct TCloseCommand { };
        
        struct TCancelCommand { };

        struct TSwitchChunkCommand
        {
            TChunkSessionPtr Session;
        };

        typedef TVariant<
            TBatchCommand,
            TCloseCommand,
            TCancelCommand,
            TSwitchChunkCommand
        > TCommand;

        TNonblockingQueue<TCommand> CommandQueue_;


        void EnqueueCommand(TCommand command)
        {
            CommandQueue_.Enqueue(std::move(command));
        }
        
        TCommand DequeueCommand()
        {
            return WaitFor(CommandQueue_.Dequeue());
        }


        void OpenJournal()
        {
            LOG_INFO("Creating upload transaction");
    
            {
                NTransactionClient::TTransactionStartOptions options;
                options.EnableUncommittedAccounting = false;
                auto attributes = CreateEphemeralAttributes();
                attributes->Set("title", Sprintf("Journal upload to %s", ~Path_));
                options.Attributes = attributes.get();

                auto transactionManager = Client_->GetTransactionManager();
                auto transactionOrError = WaitFor(transactionManager->Start(
                    ETransactionType::Master,
                    options));
                THROW_ERROR_EXCEPTION_IF_FAILED(transactionOrError, "Error creating upload transaction");
                UploadTransaction_ = transactionOrError.Value();
            }

            LOG_INFO("Upload transaction created (TransactionId: %s)",
                ~ToString(UploadTransaction_->GetId()));
            

            LOG_INFO("Requesting journal info");

            TObjectServiceProxy proxy(Client_->GetMasterChannel());
            auto batchReq = proxy.ExecuteBatch();

            {
                auto req = TCypressYPathProxy::Get(Path_);
                SetTransactionId(req, UploadTransaction_->GetId());
                TAttributeFilter attributeFilter(EAttributeFilterMode::MatchingOnly);
                attributeFilter.Keys.push_back("type");
                attributeFilter.Keys.push_back("replication_factor");
                attributeFilter.Keys.push_back("write_concern");
                attributeFilter.Keys.push_back("account");
                ToProto(req->mutable_attribute_filter(), attributeFilter);
                batchReq->AddRequest(req, "get_attributes");
            }

            {
                auto req = TJournalYPathProxy::PrepareForUpdate(Path_);
                req->set_mode(EUpdateMode::Append);
                NHydra::GenerateMutationId(req);
                SetTransactionId(req, UploadTransaction_->GetId());
                batchReq->AddRequest(req, "prepare_for_update");
            }

            auto batchRsp = WaitFor(batchReq->Invoke());
            THROW_ERROR_EXCEPTION_IF_FAILED(*batchRsp, "Error requesting journal info");

            {
                auto rsp = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_attributes");
                THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error getting journal attributes");

                auto node = ConvertToNode(TYsonString(rsp->value()));
                const auto& attributes = node->Attributes();

                auto type = attributes.Get<EObjectType>("type");
                if (type != EObjectType::Journal) {
                    THROW_ERROR_EXCEPTION("Invalid type of %s: expected %s, actual %s",
                        ~Path_,
                        ~FormatEnum(EObjectType(EObjectType::Journal)).Quote(),
                        ~FormatEnum(type).Quote());
                }

                ReplicationFactor_ = attributes.Get<int>("replication_factor");
                WriteConcern_ = attributes.Get<int>("write_concern");
                Account_ = attributes.Get<Stroka>("account");
            }

            {
                auto rsp = batchRsp->GetResponse<TJournalYPathProxy::TRspPrepareForUpdate>("prepare_for_update");
                THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error preparing journal for update");
                ChunkListId_ = FromProto<TChunkListId>(rsp->chunk_list_id());
            }

            LOG_INFO("Journal info received (ReplicationFactor: %d, WriteConcern: %d, Account: %s, ChunkListId: %s)",
                ReplicationFactor_,
                WriteConcern_,
                ~Account_,
                ~ToString(ChunkListId_));

            LOG_INFO("Journal writer opened");
            OpenedPromise_.Set(TError());
        }

        void CloseJournal()
        {
            LOG_INFO("Journal writer closed");
        }

        void OpenChunk()
        {
            CurrentSession_ = New<TChunkSession>();

            LOG_INFO("Creating chunk");

            std::vector<TChunkReplica> replicas;
            std::vector<TNodeDescriptor> targets;
            {
                auto req = TMasterYPathProxy::CreateObjects();
                req->set_type(EObjectType::JournalChunk);
                req->set_account(Account_);
                ToProto(req->mutable_transaction_id(), UploadTransaction_->GetId());

                auto* reqExt = req->MutableExtension(TReqCreateChunkExt::create_chunk_ext);
                if (Config_->PreferLocalHost) {
                    reqExt->set_preferred_host_name(TAddressResolver::Get()->GetLocalHostName());
                }
                reqExt->set_replication_factor(ReplicationFactor_);
                reqExt->set_upload_replication_factor(ReplicationFactor_);
                reqExt->set_movable(true);
                reqExt->set_vital(true);
                reqExt->set_erasure_codec(NErasure::ECodec::None);

                auto rsp = WaitFor(Proxy_.Execute(req));
                THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error creating chunk");
                CurrentSession_->ChunkId = FromProto<TChunkId>(rsp->object_ids(0));

                const auto& rspExt = rsp->GetExtension(TRspCreateChunkExt::create_chunk_ext);
                NodeDirectory_->MergeFrom(rspExt.node_directory());

                replicas = NYT::FromProto<TChunkReplica>(rspExt.replicas());
                if (replicas.size() < ReplicationFactor_) {
                    THROW_ERROR_EXCEPTION("Not enough data nodes available: %d received, %d needed",
                        static_cast<int>(replicas.size()),
                        ReplicationFactor_);
                }

                for (auto replica : replicas) {
                    const auto& descriptor = NodeDirectory_->GetDescriptor(replica);
                    targets.push_back(descriptor);
                }
            }

            LOG_INFO("Chunk created (ChunkId: %s, Targets: [%s])",
                ~ToString(CurrentSession_->ChunkId),
                ~JoinToString(targets));

            for (int index = 0; index < ReplicationFactor_; ++index) {
                auto node = New<TNode>(targets[index]);
                node->LightProxy.SetDefaultTimeout(Config_->NodeRpcTimeout);
                node->HeavyProxy.SetDefaultTimeout(Config_->NodeRpcTimeout);
                node->PingExecutor = New<TPeriodicExecutor>(
                    GetCurrentInvoker(),
                    BIND(&TImpl::SendPing, MakeWeak(this), MakeWeak(node), CurrentSession_),
                    Config_->NodePingPeriod);
                CurrentSession_->Nodes.push_back(node);
            }

            LOG_INFO("Starting chunk sessions");
            {
                auto collector = New<TParallelCollector<void>>();
                for (auto node : CurrentSession_->Nodes) {
                    auto req = node->LightProxy.StartChunk();
                    ToProto(req->mutable_chunk_id(), CurrentSession_->ChunkId);
                    req->set_session_type(EWriteSessionType::User);
                    auto asyncRsp = req->Invoke().Apply(
                        BIND(&TImpl::OnChunkStarted, MakeStrong(this), node)
                            .AsyncVia(GetCurrentInvoker()));
                    collector->Collect(asyncRsp);
                }
                auto result = WaitFor(collector->Complete());
                THROW_ERROR_EXCEPTION_IF_FAILED(result, "Error starting chunk sessions");
            }
            LOG_INFO("Chunk sessions started");

            LOG_INFO("Attaching chunk");
            {
                auto batchReq = Proxy_.ExecuteBatch();
                {
                    auto req = TChunkYPathProxy::Confirm(FromObjectId(CurrentSession_->ChunkId));
                    req->mutable_chunk_info();
                    ToProto(req->mutable_replicas(), replicas);
                    auto* meta = req->mutable_chunk_meta();
                    meta->set_type(EChunkType::Journal);
                    meta->set_version(0);
                    meta->mutable_extensions();
                    NHydra::GenerateMutationId(req);
                    batchReq->AddRequest(req, "confirm");
                }
                {
                    auto req = TChunkListYPathProxy::Attach(FromObjectId(ChunkListId_));
                    ToProto(req->add_children_ids(), CurrentSession_->ChunkId);
                    NHydra::GenerateMutationId(req);
                    batchReq->AddRequest(req, "attach");
                }

                auto batchRsp = WaitFor(batchReq->Invoke());
                THROW_ERROR_EXCEPTION_IF_FAILED(batchRsp->GetCumulativeError(), "Error attaching chunk");
            }
            LOG_INFO("Chunk attached");
        
            for (auto batch : PendingBatches_) {
                EnqueueBatchToNodes(batch);
            }
        }

        void WriteChunk()
        {
            while (true) {
                auto someCommand = DequeueCommand();
                if (auto* command = someCommand.TryAs<TCloseCommand>()) {
                    HandleClose();
                    break;
                } else if (auto* command = someCommand.TryAs<TCancelCommand>()) {
                    throw TFiberCanceledException();
                } else if (auto* command = someCommand.TryAs<TBatchCommand>()) {
                    HandleBatch(*command);
                } else if (auto* command = someCommand.TryAs<TSwitchChunkCommand>()) {
                    if (command->Session == CurrentSession_) {
                        HandleSwitchChunk();
                        break;
                    }
                }
            }
        }

        void HandleClose()
        {
            LOG_INFO("Closing journal writer");
            Closing_ = true;
        }

        void HandleBatch(TBatchPtr batch)
        {
            int recordCount = static_cast<int>(batch->Records.size());

            LOG_DEBUG("Records batch ready (Records: %d-%d)",
                CurrentRecordIndex_,
                CurrentRecordIndex_ + recordCount - 1);

            batch->FirstRecordIndex = CurrentRecordIndex_;
            CurrentRecordIndex_ += recordCount;

            PendingBatches_.push_back(batch);
            EnqueueBatchToNodes(batch);
        }

        void EnqueueBatchToNodes(TBatchPtr batch)
        {
            for (auto node : CurrentSession_->Nodes) {
                node->PendingBatches.push(batch);
                MaybeFlushBlocks(node);
            }
        }

        void HandleSwitchChunk()
        {
            LOG_INFO("Switching chunk");
        }

        void CloseChunk()
        {
            // NB: Fire-and-forget.
            LOG_INFO("Finishing chunk sessions");
            for (auto node : CurrentSession_->Nodes) {
                auto req = node->LightProxy.FinishChunk();
                ToProto(req->mutable_chunk_id(), CurrentSession_->ChunkId);
                req->Invoke().Subscribe(
                    BIND(&TImpl::OnChunkFinished, MakeStrong(this), node)
                        .Via(GetCurrentInvoker()));
            }
            
            LOG_INFO("Sealing chunk (ChunkId: %s, RecordCount: %d)",
                ~ToString(CurrentSession_->ChunkId),
                CurrentSession_->FlushedRecordCount);
            {
                auto req = TChunkYPathProxy::Seal(FromObjectId(CurrentSession_->ChunkId));
                req->set_record_count(CurrentSession_->FlushedRecordCount);
                auto rsp = WaitFor(Proxy_.Execute(req));
                THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error sealing chunk");
            }
            LOG_INFO("Chunk sealed");

            for (auto node : CurrentSession_->Nodes) {
                node->PingExecutor->Stop();
            }

            CurrentSession_.Reset();
        }


        void ActorMain()
        {
            try {
                GuardedActorMain();
            } catch (const std::exception& ex) {
                PumpFailed(ex);
            }
        }

        void GuardedActorMain()
        {
            OpenJournal();
            do {
                OpenChunk();
                WriteChunk();
                CloseChunk();
            } while (!Closing_ || !PendingBatches_.empty());
            CloseJournal();
        }

        void PumpFailed(const TError& error)
        {
            LOG_WARNING(error, "Journal writer failed");

            {
                TGuard<TSpinLock> guard(CurrentBatchSpinLock_);
                Error_ = error;
                if (CurrentBatch_) {
                    auto promise = CurrentBatch_->FlushedPromise;
                    CurrentBatch_.Reset();
                    guard.Release();
                    promise.Set(error);
                }
            }

            OpenedPromise_.TrySet(error);
            ClosedPromise_.TrySet(error);

            for (auto batch : PendingBatches_) {
                batch->FlushedPromise.Set(error);
            }
            PendingBatches_.clear();

            while (true) {
                auto someCommand = DequeueCommand();
                if (auto* command = someCommand.TryAs<TBatchCommand>()) {
                    (*command)->FlushedPromise.Set(error);
                } else if (auto* command = someCommand.TryAs<TCancelCommand>()) {
                    throw TFiberCanceledException();
                } else {
                    // Ignore.
                }
            }
        }


        static void AppendToBatch(const TBatchPtr& batch, const TSharedRef& record)
        {
            YASSERT(record);
            batch->Records.push_back(record);
            batch->DataSize += record.Size();
        }

        bool IsBatchFull(const TBatchPtr& batch)
        {
            return
                batch->DataSize > Config_->MaxBatchDataSize ||
                batch->Records.size() > Config_->MaxBatchRecordCount;
        }


        TBatchPtr EnsureCurrentBatch()
        {
            VERIFY_SPINLOCK_AFFINITY(CurrentBatchSpinLock_);

            if (!CurrentBatch_) {
                CurrentBatch_ = New<TBatch>();
                CurrentBatchFlushCookie_ = TDelayedExecutor::Submit(
                    BIND(&TImpl::OnBatchTimeout, MakeWeak(this), CurrentBatch_)
                        .Via(GetCurrentInvoker()),
                    Config_->MaxBatchDelay);
            }

            return CurrentBatch_;
        }

        void OnBatchTimeout(TBatchPtr batch)
        {
            TGuard<TSpinLock> guard(CurrentBatchSpinLock_);
            if (CurrentBatch_ == batch) {
                FlushCurrentBatch();
            }
        }

        void FlushCurrentBatch()
        {
            VERIFY_SPINLOCK_AFFINITY(CurrentBatchSpinLock_);

            if (CurrentBatchFlushCookie_) {
                TDelayedExecutor::CancelAndClear(CurrentBatchFlushCookie_);
            }

            EnqueueCommand(TBatchCommand(CurrentBatch_));
            CurrentBatch_.Reset();
        }
  

        void SendPing(TNodeWeakPtr node, TChunkSessionPtr session)
        {
            auto node_ = node.Lock();
            if (!node_)
                return;

            LOG_DEBUG("Sending ping (Address: %s)",
                ~node_->Descriptor.Address);

            auto req = node_->LightProxy.PingSession();
            ToProto(req->mutable_chunk_id(), session->ChunkId);
            req->Invoke();
        }

        TError OnChunkStarted(TNodePtr node, TDataNodeServiceProxy::TRspStartChunkPtr rsp)
        {
            if (rsp->IsOK()) {
                LOG_DEBUG("Chunk session started (Address: %s)",
                    ~node->Descriptor.Address);
                return TError();
            } else {
                return TError("Error starting session at %s",
                    ~node->Descriptor.Address)
                    << *rsp;
            }
        }

        void OnChunkFinished(TNodePtr node, TDataNodeServiceProxy::TRspFinishChunkPtr rsp)
        {
            if (rsp->IsOK()) {
                LOG_DEBUG("Chunk session finished (Address: %s)",
                    ~node->Descriptor.Address);
            } else {
                LOG_WARNING(*rsp, "Chunk session has failed to finish (Address: %s)",
                    ~node->Descriptor.Address);
            }
        }


        void MaybeFlushBlocks(TNodePtr node)
        {
            if (node->FlushInProgress || node->PendingBatches.empty())
                return;

            auto batch = node->PendingBatches.front();
            node->PendingBatches.pop();

            int firstBlockIndex = node->FirstBlockIndex;
            int lastLastIndex = firstBlockIndex + batch->Records.size() - 1;

            LOG_DEBUG("Flushing journal replica (Address: %s, BlockIds: %s:%d-%d)",
                ~ToString(CurrentSession_->ChunkId),
                firstBlockIndex,
                lastLastIndex);

            auto req = node->HeavyProxy.PutBlocks();
            ToProto(req->mutable_chunk_id(), CurrentSession_->ChunkId);
            req->set_first_block_index(node->FirstBlockIndex);
            req->set_flush_blocks(true);
            req->Attachments() = batch->Records;

            node->FlushInProgress = true;

            req->Invoke().Subscribe(
                BIND(&TImpl::OnBlocksFlushed, MakeWeak(this), CurrentSession_, node, batch, firstBlockIndex, lastLastIndex)
                    .Via(GetCurrentInvoker()));
        }

        void OnBlocksFlushed(
            TChunkSessionPtr session,
            TNodePtr node,
            TBatchPtr batch,
            int firstBlockIndex,
            int lastBlockIndex,
            TDataNodeServiceProxy::TRspPutBlocksPtr rsp)
        {
            if (session != CurrentSession_)
                return;

            if (rsp->IsOK()) {
                LOG_DEBUG("Journal replica flushed (Address: %s, BlockIds: %s:%d-%d)",
                    ~node->Descriptor.Address,
                    ~ToString(session->ChunkId),
                    firstBlockIndex,
                    lastBlockIndex);

                node->FirstBlockIndex = lastBlockIndex + 1;
                node->FlushInProgress = false;

                ++batch->FlushCounter;

                while (!PendingBatches_.empty()) {
                    auto front = PendingBatches_.front();
                    if (front->FlushCounter <  WriteConcern_)
                        break;

                    front->FlushedPromise.Set(TError());
                    int recordCount = static_cast<int>(front->Records.size());
                    session->FlushedRecordCount += recordCount;
                    PendingBatches_.pop_front();

                    LOG_DEBUG("Records flushed (Records: %d-%d)",
                        front->FirstRecordIndex,
                        front->FirstRecordIndex + recordCount - 1);
                }

                MaybeFlushBlocks(node);
            } else {
                LOG_WARNING(*rsp, "Journal replica failed (Address: %s, BlockIds: %s:%d-%d)",
                    ~node->Descriptor.Address,
                    ~ToString(session->ChunkId),
                    firstBlockIndex,
                    lastBlockIndex);

                TSwitchChunkCommand command;
                command.Session = session;
                EnqueueCommand(command);
            }
        }

    };


    TIntrusivePtr<TImpl> Impl_;

};

IJournalWriterPtr CreateJournalWriter(
    IClientPtr client,
    const TYPath& path,
    const TJournalWriterOptions& options,
    TJournalWriterConfigPtr config)
{
    return New<TJournalWriter>(
        client,
        path,
        options,
        config);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NApi
} // namespace NYT
