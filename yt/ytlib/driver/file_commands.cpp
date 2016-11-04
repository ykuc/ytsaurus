#include "file_commands.h"
#include "config.h"
#include "driver.h"

#include <yt/ytlib/api/file_reader.h>
#include <yt/ytlib/api/file_writer.h>

#include <yt/ytlib/chunk_client/chunk_spec.h>

#include <yt/core/concurrency/scheduler.h>

namespace NYT {
namespace NDriver {

using namespace NApi;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

TReadFileCommand::TReadFileCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("offset", Options.Offset)
        .Optional();
    RegisterParameter("length", Options.Length)
        .Optional();
    RegisterParameter("file_reader", FileReader)
        .Default(nullptr);

}

void TReadFileCommand::DoExecute(ICommandContextPtr context)
{
    Options.Config = UpdateYsonSerializable(
        context->GetConfig()->FileReader,
        FileReader);

    auto reader = context->GetClient()->CreateFileReader(
        Path.GetPath(),
        Options);

    WaitFor(reader->Open())
        .ThrowOnError();

    auto output = context->Request().OutputStream;

    while (true) {
        auto block = WaitFor(reader->Read())
            .ValueOrThrow();

        if (!block)
            break;

        WaitFor(output->Write(block))
            .ThrowOnError();
    }
}

//////////////////////////////////////////////////////////////////////////////////

TWriteFileCommand::TWriteFileCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("file_writer", FileWriter)
        .Default();
}

void TWriteFileCommand::DoExecute(ICommandContextPtr context)
{
    Options.Config = UpdateYsonSerializable(
        context->GetConfig()->FileWriter,
        FileWriter);
    Options.Append = Path.GetAppend();

    auto writer = context->GetClient()->CreateFileWriter(
        Path.GetPath(),
        Options);

    WaitFor(writer->Open())
        .ThrowOnError();

    struct TWriteBufferTag { };

    auto buffer = TSharedMutableRef::Allocate<TWriteBufferTag>(context->GetConfig()->WriteBufferSize, false);

    auto input = context->Request().InputStream;

    while (true) {
        auto bytesRead = WaitFor(input->Read(buffer))
            .ValueOrThrow();

        if (bytesRead == 0)
            break;

        WaitFor(writer->Write(buffer.Slice(0, bytesRead)))
            .ThrowOnError();
    }

    WaitFor(writer->Close())
        .ThrowOnError();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT
