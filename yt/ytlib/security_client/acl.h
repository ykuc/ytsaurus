#pragma once

#include "public.h"

#include <yt/core/yson/consumer.h>

#include <yt/core/ytree/permission.h>

#include <vector>

namespace NYT::NSecurityClient {

////////////////////////////////////////////////////////////////////////////////

struct TSerializableAccessControlEntry
{
    ESecurityAction Action = ESecurityAction::Undefined;
    std::vector<TString> Subjects;
    NYTree::EPermissionSet Permissions;
    EAceInheritanceMode InheritanceMode = EAceInheritanceMode::ObjectAndDescendants;

    TSerializableAccessControlEntry(
        ESecurityAction action,
        std::vector<TString> subjects,
        NYTree::EPermissionSet permissions,
        EAceInheritanceMode inheritanceMode = EAceInheritanceMode::ObjectAndDescendants);

    // Use only for deserialization.
    TSerializableAccessControlEntry();

    void Persist(const TStreamPersistenceContext& context);
};

bool operator == (const TSerializableAccessControlEntry& lhs, const TSerializableAccessControlEntry& rhs);
bool operator != (const TSerializableAccessControlEntry& lhs, const TSerializableAccessControlEntry& rhs);

void Serialize(const TSerializableAccessControlEntry& acl, NYson::IYsonConsumer* consumer);
void Deserialize(TSerializableAccessControlEntry& acl, NYTree::INodePtr node);

struct TSerializableAccessControlList
{
    std::vector<TSerializableAccessControlEntry> Entries;

    void Persist(const TStreamPersistenceContext& context);
};

bool operator == (const TSerializableAccessControlList& lhs, const TSerializableAccessControlList& rhs);
bool operator != (const TSerializableAccessControlList& lhs, const TSerializableAccessControlList& rhs);

void Serialize(const TSerializableAccessControlList& acl, NYson::IYsonConsumer* consumer);
void Deserialize(TSerializableAccessControlList& acl, NYTree::INodePtr node);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSecurityClient
