#include "stdafx.h"
#include "server_detail.h"

#include "../misc/assert.h"

namespace NYT {
namespace NRpc {

using namespace NBus;

////////////////////////////////////////////////////////////////////////////////

TServiceContextBase::TServiceContextBase(
    const TRequestId& requestId,
    const Stroka& path,
    const Stroka& verb,
    IMessage* requestMessage)
    : RequestId(requestId)
    , Path(path)
    , Verb(verb)
    , Replied(false)
{
    YASSERT(requestMessage != NULL);

    RequestBody = requestMessage->GetParts().at(1);
    RequestAttachments = yvector<TSharedRef>(
        requestMessage->GetParts().begin() + 2,
        requestMessage->GetParts().end());
}

bool TServiceContextBase::IsReplied() const
{
    return Replied;
}

void TServiceContextBase::Reply(const TError& error)
{
    // Failure here means that #Reply is called twice.
    YASSERT(!Replied);

    Replied = true;
    LogResponse(error);

    IMessage::TPtr responseMessage;
    if (error.IsOK()) {
        responseMessage = CreateResponseMessage(
            RequestId,
            error,
            MoveRV(ResponseBody),
            ResponseAttachments);
    } else {
        responseMessage = CreateErrorResponseMessage(
            RequestId,
            error);
    }

    DoReply(error, ~responseMessage);
}

TSharedRef TServiceContextBase::GetRequestBody() const
{
    return RequestBody;
}

const yvector<TSharedRef>& TServiceContextBase::GetRequestAttachments() const
{
    return RequestAttachments;
}

void TServiceContextBase::SetResponseBody(TBlob&& responseBody)
{
    // TODO: change when STL finally supports rv-refs
    // ResponseBody = MoveRV(responseBody);
    ResponseBody.swap(responseBody);
}

void TServiceContextBase::SetResponseAttachments(const yvector<TSharedRef>& attachments)
{
    ResponseAttachments = MoveRV(attachments);
}

Stroka TServiceContextBase::GetPath() const
{
    return Path;
}

Stroka TServiceContextBase::GetVerb() const
{
    return Verb;
}

void TServiceContextBase::SetRequestInfo(const Stroka& info)
{
    RequestInfo = info;
    LogRequest();
}

Stroka TServiceContextBase::GetRequestInfo() const
{
    return RequestInfo;
}

void TServiceContextBase::SetResponseInfo(const Stroka& info)
{
    ResponseInfo = info;
}

Stroka TServiceContextBase::GetResponseInfo()
{
    return ResponseInfo;
}

IAction::TPtr TServiceContextBase::Wrap(IAction* action)
{
    return FromMethod(
        &TServiceContextBase::WrapThunk,
        TPtr(this),
        action);
}

void TServiceContextBase::WrapThunk(IAction::TPtr action) throw()
{
    try {
        action->Do();
    } catch (const TServiceException& ex) {
        Reply(ex.GetError());
    } catch (...) {
        auto message = CurrentExceptionMessage();
        Reply(TError(EErrorCode::ServiceError, message));
        LogException(message);
    }
}

void TServiceContextBase::AppendInfo(Stroka& lhs, const Stroka& rhs)
{
    if (!rhs.Empty()) {
        if (!lhs.Empty()) {
            lhs.append(", ");
        }
        lhs.append(rhs);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
