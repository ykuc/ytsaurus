#include "retryless_writer.h"

#include <mapreduce/yt/common/log.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

TRetrylessWriter::~TRetrylessWriter()
{
    NDetail::FinishOrDie(this, "TRetrylessWriter");
}

void TRetrylessWriter::DoFinish()
{
    if (!Running_) {
        return;
    }
    Running_ = false;

    BufferedOutput_->Finish();
    Request_.FinishRequest();
    Request_.GetResponse();
}

void TRetrylessWriter::DoWrite(const void* buf, size_t len)
{
    try {
        BufferedOutput_->Write(buf, len);
    } catch (...) {
        Running_ = false;
        throw;
    }
}

void TRetrylessWriter::NotifyRowEnd()
{ }

void TRetrylessWriter::Abort()
{
    Running_ = false;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
