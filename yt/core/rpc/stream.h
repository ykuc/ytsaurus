#pragma once

#include "channel.h"

#include <yt/core/concurrency/async_stream.h>
#include <yt/core/concurrency/delayed_executor.h>

#include <yt/core/misc/ref.h>
#include <yt/core/misc/ring_queue.h>
#include <yt/core/misc/sliding_window.h>
#include <yt/core/misc/memory_zone.h>

#include <yt/core/actions/signal.h>
#include <yt/core/actions/future.h>

#include <yt/core/compression/public.h>

namespace NYT::NRpc {

////////////////////////////////////////////////////////////////////////////////

//! For empty and null attachments returns 1; for others returns the actual size.
size_t GetStreamingAttachmentSize(TRef attachment);

////////////////////////////////////////////////////////////////////////////////

class TAttachmentsInputStream
    : public NConcurrency::IAsyncZeroCopyInputStream
{
public:
    TAttachmentsInputStream(
        TClosure readCallback,
        IInvokerPtr compressionInvoker,
        std::optional<TDuration> timeout = {});

    virtual TFuture<TSharedRef> Read() override;

    void EnqueuePayload(const TStreamingPayload& payload);
    void Abort(const TError& error);
    void AbortUnlessClosed(const TError& error);
    TStreamingFeedback GetFeedback() const;

    DEFINE_SIGNAL(void(), Aborted);

private:
    const TClosure ReadCallback_;
    const IInvokerPtr CompressionInvoker_;
    const std::optional<TDuration> Timeout_;

    struct TWindowPacket
    {
        TStreamingPayload Payload;
        std::vector<TSharedRef> DecompressedAttachments;
    };

    struct TQueueEntry
    {
        TSharedRef Attachment;
        size_t CompressedSize;
    };

    TSpinLock Lock_;
    TSlidingWindow<TWindowPacket> Window_;
    TRingQueue<TQueueEntry> Queue_;
    TError Error_;
    TPromise<TSharedRef> Promise_;
    NConcurrency::TDelayedExecutorCookie TimeoutCookie_;

    std::atomic<ssize_t> ReadPosition_ = {0};
    bool Closed_ = false;

    void DoEnqueuePayload(
        const TStreamingPayload& payload,
        const std::vector<TSharedRef>& decompressedAttachments);
    void DoAbort(
        TGuard<TSpinLock>& guard,
        const TError& error);
    void OnTimeout();
};

DEFINE_REFCOUNTED_TYPE(TAttachmentsInputStream)

////////////////////////////////////////////////////////////////////////////////

class TAttachmentsOutputStream
    : public NConcurrency::IAsyncZeroCopyOutputStream
{
public:
    TAttachmentsOutputStream(
        EMemoryZone memoryZone,
        NCompression::ECodec codec,
        IInvokerPtr compressionInvoker,
        TClosure pullCallback,
        ssize_t windowSize,
        std::optional<TDuration> timeout = {});

    virtual TFuture<void> Write(const TSharedRef& data) override;
    virtual TFuture<void> Close() override;

    void Abort(const TError& error);
    void AbortUnlessClosed(const TError& error);
    void HandleFeedback(const TStreamingFeedback& feedback);
    std::optional<TStreamingPayload> TryPull();

    DEFINE_SIGNAL(void(), Aborted);

private:
    const EMemoryZone MemoryZone_;
    const NCompression::ECodec Codec_;
    const IInvokerPtr CompressionInvoker_;
    const TClosure PullCallback_;
    const ssize_t WindowSize_;
    const std::optional<TDuration> Timeout_;

    struct TWindowPacket
    {
        TSharedRef Data;
        TPromise<void> Promise;
        NConcurrency::TDelayedExecutorCookie TimeoutCookie;
    };

    struct TConfirmationEntry
    {
        ssize_t Position;
        TPromise<void> Promise;
        NConcurrency::TDelayedExecutorCookie TimeoutCookie;
    };

    TSpinLock Lock_;
    std::atomic<size_t> CompressionSequenceNumber_ = {0};
    TSlidingWindow<TWindowPacket> Window_;
    TError Error_;
    TRingQueue<TSharedRef> DataQueue_;
    TRingQueue<TConfirmationEntry> ConfirmationQueue_;
    TPromise<void> ClosePromise_;
    NConcurrency::TDelayedExecutorCookie CloseTimeoutCookie_;
    bool Closed_ = false;
    ssize_t WritePosition_ = 0;
    ssize_t SentPosition_ = 0;
    ssize_t ReadPosition_ = 0;
    int PayloadSequenceNumber_ = 0;

    void OnWindowPacketReady(TWindowPacket&& packet, TGuard<TSpinLock>& guard);
    void MaybeInvokePullCallback(TGuard<TSpinLock>& guard);
    bool CanPullMore(bool first) const;
    void DoAbort(TGuard<TSpinLock>& guard, const TError& error);
    void OnTimeout();
};

DEFINE_REFCOUNTED_TYPE(TAttachmentsOutputStream)

////////////////////////////////////////////////////////////////////////////////

//! Describes whether the client output stream needs server feedback to mark
//! writes as successful and whether that feedback can be negative.
DEFINE_ENUM(EWriterFeedbackStrategy,
    (NoFeedback)
    (OnlyPositive)
);

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

class TRpcInputStreamAdapter
    : public NConcurrency::IAsyncZeroCopyInputStream
{
public:
    TRpcInputStreamAdapter(
        IClientRequestPtr request,
        TFuture<void> invokeResult,
        TSharedRef firstReadResult);

    virtual TFuture<TSharedRef> Read() override;

    ~TRpcInputStreamAdapter();

private:
    const IClientRequestPtr Request_;

    NConcurrency::IAsyncZeroCopyInputStreamPtr Underlying_;
    TFuture<void> InvokeResult_;
    std::atomic<bool> FirstRead_ = {true};
    TSharedRef FirstReadResult_;
};

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EWriterFeedback,
    (Handshake)
    (Success)
);

void CheckWriterFeedback(
    const TSharedRef& ref,
    EWriterFeedback expectedFeedback);

////////////////////////////////////////////////////////////////////////////////

class TRpcOutputStreamAdapter
    : public NConcurrency::IAsyncZeroCopyOutputStream
{
public:
    TRpcOutputStreamAdapter(
        IClientRequestPtr request,
        TFuture<void> invokeResult,
        EWriterFeedbackStrategy feedbackStrategy = EWriterFeedbackStrategy::NoFeedback);

    virtual TFuture<void> Write(const TSharedRef& data) override;
    virtual TFuture<void> Close() override;

private:
    const IClientRequestPtr Request_;

    NConcurrency::IAsyncZeroCopyOutputStreamPtr Underlying_;
    TFuture<void> InvokeResult_;

    NConcurrency::IAsyncZeroCopyInputStreamPtr FeedbackStream_;
    EWriterFeedbackStrategy FeedbackStrategy_;

    TSpinLock QueueLock_;
    TRingQueue<TPromise<void>> ConfirmationQueue_;
    TError Error_;

    void AbortOnError(const TError& error);
    void OnFeedback(const TErrorOr<TSharedRef>& refOrError);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

//! Creates an input stream adapter from an uninvoked client request.
template <class TRequestMessage, class TResponse>
TFuture<NConcurrency::IAsyncZeroCopyInputStreamPtr> CreateInputStreamAdapter(
    TIntrusivePtr<TTypedClientRequest<TRequestMessage, TResponse>> request);

//! Creates an output stream adapter from an uninvoked client request.
template <class TRequestMessage, class TResponse>
TFuture<NConcurrency::IAsyncZeroCopyOutputStreamPtr> CreateOutputStreamAdapter(
    TIntrusivePtr<TTypedClientRequest<TRequestMessage, TResponse>> request,
    EWriterFeedbackStrategy feedbackStrategy = EWriterFeedbackStrategy::NoFeedback);

////////////////////////////////////////////////////////////////////////////////

//! Handles an incoming streaming request that uses the #CreateInputStreamAdapter
//! function.
void HandleInputStreamingRequest(
    const IServiceContextPtr& context,
    const TCallback<TFuture<TSharedRef>()>& blockGenerator);

void HandleInputStreamingRequest(
    const IServiceContextPtr& context,
    const NConcurrency::IAsyncZeroCopyInputStreamPtr& input);

//! Handles an incoming streaming request that uses the #CreateOutputStreamAdapter
//! function with the same #feedbackStrategy.
void HandleOutputStreamingRequest(
    const IServiceContextPtr& context,
    const TCallback<TFuture<void>(TSharedRef)>& blockHandler,
    const TCallback<TFuture<void>()>& finalizer,
    EWriterFeedbackStrategy feedbackStrategy = EWriterFeedbackStrategy::NoFeedback);

void HandleOutputStreamingRequest(
    const IServiceContextPtr& context,
    const NConcurrency::IAsyncZeroCopyOutputStreamPtr& output,
    EWriterFeedbackStrategy feedbackStrategy = EWriterFeedbackStrategy::NoFeedback);

/////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc

#define STREAM_INL_H_
#include "stream-inl.h"
#undef STREAM_INL_H_

