#include "node_proxy.h"

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

TMapNodeProxy::TMapNodeProxy(
    TCypressManager::TPtr state,
    const TTransactionId& transactionId,
    const TNodeId& nodeId)
    : TCompositeNodeProxyBase(
        state,
        transactionId,
        nodeId)
{ }

void TMapNodeProxy::Clear()
{
    ValidateModifiable();

    // TODO: refcount
    auto& impl = GetTypedImplForUpdate();
    impl.NameToChild().clear();
    impl.ChildToName().clear();
}

int TMapNodeProxy::GetChildCount() const
{
    return GetTypedImpl().NameToChild().ysize();
}

yvector< TPair<Stroka, INode::TPtr> > TMapNodeProxy::GetChildren() const
{
    yvector< TPair<Stroka, INode::TPtr> > result;
    const auto& map = GetTypedImpl().NameToChild();
    result.reserve(map.ysize());
    FOREACH (const auto& pair, map) {
        result.push_back(MakePair(
            pair.First(),
            GetProxy<INode>(pair.Second())));
    }
    return result;
}

INode::TPtr TMapNodeProxy::FindChild(const Stroka& name) const
{
    const auto& map = GetTypedImpl().NameToChild();
    auto it = map.find(name);
    return it == map.end() ? NULL : GetProxy<INode>(it->Second());
}

bool TMapNodeProxy::AddChild(INode::TPtr child, const Stroka& name)
{
    ValidateModifiable();

    // TODO: refcount
    auto& impl = GetTypedImplForUpdate();

    auto childProxy = ToProxy(child);
    auto childId = childProxy->GetNodeId();

    if (impl.NameToChild().insert(MakePair(name, childId)).Second()) {
        YVERIFY(impl.ChildToName().insert(MakePair(childId, name)).Second());
        childProxy->GetImplForUpdate().SetParentId(NodeId);
        return true;
    } else {
        return false;
    }
}

bool TMapNodeProxy::RemoveChild(const Stroka& name)
{
    ValidateModifiable();

    // TODO: refcount
    auto& impl = GetTypedImplForUpdate();

    auto it = impl.NameToChild().find(name);
    if (it == impl.NameToChild().end())
        return false;

    const auto& childId = it->Second();
    auto childProxy = GetProxy<ICypressNodeProxy>(childId);
    auto& childImpl = childProxy->GetImplForUpdate();
    childImpl.SetParentId(NullNodeId);
    impl.NameToChild().erase(it);
    YVERIFY(impl.ChildToName().erase(childId) == 1);
    return true;
}

void TMapNodeProxy::RemoveChild(INode::TPtr child)
{
    ValidateModifiable();

    // TODO: refcount

    auto& impl = GetTypedImplForUpdate();
    
    auto childProxy = ToProxy(child);
    childProxy->GetImplForUpdate().SetParentId(NullNodeId);

    auto it = impl.ChildToName().find(childProxy->GetNodeId());
    YASSERT(it != impl.ChildToName().end());

    Stroka name = it->Second();
    impl.ChildToName().erase(it);
    YVERIFY(impl.NameToChild().erase(name) == 1);
}

void TMapNodeProxy::ReplaceChild(INode::TPtr oldChild, INode::TPtr newChild)
{
    ValidateModifiable();

    // TODO: refcount

    auto& impl = GetTypedImplForUpdate();

    if (oldChild == newChild)
        return;

    auto oldChildProxy = ToProxy(oldChild);
    auto newChildProxy = ToProxy(newChild);

    auto it = impl.ChildToName().find(oldChildProxy->GetNodeId());
    YASSERT(it != impl.ChildToName().end());

    Stroka name = it->Second();

    oldChildProxy->GetImplForUpdate().SetParentId(NullNodeId);
    impl.ChildToName().erase(it);

    impl.NameToChild()[name] = newChildProxy->GetNodeId();
    newChildProxy->GetImplForUpdate().SetParentId(NodeId);
    YVERIFY(impl.ChildToName().insert(MakePair(newChildProxy->GetNodeId(), name)).Second());
}

// TODO: maybe extract base?
IYPathService::TNavigateResult TMapNodeProxy::Navigate(TYPath path)
{
    if (path.empty()) {
        return TNavigateResult::CreateDone(this);
    }

    Stroka prefix;
    TYPath tailPath;
    ChopYPathPrefix(path, &prefix, &tailPath);

    auto child = FindChild(prefix);
    if (~child == NULL) {
        throw TYPathException() << Sprintf("Child %s it not found",
            ~prefix.Quote());
    }

    return TNavigateResult::CreateRecurse(AsYPath(child), tailPath);
}

IYPathService::TSetResult TMapNodeProxy::Set(
    TYPath path,
    TYsonProducer::TPtr producer)
{
    if (path.empty()) {
        ValidateModifiable();
        SetNodeFromProducer(IMapNode::TPtr(this), producer);
        return TSetResult::CreateDone();
    }

    Stroka prefix;
    TYPath tailPath;
    ChopYPathPrefix(path, &prefix, &tailPath);

    auto child = FindChild(prefix);
    if (~child != NULL) {
        return TSetResult::CreateRecurse(AsYPath(child), tailPath);
    }

    if (tailPath.empty()) {
        TTreeBuilder builder(GetFactory());
        producer->Do(&builder);
        INode::TPtr newChild = builder.GetRoot();
        AddChild(newChild, prefix);
        return TSetResult::CreateDone();
    } else {
        INode::TPtr newChild = ~GetFactory()->CreateMap();
        AddChild(newChild, prefix);
        return TSetResult::CreateRecurse(AsYPath(newChild), tailPath);
    }
}

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT

