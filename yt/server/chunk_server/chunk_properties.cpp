#include "chunk_properties.h"

#include "chunk_manager.h"
#include "medium.h"

#include <yt/server/cell_master/automaton.h>
#include <yt/core/misc/serialize.h>

namespace NYT {
namespace NChunkServer {

using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

TMediumChunkProperties& TMediumChunkProperties::operator|=(const TMediumChunkProperties& rhs)
{
    if (this == &rhs)
        return *this;

    SetReplicationFactor(std::max(GetReplicationFactor(), rhs.GetReplicationFactor()));
    SetDataPartsOnly(GetDataPartsOnly() && rhs.GetDataPartsOnly());

    return *this;
}

void TMediumChunkProperties::Save(NCellMaster::TSaveContext& context) const
{
    using NYT::Save;
    Save(context, ReplicationFactor_);
    Save(context, DataPartsOnly_);
}

void TMediumChunkProperties::Load(NCellMaster::TLoadContext& context)
{
    using NYT::Load;
    ReplicationFactor_ = Load<decltype(ReplicationFactor_)>(context);
    DataPartsOnly_ = Load<decltype(DataPartsOnly_)>(context);
}

Stroka ToString(const TMediumChunkProperties& properties)
{
    return Format("{ReplicationFactor: %v, DataPartsOnly: %v}",
        properties.GetReplicationFactor(),
        properties.GetDataPartsOnly());
}

////////////////////////////////////////////////////////////////////////////////

void TChunkProperties::Save(NCellMaster::TSaveContext& context) const
{
    using NYT::Save;
    Save(context, MediumChunkProperties_);
    Save(context, Vital_);
}

void TChunkProperties::Load(NCellMaster::TLoadContext& context)
{
    using NYT::Load;
    Load(context, MediumChunkProperties_);
    Load(context, Vital_);
}

TChunkProperties& TChunkProperties::operator|=(const TChunkProperties& rhs)
{
    if (this == &rhs) {
        return *this;
    }

    SetVital(GetVital() || rhs.GetVital());

    for (int i = 0; i < MaxMediumCount; ++i) {
        (*this)[i] |= rhs[i];
    }

    return *this;
}

bool TChunkProperties::IsValid() const
{
    for (const auto& mediumProps : MediumChunkProperties_) {
        if (mediumProps && !mediumProps.GetDataPartsOnly()) {
            // At least one medium has complete data.
            return true;
        }
    }

    return false;
}

Stroka ToString(const TChunkProperties& properties)
{
    TStringBuilder builder;
    builder.AppendFormat("{Vital: %v, Media: {", properties.GetVital());

    int mediumIndex = 0;
    JoinToString(&builder, properties.begin(), properties.end(),
        [&] (TStringBuilder* builderPtr, const TMediumChunkProperties& mediumProps) {
            if (mediumProps) {
                builderPtr->AppendFormat("%v: %v", mediumIndex, mediumProps);
            }
            ++mediumIndex;
            return (bool)mediumProps;
        });

    builder.AppendString("}}");

    return builder.Flush();
}

////////////////////////////////////////////////////////////////////////////////

TSerializableChunkProperties::TSerializableChunkProperties(
    const TChunkProperties& properties,
    const TChunkManagerPtr& chunkManager)
{
    int mediumIndex = 0;
    for (const auto& mediumProps : properties) {
        if (mediumProps) {
            auto* medium = chunkManager->GetMediumByIndexOrThrow(mediumIndex);

            TMediumProperties resultMediumProps;
            resultMediumProps.ReplicationFactor = mediumProps.GetReplicationFactor();
            resultMediumProps.DataPartsOnly = mediumProps.GetDataPartsOnly();

            Y_ASSERT(resultMediumProps.ReplicationFactor != 0);

            MediumProperties_.emplace(medium->GetName(), resultMediumProps);
        }
        ++mediumIndex;
    }
}

void TSerializableChunkProperties::ToChunkProperties(
    TChunkProperties* properties,
    const TChunkManagerPtr& chunkManager)
{
    for (auto& mediumProperties : *properties) {
        mediumProperties.Clear();
    }

    for (const auto& pair : MediumProperties_) {
        auto* medium = chunkManager->GetMediumByNameOrThrow(pair.first);
        auto mediumIndex = medium->GetIndex();
        auto& mediumProperties = (*properties)[mediumIndex];
        mediumProperties.SetReplicationFactorOrThrow(pair.second.ReplicationFactor);
        mediumProperties.SetDataPartsOnly(pair.second.DataPartsOnly);
    }
}

void TSerializableChunkProperties::Serialize(NYson::IYsonConsumer* consumer) const
{
    BuildYsonFluently(consumer)
        .Value(MediumProperties_);
}

void TSerializableChunkProperties::Deserialize(INodePtr node)
{
    YCHECK(node);

    MediumProperties_ = ConvertTo<std::map<Stroka, TMediumProperties>>(node);
}

void Serialize(const TSerializableChunkProperties& serializer, NYson::IYsonConsumer* consumer)
{
    serializer.Serialize(consumer);
}

void Deserialize(TSerializableChunkProperties& serializer, INodePtr node)
{
    serializer.Deserialize(node);
}

void Serialize(const TSerializableChunkProperties::TMediumProperties& properties, NYson::IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("replication_factor").Value(properties.ReplicationFactor)
            .Item("data_parts_only").Value(properties.DataPartsOnly)
        .EndMap();
}

void Deserialize(TSerializableChunkProperties::TMediumProperties& properties, INodePtr node)
{
    auto map = node->AsMap();
    properties.ReplicationFactor = map->GetChild("replication_factor")->AsInt64()->GetValue();
    properties.DataPartsOnly = map->GetChild("data_parts_only")->AsBoolean()->GetValue();
}

////////////////////////////////////////////////////////////////////////////////

void ValidateReplicationFactor(int replicationFactor)
{
    if (replicationFactor != 0 && // Zero is a special - and permitted - case.
        (replicationFactor < NChunkClient::MinReplicationFactor ||
         replicationFactor > NChunkClient::MaxReplicationFactor))
    {
        THROW_ERROR_EXCEPTION("Replication factor %v is out of range [%v,%v]",
            replicationFactor,
            NChunkClient::MinReplicationFactor,
            NChunkClient::MaxReplicationFactor);
    }
}

void ValidateChunkProperties(
    const TChunkManagerPtr& chunkManager,
    const TChunkProperties& properties,
    int primaryMediumIndex)
{
    if (!properties.IsValid()) {
        THROW_ERROR_EXCEPTION(
            "At least one medium should store replicas (including parity parts); "
                "configuring otherwise would result in a data loss");
    }

    const auto* medium = chunkManager->GetMediumByIndexOrThrow(primaryMediumIndex);
    const auto& primaryMediumProperties = properties[primaryMediumIndex];
    if (primaryMediumProperties.GetReplicationFactor() == 0) {
        THROW_ERROR_EXCEPTION("Medium %Qv stores no chunk replicas and cannot be made primary",
            medium->GetName());
    }
    if (primaryMediumProperties.GetDataPartsOnly()) {
        THROW_ERROR_EXCEPTION("Medium %Qv stores no parity parts and cannot be made primary",
            medium->GetName());
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
