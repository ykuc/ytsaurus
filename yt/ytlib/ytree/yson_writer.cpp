#include "stdafx.h"
#include "common.h"

#include "yson_writer.h"
#include "yson_format.h"

#include "../misc/serialize.h"

#include <util/string/escape.h>

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////
    
TYsonWriter::TYsonWriter(TOutputStream* stream, EFormat format)
    : Stream(stream)
    , IsFirstItem(false)
    , IsEmptyEntity(false)
    , Indent(0)
    , Format(format)
{ }

void TYsonWriter::WriteIndent()
{
    for (int i = 0; i < IndentSize * Indent; ++i) {
        Stream->Write(' ');
    }
}

void TYsonWriter::WriteStringScalar(const Stroka& value)
{
    if (Format == EFormat::Binary) {
        Stream->Write(StringMarker);
        WriteVarInt32(Stream, static_cast<i32>(value.length()));
        Stream->Write(value.begin(), value.length());
    } else {
        Stream->Write('"');
        Stream->Write(EscapeC(value));
        Stream->Write('"');
    }
}

void TYsonWriter::WriteMapItem(const Stroka& name)
{
    CollectionItem(ItemSeparator);
    WriteStringScalar(name);
    if (Format == EFormat::Pretty) {
        Stream->Write(' ');
    }
    Stream->Write(KeyValueSeparator);
    if (Format == EFormat::Pretty) {
        Stream->Write(' ');
    }
    IsFirstItem = false;
}

void TYsonWriter::BeginCollection(char openBracket)
{
    Stream->Write(openBracket);
    IsFirstItem = true;
}

void TYsonWriter::CollectionItem(char separator)
{
    if (IsFirstItem) {
        if (Format == EFormat::Pretty) {
            Stream->Write('\n');
            ++Indent;
        }
    } else {
        Stream->Write(separator);
        if (Format == EFormat::Pretty) {
            Stream->Write('\n');
        }
    }
    if (Format == EFormat::Pretty) {
        WriteIndent();
    }
    IsFirstItem = false;
}

void TYsonWriter::EndCollection(char closeBracket)
{
    if (Format == EFormat::Pretty && !IsFirstItem) {
        Stream->Write('\n');
        --Indent;
        WriteIndent();
    }
    Stream->Write(closeBracket);
    IsFirstItem = false;
}

void TYsonWriter::OnStringScalar(const Stroka& value, bool hasAttributes)
{
    WriteStringScalar(value);
    if (Format == EFormat::Pretty && hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnInt64Scalar(i64 value, bool hasAttributes)
{
    if (Format == EFormat::Binary) {
        Stream->Write(Int64Marker);
        WriteVarInt64(Stream, value);
    } else {
        Stream->Write(ToString(value));
    }
    if (Format == EFormat::Pretty && hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnDoubleScalar(double value, bool hasAttributes)
{
    if (Format == EFormat::Binary) {
        Stream->Write(DoubleMarker);
        Stream->Write(&value, sizeof(double));
    } else {
        Stream->Write(ToString(value));
    }
    if (Format == EFormat::Pretty && hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnEntity(bool hasAttributes)
{
    if (!hasAttributes) {
        Stream->Write(BeginAttributesSymbol);
        Stream->Write(EndAttributesSymbol);
    }
}

void TYsonWriter::OnBeginList()
{
    BeginCollection(BeginListSymbol);
}

void TYsonWriter::OnListItem()
{
    CollectionItem(ItemSeparator);
}

void TYsonWriter::OnEndList(bool hasAttributes)
{
    EndCollection(EndListSymbol);
    if (Format == EFormat::Pretty && hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnBeginMap()
{
    BeginCollection(BeginMapSymbol);
}

void TYsonWriter::OnMapItem(const Stroka& name)
{
    WriteMapItem(name);
}

void TYsonWriter::OnEndMap(bool hasAttributes)
{
    EndCollection(EndMapSymbol);
    if (Format == EFormat::Pretty && hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnBeginAttributes()
{
    BeginCollection(BeginAttributesSymbol);
}

void TYsonWriter::OnAttributesItem(const Stroka& name)
{
    WriteMapItem(name);
}

void TYsonWriter::OnEndAttributes()
{
    EndCollection(EndAttributesSymbol);
}

void TYsonWriter::OnRaw(const TYson& yson)
{
    Stream->Write(yson);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
