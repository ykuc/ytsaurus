#pragma once

#include "public.h"

#include <ytlib/table_client/public.h>

#include <ytlib/formats/format.h>

#include <core/yson/public.h>

namespace NYT {
namespace NVersionedTableClient {

//////////////////////////////////////////////////////////////////////////////////

class TTableOutput
    : public TOutputStream
{
public:
    TTableOutput(const NFormats::TFormat& format, NYson::IYsonConsumer* consumer);
    ~TTableOutput() throw();

private:
    void DoWrite(const void* buf, size_t len);
    void DoFinish();


    const std::unique_ptr<NFormats::IParser> Parser_;

    bool IsParserValid_ = true;

};

//////////////////////////////////////////////////////////////////////////////////

void PipeReaderToWriter(
    ISchemalessReaderPtr reader,
    ISchemalessWriterPtr writer,
    int bufferRowCount);

void PipeInputToOutput(
    TInputStream* input,
    TOutputStream* output,
    i64 bufferBlockSize);

//////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
} // namespace NVersionedTableClient
