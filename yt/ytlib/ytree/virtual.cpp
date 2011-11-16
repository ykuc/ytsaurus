#include "stdafx.h"
#include "virtual.h"
#include "fluent.h"
#include "node_detail.h"

namespace NYT {
namespace NYTree {

using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

IYPathService::TResolveResult TVirtualMapBase::Resolve(TYPath path, const Stroka& verb)
{
    UNUSED(path);
    UNUSED(verb);
    ythrow yexception() << "Resolution is not supported";
}

void TVirtualMapBase::Invoke(NRpc::IServiceContext* context)
{
    UNUSED(context);
}

//IYPathService::TGetResult TVirtualMapBase::Get(TYPath path, IYsonConsumer* consumer)
//{
//    // TODO: attributes?
//
//    if (path.Empty()) {
//        auto keys = GetKeys();
//        // TODO: refactor using fluent API
//        consumer->OnBeginMap();
//        FOREACH (const auto& key, keys) {
//            consumer->OnMapItem(key);
//            auto service = GetItemService(key);
//            YASSERT(~service != NULL);
//            // TODO: use constant for /
//            GetYPath(service, "/", consumer);
//        }
//        consumer->OnEndMap(false);
//    } else {
//        Stroka prefix;
//        TYPath suffixPath;
//        ChopYPathPrefix(path, &prefix, &suffixPath);
//
//        auto service = GetItemService(prefix);
//        if (~service == NULL) {
//            ythrow TYTreeException() << Sprintf("Key %s is not found",
//                ~prefix.Quote());
//        }
//
//        return TGetResult::CreateRecurse(service, suffixPath);
//    }
//    return TGetResult::CreateDone();
//}

////////////////////////////////////////////////////////////////////////////////

class TVirtualEntityNode
    : public TNodeBase
    , public IEntityNode
{
    YTREE_NODE_TYPE_OVERRIDES(Entity)

public:
    TVirtualEntityNode(
        TYPathServiceProducer* builder,
        INodeFactory* factory)
        : Builder(builder)
        , Factory(factory)
    { }

    virtual INodeFactory* GetFactory() const
    {
        return Factory;
    }

    virtual ICompositeNode::TPtr GetParent() const
    {
        return Parent;
    }

    virtual void SetParent(ICompositeNode::TPtr parent)
    {
        Parent = ~parent;
    }

    virtual IMapNode::TPtr GetAttributes() const
    {
        return Attributes;
    }

    virtual void SetAttributes(IMapNode::TPtr attributes)
    {
        Attributes = attributes;
    }

    virtual void Invoke(IServiceContext* context)
    {
        TYPath path = context->GetPath();
        if (IsEmptyYPath(path)) {
            TNodeBase::Invoke(context);
        } else {
            auto service = Builder->Do();
            service->Invoke(context);
        }
    }

private:
    TYPathServiceProducer::TPtr Builder;
    INodeFactory* Factory;

    ICompositeNode* Parent;
    IMapNode::TPtr Attributes;

    TResolveResult ResolveRecursive(TYPath path, const Stroka& verb)
    {
        auto service = Builder->Do();
        return service->Resolve(path, verb);
    }

};

INode::TPtr CreateVirtualNode(
    TYPathServiceProducer* builder,
    INodeFactory* factory)
{
    return New<TVirtualEntityNode>(builder, factory);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT

