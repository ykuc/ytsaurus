#include "stdafx.h"
#include "cypress_integration.h"

#include <ytlib/cypress/virtual.h>
#include <ytlib/ytree/virtual.h>
#include <ytlib/ytree/fluent.h>
#include <ytlib/misc/string.h>

namespace NYT {
namespace NTransactionServer {

using namespace NYTree;
using namespace NCypress;

////////////////////////////////////////////////////////////////////////////////

class TVirtualTransactionMap
    : public TVirtualMapBase
{
public:
    TVirtualTransactionMap(TTransactionManager* transactionManager)
        : TransactionManager(transactionManager)
    { }

private:
    TTransactionManager::TPtr TransactionManager;

    virtual yvector<Stroka> GetKeys(size_t sizeLimit) const
    {
        const auto& ids = TransactionManager->GetTransactionIds(sizeLimit);
        return ConvertToStrings(ids.begin(), Min(ids.size(), sizeLimit));
    }

    virtual size_t GetSize() const
    {
        return TransactionManager->GetTransactionCount();
    }

    virtual IYPathService::TPtr GetItemService(const Stroka& key) const
    {
        auto id = TTransactionId::FromString(key);
        return TransactionManager->GetObjectManager()->GetProxy(id);
    }
};

NCypress::INodeTypeHandler::TPtr CreateTransactionMapTypeHandler(
    NCypress::TCypressManager* cypressManager,
    TTransactionManager* transactionManager)
{
    YASSERT(cypressManager);
    YASSERT(transactionManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        EObjectType::TransactionMap,
        ~New<TVirtualTransactionMap>(transactionManager));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionServer
} // namespace NYT
