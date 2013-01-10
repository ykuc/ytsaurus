#include "stdafx.h"
#include "file_writer.h"
#include "file_chunk_output.h"
#include "config.h"
#include "private.h"

#include <ytlib/object_client/object_service_proxy.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>

#include <ytlib/chunk_client/chunk_list_ypath_proxy.h>

#include <ytlib/file_client/file_ypath_proxy.h>

#include <ytlib/transaction_client/transaction_manager.h>
#include <ytlib/transaction_client/transaction.h>

#include <ytlib/meta_state/rpc_helpers.h>

namespace NYT {
namespace NFileClient {

using namespace NYTree;
using namespace NCypressClient;
using namespace NObjectClient;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NTransactionClient;

////////////////////////////////////////////////////////////////////////////////

TFileWriter::TFileWriter(
    TFileWriterConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    ITransactionPtr transaction,
    TTransactionManagerPtr transactionManager,
    const TYPath& path,
    IAttributeDictionary* attributes)
    : Config(config)
    , MasterChannel(masterChannel)
    , Transaction(transaction)
    , TransactionManager(transactionManager)
    , Path(path)
    , Attributes(attributes ? attributes->Clone() : CreateEphemeralAttributes())
    , Logger(FileWriterLogger)
{
    YCHECK(transactionManager);

    Logger.AddTag(Sprintf("Path: %s, TransactionId: %s",
        ~Path,
        transaction ? ~transaction->GetId().ToString() : "None"));

    Attributes->Set("replication_factor", Config->ReplicationFactor);
    
    if (Transaction) {
        ListenTransaction(Transaction);
    }
}

TFileWriter::~TFileWriter()
{ }

void TFileWriter::Open()
{
    CheckAborted();

    LOG_INFO("Creating upload transaction");
    try {
        TTransactionStartOptions options;
        options.ParentId = Transaction ? Transaction->GetId() : NullTransactionId;
        options.EnableUncommittedAccounting = false;
        UploadTransaction = TransactionManager->Start(options);
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error creating upload transaction")
            << ex;
    }

    ListenTransaction(UploadTransaction);
    LOG_INFO("Upload transaction created (TransactionId: %s)",
        ~UploadTransaction->GetId().ToString());

    TObjectServiceProxy proxy(MasterChannel);
    
    LOG_INFO("Creating file node");
    {
        auto req = TCypressYPathProxy::Create(Path);
        NMetaState::GenerateRpcMutationId(req);
        SetTransactionId(req, UploadTransaction);
        req->set_type(EObjectType::File);
        ToProto(req->mutable_node_attributes(), *Attributes);

        auto rsp = proxy.Execute(req).Get();
        THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error creating file node");

        NodeId = NCypressClient::TNodeId::FromProto(rsp->node_id());
    }
    LOG_INFO("File node created (NodeId: %s)", ~NodeId.ToString());

    LOG_INFO("Requesting file info");
    {
        auto batchReq = proxy.ExecuteBatch();

        {
            auto req = TCypressYPathProxy::Get(FromObjectId(NodeId));
            SetTransactionId(req, UploadTransaction);
            TAttributeFilter attributeFilter(EAttributeFilterMode::MatchingOnly);
            attributeFilter.Keys.push_back("replication_factor");
            attributeFilter.Keys.push_back("account");
            *req->mutable_attribute_filter() = ToProto(attributeFilter);
            batchReq->AddRequest(req, "get_attributes");
        }

        {
            auto req = TFileYPathProxy::PrepareForUpdate(FromObjectId(NodeId));
            NMetaState::GenerateRpcMutationId(req);
            SetTransactionId(req, UploadTransaction);
            batchReq->AddRequest(req, "prepare_for_update");
        }

        auto batchRsp = batchReq->Invoke().Get();
        THROW_ERROR_EXCEPTION_IF_FAILED(*batchRsp, "Error preparing node for update");

        {
            auto rsp = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_attributes");
            THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error getting file attributes");

            auto node = ConvertToNode(TYsonString(rsp->value()));
            const auto& attributes = node->Attributes();

            Config->ReplicationFactor = attributes.Get<int>("replication_factor");

            Account = attributes.Get<Stroka>("account");
        }

        {
            auto rsp = batchRsp->GetResponse<TFileYPathProxy::TRspPrepareForUpdate>("prepare_for_update");
            THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error preparing node for update");
            ChunkListId = TChunkListId::FromProto(rsp->chunk_list_id());
        }
    }
    LOG_INFO("File info received (Account: %s, ChunkListId: %s)",
        ~Account,
        ~ChunkListId.ToString());

    Writer = new TFileChunkOutput(
        Config,
        MasterChannel,
        UploadTransaction->GetId(),
        Account);
    Writer->Open();
}

void TFileWriter::Write(const TRef& data)
{
    CheckAborted();
    Writer->Write(data.Begin(), data.Size());
}

void TFileWriter::Close()
{
    CheckAborted();

    Writer->Finish();

    TObjectServiceProxy proxy(MasterChannel);
    auto chunkId = Writer->GetChunkId();

    LOG_INFO("Attaching chunk (ChunkId: %s)", ~ToString(chunkId));
    {
        auto req = TChunkListYPathProxy::Attach(FromObjectId(ChunkListId));
        *req->add_children_ids() = chunkId.ToProto();
        
        auto rsp = proxy.Execute(req).Get();
        THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error attaching chunk");
    }
    LOG_INFO("Chunk attached");

    LOG_INFO("Committing upload transaction");
    try {
        UploadTransaction->Commit();
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error committing upload transaction")
            << ex;
    }
    LOG_INFO("Upload transaction committed");
}

NCypressClient::TNodeId TFileWriter::GetNodeId() const
{
    YCHECK(NodeId != NullObjectId);
    return NodeId;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileClient
} // namespace NYT
