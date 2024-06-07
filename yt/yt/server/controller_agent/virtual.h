#pragma once

#include "private.h"

#include <yt/yt/ytlib/table_client/public.h>

#include <yt/yt/ytlib/object_client/proto/object_ypath.pb.h>

#include <yt/yt/ytlib/chunk_client/public.h>

#include <yt/yt/ytlib/chunk_client/proto/chunk_owner_ypath.pb.h>

#include <yt/yt/ytlib/node_tracker_client/public.h>

#include <yt/yt/core/ytree/virtual.h>

#include <yt/yt/core/ypath/public.h>

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

class TVirtualStaticTable
    : public NYTree::TSupportsAttributes
    , public NYTree::ISystemAttributeProvider
{
public:
    TVirtualStaticTable(
        const THashSet<NChunkClient::TInputChunkPtr>& chunks,
        NTableClient::TTableSchemaPtr schema,
        NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
        TOperationId operationId,
        TString name,
        NYTree::TYPath path);

    bool DoInvoke(const NYTree::IYPathServiceContextPtr& context) override;

    // TSupportsAttributes overrides
    ISystemAttributeProvider* GetBuiltinAttributeProvider() override;

    // ISystemAttributeProvider overrides
    void ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors) override;
    const THashSet<NYTree::TInternedAttributeKey>& GetBuiltinAttributeKeys() override;
    bool GetBuiltinAttribute(NYTree::TInternedAttributeKey key, NYson::IYsonConsumer* consumer) override;
    TFuture<NYson::TYsonString> GetBuiltinAttributeAsync(NYTree::TInternedAttributeKey key) override;
    bool SetBuiltinAttribute(NYTree::TInternedAttributeKey key, const NYson::TYsonString& value, bool force) override;
    bool RemoveBuiltinAttribute(NYTree::TInternedAttributeKey key) override;

    void GetSelf(TReqGet* request, TRspGet* response, const TCtxGetPtr& context) override;

    void DoWriteAttributesFragment(
        NYT::NYson::IAsyncYsonConsumer* consumer,
        const NYTree::TAttributeFilter& attributeFilter,
        bool stable) override;

private:
    NYTree::TSystemBuiltinAttributeKeysCache BuiltinAttributeKeysCache_;

    const THashSet<NChunkClient::TInputChunkPtr>& Chunks_;

    NTableClient::TTableSchemaPtr Schema_;

    NNodeTrackerClient::TNodeDirectoryPtr NodeDirectory_;

    TOperationId OperationId_;
    TString Name_;
    NYTree::TYPath Path_;

    DECLARE_YPATH_SERVICE_METHOD(NObjectClient::NProto, GetBasicAttributes);
    DECLARE_YPATH_SERVICE_METHOD(NChunkClient::NProto, Fetch);
    DECLARE_YPATH_SERVICE_METHOD(NObjectClient::NProto, CheckPermission);
};

DEFINE_REFCOUNTED_TYPE(TVirtualStaticTable)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableServer
