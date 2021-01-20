#include "rich.h"

#include "parser_detail.h"

#include <yt/client/chunk_client/read_limit.h>

#include <yt/client/table_client/schema.h>
#include <yt/client/table_client/column_rename_descriptor.h>

#include <yt/core/misc/error.h>

#include <yt/core/yson/consumer.h>

#include <yt/core/ytree/fluent.h>

namespace NYT::NYPath {

using namespace NYTree;
using namespace NYson;
using namespace NChunkClient;
using namespace NTableClient;
using namespace NSecurityClient;

////////////////////////////////////////////////////////////////////////////////

TRichYPath::TRichYPath()
{ }

TRichYPath::TRichYPath(const TRichYPath& other)
    : Path_(other.Path_)
    , Attributes_(other.Attributes_ ? other.Attributes_->Clone() : nullptr)
{ }

TRichYPath::TRichYPath(const char* path)
    : Path_(path)
{ }

TRichYPath::TRichYPath(const TYPath& path)
    : Path_(path)
{ }

TRichYPath::TRichYPath(TRichYPath&& other)
    : Path_(std::move(other.Path_))
    , Attributes_(std::move(other.Attributes_))
{ }

TRichYPath::TRichYPath(const TYPath& path, const IAttributeDictionary& attributes)
    : Path_(path)
    , Attributes_(attributes.Clone())
{ }

const TYPath& TRichYPath::GetPath() const
{
    return Path_;
}

void TRichYPath::SetPath(const TYPath& path)
{
    Path_ = path;
}

const IAttributeDictionary& TRichYPath::Attributes() const
{
    return Attributes_ ? *Attributes_ : EmptyAttributes();
}

IAttributeDictionary& TRichYPath::Attributes()
{
    if (!Attributes_) {
        Attributes_ = CreateEphemeralAttributes();
    }
    return *Attributes_;
}

TRichYPath& TRichYPath::operator = (const TRichYPath& other)
{
    if (this != &other) {
        Path_ = other.Path_;
        Attributes_ = other.Attributes_ ? other.Attributes_->Clone() : nullptr;
    }
    return *this;
}

////////////////////////////////////////////////////////////////////////////////

bool operator== (const TRichYPath& lhs, const TRichYPath& rhs)
{
    return lhs.GetPath() == rhs.GetPath() && lhs.Attributes() == rhs.Attributes();
}

////////////////////////////////////////////////////////////////////////////////

namespace {

void AppendAttributes(TStringBuilderBase* builder, const IAttributeDictionary& attributes, EYsonFormat ysonFormat)
{
    TString attrString;
    TStringOutput output(attrString);
    TYsonWriter writer(&output, ysonFormat, EYsonType::MapFragment);

    BuildYsonAttributesFluently(&writer)
        .Items(attributes);

    if (!attrString.empty()) {
        builder->AppendChar(TokenTypeToChar(NYson::ETokenType::LeftAngle));
        builder->AppendString(attrString);
        builder->AppendChar(TokenTypeToChar(NYson::ETokenType::RightAngle));
    }
}

template <class TFunc>
auto RunAttributeAccessor(const TRichYPath& path, const TString& key, TFunc accessor) -> decltype(accessor())
{
    try {
        return accessor();
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error parsing attribute %Qv of rich YPath %v",
            key,
            path.GetPath()) << ex;
    }
}

template <class T>
T GetAttribute(const TRichYPath& path, const TString& key, const T& defaultValue)
{
    return RunAttributeAccessor(path, key, [&] () {
        return path.Attributes().Get(key, defaultValue);
    });
}

template <class T>
typename TOptionalTraits<T>::TOptional FindAttribute(const TRichYPath& path, const TString& key)
{
    return RunAttributeAccessor(path, key, [&] () {
        return path.Attributes().Find<T>(key);
    });
}

TYsonString FindAttributeYson(const TRichYPath& path, const TString& key)
{
    return RunAttributeAccessor(path, key, [&] () {
        return path.Attributes().FindYson(key);
    });
}

} // namespace

TRichYPath TRichYPath::Parse(const TString& str)
{
    return ParseRichYPathImpl(str);
}

TRichYPath TRichYPath::Normalize() const
{
    auto parsed = TRichYPath::Parse(Path_);
    parsed.Attributes().MergeFrom(Attributes());
    return parsed;
}

void TRichYPath::Save(TStreamSaveContext& context) const
{
    using NYT::Save;
    Save(context, Path_);
    Save(context, Attributes_);
}

void TRichYPath::Load(TStreamLoadContext& context)
{
    using NYT::Load;
    Load(context, Path_);
    Load(context, Attributes_);
}

bool TRichYPath::GetAppend(bool defaultValue) const
{
    return GetAttribute(*this, "append", defaultValue);
}

void TRichYPath::SetAppend(bool value)
{
    Attributes().Set("append", value);
}

bool TRichYPath::GetTeleport() const
{
    return GetAttribute(*this, "teleport", false);
}

bool TRichYPath::GetPrimary() const
{
    return GetAttribute(*this, "primary", false);
}

bool TRichYPath::GetForeign() const
{
    return GetAttribute(*this, "foreign", false);
}

void TRichYPath::SetForeign(bool value)
{
    Attributes().Set("foreign", value);
}

std::optional<std::vector<TString>> TRichYPath::GetColumns() const
{
    if (Attributes().Contains("channel")) {
        THROW_ERROR_EXCEPTION("Deprecated attribute \"channel\" in YPath");
    }
    return FindAttribute<std::vector<TString>>(*this, "columns");
}

void TRichYPath::SetColumns(const std::vector<TString>& columns)
{
    Attributes().Set("columns", columns);
}

std::vector<NChunkClient::TLegacyReadRange> TRichYPath::GetRanges() const
{
    // COMPAT(ignat): top-level "lower_limit" and "upper_limit" are processed for compatibility.
    auto optionalLowerLimit = FindAttribute<TLegacyReadLimit>(*this, "lower_limit");
    auto optionalUpperLimit = FindAttribute<TLegacyReadLimit>(*this, "upper_limit");
    auto optionalRanges = FindAttribute<std::vector<TLegacyReadRange>>(*this, "ranges");

    if (optionalLowerLimit || optionalUpperLimit) {
        if (optionalRanges) {
            THROW_ERROR_EXCEPTION("YPath cannot be annotated with both multiple (\"ranges\" attribute) "
                "and single (\"lower_limit\" or \"upper_limit\" attributes) ranges");
        }
        return std::vector<TLegacyReadRange>({
            TLegacyReadRange(
                optionalLowerLimit.value_or(TLegacyReadLimit()),
                optionalUpperLimit.value_or(TLegacyReadLimit())
            )});
    } else {
        return optionalRanges.value_or(std::vector<TLegacyReadRange>({TLegacyReadRange()}));
    }
}

void TRichYPath::SetRanges(const std::vector<NChunkClient::TLegacyReadRange>& value)
{
    Attributes().Set("ranges", value);
    // COMPAT(ignat)
    Attributes().Remove("lower_limit");
    Attributes().Remove("upper_limit");
}

bool TRichYPath::HasNontrivialRanges() const
{
    auto optionalLowerLimit = FindAttribute<TLegacyReadLimit>(*this, "lower_limit");
    auto optionalUpperLimit = FindAttribute<TLegacyReadLimit>(*this, "upper_limit");
    auto optionalRanges = FindAttribute<std::vector<TLegacyReadRange>>(*this, "ranges");

    return optionalLowerLimit || optionalUpperLimit || optionalRanges;
}

std::optional<TString> TRichYPath::GetFileName() const
{
    return FindAttribute<TString>(*this, "file_name");
}

std::optional<bool> TRichYPath::GetExecutable() const
{
    return FindAttribute<bool>(*this, "executable");
}

TYsonString TRichYPath::GetFormat() const
{
    return FindAttributeYson(*this, "format");
}

TTableSchemaPtr TRichYPath::GetSchema() const
{
    return RunAttributeAccessor(*this, "schema", [&] () {
        auto schema = FindAttribute<TTableSchemaPtr>(*this, "schema");
        if (schema) {
            ValidateTableSchema(*schema);
        }
        return schema;
    });
}

std::optional<TColumnRenameDescriptors> TRichYPath::GetColumnRenameDescriptors() const
{
    return FindAttribute<TColumnRenameDescriptors>(*this, "rename_columns");
}

TSortColumns TRichYPath::GetSortedBy() const
{
    return GetAttribute(*this, "sorted_by", TSortColumns());
}

void TRichYPath::SetSortedBy(const TSortColumns& value)
{
    if (value.empty()) {
        Attributes().Remove("sorted_by");
    } else {
        Attributes().Set("sorted_by", value);
    }
}

std::optional<i64> TRichYPath::GetRowCountLimit() const
{
    return FindAttribute<i64>(*this, "row_count_limit");
}

std::optional<NTransactionClient::TTimestamp> TRichYPath::GetTimestamp() const
{
    return FindAttribute<NTransactionClient::TTimestamp>(*this, "timestamp");
}

std::optional<NTransactionClient::TTimestamp> TRichYPath::GetRetentionTimestamp() const
{
    return FindAttribute<NTransactionClient::TTimestamp>(*this, "retention_timestamp");
}

std::optional<NTableClient::EOptimizeFor> TRichYPath::GetOptimizeFor() const
{
    return FindAttribute<NTableClient::EOptimizeFor>(*this, "optimize_for");
}

std::optional<NCompression::ECodec> TRichYPath::GetCompressionCodec() const
{
    return FindAttribute<NCompression::ECodec>(*this, "compression_codec");
}

std::optional<NErasure::ECodec> TRichYPath::GetErasureCodec() const
{
    return FindAttribute<NErasure::ECodec>(*this, "erasure_codec");
}

bool TRichYPath::GetAutoMerge() const
{
    return GetAttribute<bool>(*this, "auto_merge", true);
}

std::optional<NObjectClient::TTransactionId> TRichYPath::GetTransactionId() const
{
    return FindAttribute<NObjectClient::TTransactionId>(*this, "transaction_id");
}

std::optional<std::vector<TSecurityTag>> TRichYPath::GetSecurityTags() const
{
    return FindAttribute<std::vector<TSecurityTag>>(*this, "security_tags");
}

bool TRichYPath::GetBypassArtifactCache() const
{
    return GetAttribute<bool>(*this, "bypass_artifact_cache", false);
}

ETableSchemaModification TRichYPath::GetSchemaModification() const
{
    return GetAttribute<ETableSchemaModification>(
        *this,
        "schema_modification",
        ETableSchemaModification::None);
}

bool TRichYPath::GetPartiallySorted() const
{
    return GetAttribute<bool>(*this, "partially_sorted", false);
}

std::optional<int> TRichYPath::GetChunkKeyColumnCount() const
{
    return FindAttribute<int>(*this, "chunk_key_column_count");
}

std::optional<bool> TRichYPath::GetChunkUniqueKeys() const
{
    return FindAttribute<bool>(*this, "chunk_unique_keys");
}

std::optional<bool> TRichYPath::GetCopyFile() const
{
    return FindAttribute<bool>(*this, "copy_file");
}

////////////////////////////////////////////////////////////////////////////////

TString ConvertToString(const TRichYPath& path, EYsonFormat ysonFormat)
{
    const IAttributeDictionary* attributes = nullptr;
    IAttributeDictionaryPtr attrHolder;
    auto columns = path.GetColumns();

    if (columns) {
        attrHolder = path.Attributes().Clone();
        attrHolder->Remove("columns");
        attributes = attrHolder.Get();
    } else {
        attributes = &path.Attributes();
    }

    TStringBuilder builder;

    AppendAttributes(&builder, *attributes, ysonFormat);
    builder.AppendString(path.GetPath());
    if (columns) {
        builder.AppendChar('{');
        JoinToString(
            &builder,
            columns->begin(),
            columns->end(),
            TDefaultFormatter(),
            ",");
        builder.AppendChar('}');
    }

    return builder.Flush();
}

TString ToString(const TRichYPath& path)
{
    // NB: we intentionally use Text format since string-representation of rich ypath should be readable.
    return ConvertToString(path, EYsonFormat::Text);
}

std::vector<TRichYPath> Normalize(const std::vector<TRichYPath>& paths)
{
    std::vector<TRichYPath> result;
    for (const auto& path : paths) {
        result.push_back(path.Normalize());
    }
    return result;
}

void Serialize(const TRichYPath& richPath, IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer)
        .BeginAttributes()
            .Items(richPath.Attributes())
        .EndAttributes()
        .Value(richPath.GetPath());
}

void Deserialize(TRichYPath& richPath, INodePtr node)
{
    if (node->GetType() != ENodeType::String) {
        THROW_ERROR_EXCEPTION("YPath can only be parsed from %Qlv but got %Qlv",
            ENodeType::String,
            node->GetType());
    }
    richPath.SetPath(node->GetValue<TString>());
    richPath.Attributes().Clear();
    richPath.Attributes().MergeFrom(node->Attributes());
}

void ToProto(TString* protoPath, const TRichYPath& path)
{
    *protoPath = ConvertToString(path, EYsonFormat::Binary);
}

void FromProto(TRichYPath* path, const TString& protoPath)
{
    *path = TRichYPath::Parse(protoPath);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYPath
