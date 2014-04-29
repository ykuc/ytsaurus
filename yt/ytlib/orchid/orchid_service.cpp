#include "stdafx.h"
#include "orchid_service.h"

#include <core/ytree/ypath_detail.h>
#include <core/ytree/ypath_client.h>

#include <core/rpc/message.h>

namespace NYT {
namespace NOrchid {

using namespace NBus;
using namespace NRpc;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = OrchidLogger;

////////////////////////////////////////////////////////////////////////////////

TOrchidService::TOrchidService(
    NYTree::INodePtr root,
    IInvokerPtr invoker)
    : NRpc::TServiceBase(
        invoker,
        TOrchidServiceProxy::GetServiceName(),
        OrchidLogger.GetCategory())
{
    YCHECK(root);

    RootService = CreateRootService(root);
    RegisterMethod(RPC_SERVICE_METHOD_DESC(Execute));
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_RPC_SERVICE_METHOD(TOrchidService, Execute)
{
    UNUSED(request);
    UNUSED(response);

    auto requestMessage = TSharedRefArray(request->Attachments());

    NRpc::NProto::TRequestHeader requestHeader;
    if (!ParseRequestHeader(requestMessage, &requestHeader)) {
        THROW_ERROR_EXCEPTION("Error parsing request header");
    }

    auto path = GetRequestYPath(context);
    const auto& method = requestHeader.method();

    context->SetRequestInfo("Path: %s, Method: %s",
        ~path,
        ~method);

    ExecuteVerb(RootService, requestMessage)
        .Subscribe(BIND([=] (TSharedRefArray responseMessage) {
            NRpc::NProto::TResponseHeader responseHeader;
            YCHECK(ParseResponseHeader(responseMessage, &responseHeader));

            auto error = FromProto<TError>(responseHeader.error());

            context->SetResponseInfo("InnerError: %s", ~ToString(error));

            response->Attachments() = responseMessage.ToVector();
            context->Reply();
        }));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NOrchid
} // namespace NYT

