#include "stdafx.h"
#include "cypress_service.h"
#include "node_proxy.h"

#include "../ytree/ypath_detail.h"

namespace NYT {
namespace NCypress {

using namespace NBus;
using namespace NYTree;
using namespace NMetaState;
using namespace NTransaction;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = CypressLogger;

////////////////////////////////////////////////////////////////////////////////

TCypressService::TCypressService(
    IInvoker* invoker,
    TCypressManager* cypressManager,
    TTransactionManager* transactionManager,
    NRpc::IServer* server)
    : NRpc::TServiceBase(
        invoker,
        TCypressServiceProxy::GetServiceName(),
        CypressLogger.GetCategory())
    , CypressManager(cypressManager)
    , TransactionManager(transactionManager)
{
    YASSERT(cypressManager != NULL);
    YASSERT(server != NULL);

    RegisterMethod(RPC_SERVICE_METHOD_DESC(Execute));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(GetNodeId));

    server->RegisterService(this);
}

void TCypressService::ValidateTransactionId(const TTransactionId& transactionId)
{
    if (transactionId != NullTransactionId &&
        TransactionManager->FindTransaction(transactionId) == NULL)
    {
        ythrow TServiceException(EErrorCode::NoSuchTransaction) << 
            Sprintf("Invalid transaction id (TransactionId: %s)", ~transactionId.ToString());
    }
}

////////////////////////////////////////////////////////////////////////////////

RPC_SERVICE_METHOD_IMPL(TCypressService, Execute)
{
    UNUSED(response);

    auto transactionId = TTransactionId::FromProto(request->GetTransactionId());

    const auto& attachments = request->Attachments();
    YASSERT(attachments.ysize() >= 2);

    TYPath path;
    Stroka verb;
    ParseYPathRequestHeader(
        attachments[0],
        &path,
        &verb);

    context->SetRequestInfo("TransactionId: %s, Path: %s, Verb: %s",
        ~transactionId.ToString(),
        ~path,
        ~verb);

    ValidateTransactionId(transactionId);

    auto root = CypressManager->GetNodeProxy(RootNodeId, transactionId);
    auto rootService = IYPathService::FromNode(~root);

    IYPathService::TPtr suffixService;
    TYPath suffixPath;
    try {
        ResolveYPath(~rootService, path, verb, &suffixService, &suffixPath);
    } catch (...) {
        ythrow TServiceException(EErrorCode::ResolutionError) << CurrentExceptionMessage();
    }

    LOG_DEBUG("Execute: SuffixPath: %s", ~suffixPath);

    auto requestMessage = UnwrapYPathRequest(~context->GetUntypedContext());
    auto updatedRequestMessage = UpdateYPathRequestHeader(~requestMessage, suffixPath, verb);
    auto innerContext = CreateYPathContext(
        ~updatedRequestMessage,
        suffixPath,
        verb,
        Logger.GetCategory(),
        ~FromFunctor([=] (const TYPathResponseHandlerParam& param)
            {
                WrapYPathResponse(~context->GetUntypedContext(), ~param.Message);
                context->Reply();
            }));

    CypressManager->ExecuteVerb(~suffixService, ~innerContext);
}

RPC_SERVICE_METHOD_IMPL(TCypressService, GetNodeId)
{
    auto transactionId = TTransactionId::FromProto(request->GetTransactionId());
    Stroka path = request->GetPath();

    context->SetRequestInfo("TransactionId: %s, Path: %s",
        ~transactionId.ToString(),
        ~path);

    ValidateTransactionId(transactionId);

    auto root = CypressManager->GetNodeProxy(RootNodeId, transactionId);
    auto rootService = IYPathService::FromNode(~root);

    IYPathService::TPtr targetService;
    try {
        targetService = ResolveYPath(~rootService, path);
    } catch (...) {
        ythrow TServiceException(EErrorCode::ResolutionError) << CurrentExceptionMessage();
    }

    auto* targetNode = dynamic_cast<ICypressNodeProxy*>(~targetService);
    if (targetNode == NULL) {
        ythrow TServiceException(EErrorCode::ResolutionError) << "Path does not resolve to a physical node";
    }

    auto id = targetNode->GetNodeId();
    response->SetNodeId(targetNode->GetNodeId().ToProto());
    context->SetResponseInfo("NodeId: %s", ~id.ToString());
    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT
