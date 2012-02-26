#pragma once

#include "common.h"
#include "attribute_set.h"
#include "type_handler.h"
#include "object_manager.pb.h"

#include <ytlib/cell_master/public.h>
#include <ytlib/misc/thread_affinity.h>
#include <ytlib/misc/id_generator.h>
#include <ytlib/meta_state/composite_meta_state.h>
#include <ytlib/meta_state/map.h>

namespace NYT {

// TODO(babenko): killme
namespace NCypress {
    class TCypressManager;
}

namespace NTransactionServer {
    class TTransactionManager;
}

namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

//! Provides high-level management and tracking of objects and their attributes.
/*!
 *  \note
 *  Thread affinity: single-threaded
 */
class TObjectManager
    : public NMetaState::TMetaStatePart
{
public:
    typedef TIntrusivePtr<TObjectManager> TPtr;

    //! Initializes a new instance.
    TObjectManager(
        NMetaState::IMetaStateManager* metaStateManager,
        NMetaState::TCompositeMetaState* metaState,
        TCellId cellId);

    ~TObjectManager();

    // TODO(babenko): killme
    void SetCypressManager(NCypress::TCypressManager* cypressManager);
    void SetTransactionManager(NTransactionServer::TTransactionManager* transactionmanager);

    //! Registers a new type handler.
    /*!
     *  It asserts than no handler of this type is already registered.
     */
    void RegisterHandler(IObjectTypeHandler* handler);

    //! Returns the handler for a given type or NULL if the type is unknown.
    IObjectTypeHandler* FindHandler(EObjectType type) const;

    //! Returns the handler for a given type.
    IObjectTypeHandler* GetHandler(EObjectType type) const;
    
    //! Returns the handler for a given id.
    IObjectTypeHandler* GetHandler(const TObjectId& id) const;

    //! Returns the cell id.
    TCellId GetCellId() const;

    //! Creates a new unique object id.
    TObjectId GenerateId(EObjectType type);

    //! Adds a reference.
    void RefObject(const TObjectId& id);

    //! Removes a reference (from an unspecified source).
    void UnrefObject(const TObjectId& id);

    //! Returns the current reference counter.
    i32 GetObjectRefCounter(const TObjectId& id);

    //! Returns True if an object with the given #id exists.
    bool ObjectExists(const TObjectId& id);

    //! Returns a proxy for the object with the given versioned id or NULL if there's no such object.
    IObjectProxy::TPtr FindProxy(const TVersionedObjectId& id);

    //! Returns a proxy for the object with the given versioned id or NULL. Fails if there's no such object.
    IObjectProxy::TPtr GetProxy(const TVersionedObjectId& id);

	//! Creates a new empty attribute set.
    TAttributeSet* CreateAttributes(const TVersionedObjectId& id);

	//! Removes an existing attribute set.
    void RemoveAttributes(const TVersionedObjectId& id);

    //! Called when a versioned object is branched.
    void BranchAttributes(
        const TVersionedObjectId& originatingId,
        const TVersionedObjectId& branchedId);

    //! Called when a versioned object is merged during transaction commit.
    void MergeAttributes(
        const TVersionedObjectId& originatingId,
        const TVersionedObjectId& branchedId);

	//! Returns a YPath service that handles all requests.
	/*!
	 *  This service supports some special prefix syntax for YPaths:
	 */
    NYTree::IYPathService* GetRootService();
    
	//! Executes a YPath verb, logging the change if neccessary.
	/*!
	 *  \param id The id of the object that handles the verb.
	 *  If the change is logged, this id is written to the changelog and
	 *  used afterwards to replay the change.
	 *  \param isWrite True if the verb modifies the state and thus must be logged.
	 *  \param context The request context.
	 *  \param action An action to call that executes the actual verb logic.
	 *  
	 *  Note that #action takes a context as a parameter. This is because the original #context
	 *  gets wrapped to intercept replies and #action gets the wrapped instance.
	 */
    void ExecuteVerb(
        const TVersionedObjectId& id,
        bool isWrite,
        NRpc::IServiceContext* context,
        IParamAction<NRpc::IServiceContext*>* action);

    DECLARE_METAMAP_ACCESSORS(Attributes, TAttributeSet, TVersionedObjectId);

private:
    class TServiceContextWrapper;
    class TRootService;

    TCellId CellId;
    yvector< TIdGenerator<ui64> > TypeToCounter;
    yvector<IObjectTypeHandler::TPtr> TypeToHandler;
    TIntrusivePtr<TRootService> RootService;

    TIntrusivePtr<NCypress::TCypressManager> CypressManager;
    TIntrusivePtr<NTransactionServer::TTransactionManager> TransactionManager;

    // Stores deltas from parent transaction.
    NMetaState::TMetaStateMap<TVersionedObjectId, TAttributeSet> Attributes;

    void SaveKeys(TOutputStream* output);
    void SaveValues(TOutputStream* output);
    void LoadKeys(TInputStream* input);
    void LoadValues(TInputStream* input, NCellMaster::TLoadContext context);
    virtual void Clear();

    TVoid ReplayVerb(const NProto::TMsgExecuteVerb& message);

    DECLARE_THREAD_AFFINITY_SLOT(StateThread);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT

