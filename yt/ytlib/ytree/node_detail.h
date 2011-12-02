#pragma once

#include "common.h"
#include "ytree.h"
#include "ypath_service.h"
#include "tree_builder.h"
#include "yson_reader.h"
#include "ypath_rpc.pb.h"
#include "ypath_detail.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

class TNodeBase
    : public virtual INode
    , public TYPathServiceBase
{
public:
    typedef TIntrusivePtr<TNodeBase> TPtr;

#define IMPLEMENT_AS_METHODS(name) \
    virtual TIntrusivePtr<I##name##Node> As##name() \
    { \
        ythrow yexception() << Sprintf("Invalid node type (Expected: %s, Actual: %s)", \
            #name, \
            ~GetType().ToString()); \
    } \
    \
    virtual TIntrusivePtr<const I##name##Node> As##name() const \
    { \
        ythrow yexception() << Sprintf("Invalid node type (Expected: %s, Actual: %s)", \
            #name, \
            ~GetType().ToString()); \
    }

    IMPLEMENT_AS_METHODS(Entity)
    IMPLEMENT_AS_METHODS(Composite)
    IMPLEMENT_AS_METHODS(String)
    IMPLEMENT_AS_METHODS(Int64)
    IMPLEMENT_AS_METHODS(Double)
    IMPLEMENT_AS_METHODS(List)
    IMPLEMENT_AS_METHODS(Map)
#undef IMPLEMENT_AS_METHODS

protected:
    template <class TNode>
    void DoSetSelf(TNode* node, const Stroka& value)
    {
        auto builder = CreateBuilderFromFactory(GetFactory());
        TStringInput stream(value);
        SetNodeFromProducer(node, ~TYsonReader::GetProducer(&stream), ~builder);
    }
    
    virtual void DoInvoke(NRpc::IServiceContext* context);
    virtual TResolveResult ResolveAttributes(TYPath path, const Stroka& verb);

    DECLARE_RPC_SERVICE_METHOD(NProto, Get);
    virtual void GetSelf(TReqGet* request, TRspGet* response, TCtxGet::TPtr context);
    virtual void GetRecursive(TYPath path, TReqGet* request, TRspGet* response, TCtxGet::TPtr context);

    DECLARE_RPC_SERVICE_METHOD(NProto, Set);
    virtual void SetSelf(TReqSet* request, TRspSet* response, TCtxSet::TPtr context);
    virtual void SetRecursive(TYPath path, TReqSet* request, TRspSet* response, TCtxSet::TPtr context);

    DECLARE_RPC_SERVICE_METHOD(NProto, Remove);
    virtual void RemoveSelf(TReqRemove* request, TRspRemove* response, TCtxRemove::TPtr context);
    virtual void RemoveRecursive(TYPath path, TReqRemove* request, TRspRemove* response, TCtxRemove::TPtr context);

    virtual yvector<Stroka> GetVirtualAttributeNames();
    virtual IYPathService::TPtr GetVirtualAttributeService(const Stroka& name);

};

////////////////////////////////////////////////////////////////////////////////

class TMapNodeMixin
    : public virtual IMapNode
{
protected:
    bool DoInvoke(NRpc::IServiceContext* context);
    IYPathService::TResolveResult ResolveRecursive(TYPath path, const Stroka& verb);
    void SetRecursive(TYPath path, NProto::TReqSet* request);
    void SetRecursive(TYPath path, INode* value);

private:
    DECLARE_RPC_SERVICE_METHOD(NProto, List);

};

////////////////////////////////////////////////////////////////////////////////

class TListNodeMixin
    : public virtual IListNode
{
protected:
    IYPathService::TResolveResult ResolveRecursive(TYPath path, const Stroka& verb);
    void SetRecursive(TYPath path, NProto::TReqSet* request);
    void SetRecursive(TYPath path, INode* value);

private:
    int ParseChildIndex(TStringBuf str);
    void CreateChild(int beforeIndex, TYPath path, INode* value);

};

////////////////////////////////////////////////////////////////////////////////

#define YTREE_NODE_TYPE_OVERRIDES(name) \
public: \
    virtual ::NYT::NYTree::ENodeType GetType() const \
    { \
        return ::NYT::NYTree::ENodeType::name; \
    } \
    \
    virtual TIntrusivePtr<const ::NYT::NYTree::I##name##Node> As##name() const \
    { \
        return this; \
    } \
    \
    virtual TIntrusivePtr< ::NYT::NYTree::I##name##Node > As##name() \
    { \
        return this; \
    } \
    \
    virtual void SetSelf(TReqSet* request, TRspSet* response, TCtxSet::TPtr context) \
    { \
        UNUSED(response); \
        DoSetSelf< ::NYT::NYTree::I##name##Node >(this, request->value()); \
        context->Reply(); \
    }

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT

