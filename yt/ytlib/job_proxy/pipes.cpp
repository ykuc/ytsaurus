﻿#include "stdafx.h"
#include "pipes.h"

#include <ytlib/ytree/yson_reader.h>
#include <ytlib/table_client/yson_row_consumer.h>

#include <util/system/file.h>

#ifdef _linux_

#include <unistd.h>
#include <fcntl.h>

#include <sys/epoll.h>

#endif

namespace NYT {
namespace NJobProxy {

using namespace NYTree;
using namespace NTableClient;

////////////////////////////////////////////////////////////////////

static auto& Logger = JobProxyLogger;

////////////////////////////////////////////////////////////////////

#ifdef _linux_

int SafeDup(int oldFd)
{
    auto fd = dup(oldFd);

    // ToDo: provide proper message.
    if (fd == -1)
        ythrow yexception() << "dup failed with errno: " << errno;
}

void SafeDup2(int oldFd, int newFd)
{
    auto res = dup2(oldFd, newFd);

    // ToDo: provide proper message.
    if (res == -1)
        ythrow yexception() << "dup2 failed with errno: " << errno;
}

void SafeClose(int fd)
{
    auto res = close(fd);

    if (res == -1) 
        // ToDo: provide proper message.
        ythrow yexception() << "close failed with errno: " << errno;
}

int SafePipe(int fd[2])
{
    auto res = pipe(fd);

    // ToDo: provide proper message.
    if (res == -1)
        ythrow yexception() << "pipe failed with errno: " << errno;
}

void SafeMakeNonblocking(int fd)
{
    auto res = fcntl(fd, F_GETFL);

    if (res == -1)
        ythrow yexception() << Sprintf(
            "fcntl failed to get descriptor flags (fd: %d, errno: %d)",
            fd, 
            errno);

    res = fcntl(fd, F_SETFL, res | O_NONBLOCK);

    if (res == -1)
        ythrow yexception() << Sprintf(
            "fcntl failed to set descriptor to nonblocking mode (fd: %d, errno %d)",
            fd, 
            errno);
}

#elif defined _win_

// Streaming jobs are not supposed to work on windows for now.

int SafeDup(int oldFd)
{
    YUNIMPLEMENTED();
}

void SafeDup2(int oldFd, int newFd)
{
    YUNIMPLEMENTED();
}

void SafeClose(int fd)
{
    YUNIMPLEMENTED();
}

int SafePipe(int fd[2])
{
    YUNIMPLEMENTED();
}

void SafeMakeNonblocking(int fd)
{
    YUNIMPLEMENTED();
}

#endif

////////////////////////////////////////////////////////////////////

TErrorPipe::TErrorPipe(TOutputStream* output, int jobDescriptor /* = 2 */)
    : OutputStream(output)
    , JobDescriptor(jobDescriptor)
    , IsFinished(false)
{
    int fd[2];
    SafePipe(fd);
    Pipe = TPipe(fd);
}

void TErrorPipe::PrepareJobDescriptors()
{
    YASSERT(!IsFinished);

    SafeClose(Pipe.ReadFd);
    SafeDup2(Pipe.WriteFd, JobDescriptor);
    SafeClose(Pipe.WriteFd);
}

void TErrorPipe::PrepareProxyDescriptors()
{
    YASSERT(!IsFinished);

    SafeClose(Pipe.WriteFd);
    SafeMakeNonblocking(Pipe.ReadFd);
}

int TErrorPipe::GetEpollDescriptor() const 
{
    YASSERT(!IsFinished);

    return Pipe.ReadFd;
}

int TErrorPipe::GetEpollFlags() const
{
    YASSERT(!IsFinished);

#ifdef _linux_
    return EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
#else
    YUNIMPLEMENTED();
#endif
}

bool TErrorPipe::ProcessData(ui32 epollEvent)
{
    YASSERT(!IsFinished);

    const int bufferSize = 4096;
    char buffer[bufferSize];
    int size;

    for ( ; ; ) {
        size = ::read(Pipe.ReadFd, buffer, bufferSize);

        LOG_TRACE("Read %d bytes from error pipe (JobDescriptor: %d)", size, JobDescriptor);

        if (size > 0) {
            OutputStream->Write(buffer, static_cast<size_t>(size));
            if (size == bufferSize) { // it's marginal case
                // try to read again: is more bytes present in pipe?
                // Another way would be to restore this descriptor in epoll
                // and return back to 'read' after epoll's signal
                // (this descriptor in 'event triggered' mode, so restore
                // in epoll indeed required)
                continue;
            }
            return true;
        } else if (size == 0) {
            errno = 0;
            ::close(Pipe.ReadFd);

            LOG_TRACE("Error pipe closed (JobDescriptor: %d)", JobDescriptor);

            return false;
        } else { // size < 0
            switch (errno) {
                case EAGAIN:
                    errno = 0; // this is NONBLOCK socket, nothing read; return
                    return true;
                case EINTR:
                    // retry
                    break;
                default:
                    ::close(Pipe.ReadFd);

                    LOG_TRACE("Error pipe closed (JobDescriptor: %d)", JobDescriptor);

                    return false;
            }
        }
    }

    return true;
}

void TErrorPipe::Finish()
{
    if (!IsFinished) {
        OutputStream->Finish();
        IsFinished = true;
    }
}

////////////////////////////////////////////////////////////////////

TInputPipe::TInputPipe(TInputStream* input, int jobDescriptor)
    : InputStream(input)
    , JobDescriptor(jobDescriptor)
    , Length(0)
    , Position(0)
    , IsFinished(false)
{
    int fd[2];
    SafePipe(fd);
    Pipe = TPipe(fd);
}

void TInputPipe::PrepareJobDescriptors()
{
    YASSERT(!IsFinished);

    SafeClose(Pipe.WriteFd);
    SafeDup2(Pipe.ReadFd, JobDescriptor);
    SafeClose(Pipe.ReadFd);
}

void TInputPipe::PrepareProxyDescriptors()
{
    YASSERT(!IsFinished);

    SafeMakeNonblocking(Pipe.WriteFd);
}

int TInputPipe::GetEpollDescriptor() const
{
    YASSERT(!IsFinished);

    return Pipe.WriteFd;
}

int TInputPipe::GetEpollFlags() const
{
    YASSERT(!IsFinished);

#ifdef _linux_
    return EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
#else
    YUNIMPLEMENTED();
#endif
}

bool TInputPipe::ProcessData(ui32 epollEvents)
{
    YASSERT(!IsFinished);

    while (HasData) {
        if (Position < Length) {
            auto res = ::write(Pipe.WriteFd, Buffer + Position, Length - Position);

            LOG_TRACE("Written %ld bytes to input pipe (JobDescriptor: %d)", res, JobDescriptor);

            if (res > 0)
                Position += res;
            else if (res < 0) {
                if (errno == EAGAIN) {
                    // Pipe blocked, pause writing.
                    return true;
                } else {
                    // Error with pipe.
                    ythrow yexception() << 
                        Sprintf("Writing to pipe failed (fd: %d, job fd: %d, errno: %d).",
                            Pipe.WriteFd,
                            JobDescriptor,
                            errno);
                }
            }
        } else {
            Position = 0;
            Length = InputStream->Read(Buffer, BufferSize);

            if (!(HasData = (Length > 0)))
                SafeClose(Pipe.WriteFd);
        }
    }

    LOG_TRACE("Input pipe finished writing (JobDescriptor: %d)", JobDescriptor);

    return true;
}

void TInputPipe::Finish()
{
    if (HasData) {
        ythrow yexception() << 
            Sprintf("Not all data was consumed by job (fd: %d, job fd: %d)",
                Pipe.WriteFd,
                JobDescriptor);
    }

    // Try to read some data from the pipe.
    ssize_t res = read(Pipe.ReadFd, Buffer, BufferSize);
    if (res > 0) {
        ythrow yexception() << 
            Sprintf("Not all data was consumed by job (fd: %d, job fd: %d)",
            Pipe.WriteFd,
            JobDescriptor);
    }

    SafeClose(Pipe.ReadFd);
    IsFinished = true;
}

////////////////////////////////////////////////////////////////////

TOutputPipe::TOutputPipe(NTableClient::ISyncWriter* writer, int jobDescriptor)
    : Writer(writer)
    , OutputThread(ThreadFunc, (void*)this)
    , JobDescriptor(jobDescriptor)
{
    int fd[2];
    SafePipe(fd);
    ReadingPipe = TPipe(fd);
}

void TOutputPipe::PrepareJobDescriptors()
{
    SafeClose(ReadingPipe.ReadFd);
    SafeDup2(ReadingPipe.WriteFd, JobDescriptor);
    SafeClose(ReadingPipe.WriteFd);
}

void TOutputPipe::PrepareProxyDescriptors()
{
    SafeClose(ReadingPipe.WriteFd);

    int fd[2];
    SafePipe(fd);
    FinishPipe = TPipe(fd);

    OutputThread.Start();
    OutputThread.Detach();
}

int TOutputPipe::GetEpollDescriptor() const
{
    YASSERT(FinishPipe.ReadFd >= 0);
    return FinishPipe.ReadFd;
}

int TOutputPipe::GetEpollFlags() const
{
    YASSERT(FinishPipe.ReadFd >= 0);

#ifdef _linux_
    return EPOLLIN | EPOLLERR | EPOLLHUP;
#else
    YUNIMPLEMENTED();
#endif
}

void* TOutputPipe::ThreadFunc(void* param)
{
    TOutputPipe* outputPipe = (TOutputPipe*)param;
    outputPipe->ThreadMain();
    return NULL;
}

void TOutputPipe::ThreadMain()
{
    try {
        Writer->Open();

        TFile file(FHANDLE(ReadingPipe.ReadFd));
        TFileInput input(file);

        TAutoPtr<TRowConsumer> rowConsumer(new TRowConsumer(~Writer));
        TYsonFragmentReader reader(rowConsumer.Get(), &input);

        while (reader.HasNext()) {
            reader.ReadNext();
        }

        Writer->Close();

    } catch (yexception e) {
        ErrorString = e.what();
    }

    // ToDo(psushin): replace by event?
    // No matter what we write, this just indicates that we're done.
    ::write(FinishPipe.WriteFd, "Done", 4);
    ::close(FinishPipe.WriteFd);

    LOG_TRACE("Output pipe finished reading (JobDescriptor: %d)", JobDescriptor);
}

bool TOutputPipe::ProcessData(ui32 epollEvents)
{
    LOG_TRACE("Output pipe closed (JobDescriptor: %d)", JobDescriptor);
    SafeClose(FinishPipe.ReadFd);
    return false;
}

void TOutputPipe::Finish()
{
    if (!ErrorString.Empty()) {
        ythrow yexception() << ErrorString;
    }
}

////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
