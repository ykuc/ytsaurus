#pragma once

#include "public.h"
#include "client.h"

#include <core/misc/ref.h>
#include <core/misc/error.h>

#include <core/ypath/public.h>

namespace NYT {
namespace NApi {

///////////////////////////////////////////////////////////////////////////////

struct IFileReader
    : public virtual TRefCounted
{
    //! Opens the reader. No other method can be called prior to the success of this one.
    virtual TAsyncError Open() = 0;

    //! Reads another portion of file.
    virtual TFuture<TErrorOr<TSharedRef>> Read() = 0;
};

DEFINE_REFCOUNTED_TYPE(IFileReader)

IFileReaderPtr CreateFileReader(
    IClientPtr client,
    const NYPath::TYPath& path,
    const TFileReaderOptions& options = TFileReaderOptions(),
    TFileReaderConfigPtr config = TFileReaderConfigPtr());

///////////////////////////////////////////////////////////////////////////////

} // namespace NApi
} // namespace NYT

