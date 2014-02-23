#pragma once

#include "public.h"
#include "dynamic_memory_store_bits.h"

#include <core/misc/chunked_memory_pool.h>

#include <ytlib/tablet_client/public.h>

#include <ytlib/new_table_client/public.h>

#include <ytlib/chunk_client/chunk.pb.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TStoreManager
    : public TRefCounted
{
public:
    TStoreManager(
        TTabletManagerConfigPtr config,
        TTablet* tablet);

    ~TStoreManager();

    TTablet* GetTablet() const;

    //! Returns |true| is there are outstanding locks to any of in-memory stores.
    //! Used to determine when it is safe to unmount the tablet.
    bool HasActiveLocks() const;

    //! Returns |true| is there are some in-memory stores that are not flushed yet.
    bool HasUnflushedStores() const;

    //! Executes a bunch of row lookup requests. Request parameters are parsed via #reader,
    //! response is written into #writer.
    void LookupRows(
        TTimestamp timestamp,
        NTabletClient::TWireProtocolReader* reader,
        NTabletClient::TWireProtocolWriter* writer);

    void WriteRow(
        TTransaction* transaction,
        NVersionedTableClient::TUnversionedRow row,
        bool prewrite,
        std::vector<TDynamicRow>* lockedRows);

    void DeleteRow(
        TTransaction* transaction,
        NVersionedTableClient::TKey key,
        bool prewrite,
        std::vector<TDynamicRow>* lockedRows);

    void ConfirmRow(const TDynamicRowRef& rowRef);
    void PrepareRow(const TDynamicRowRef& rowRef);
    void CommitRow(const TDynamicRowRef& rowRef);
    void AbortRow(const TDynamicRowRef& rowRef);

    bool IsRotationNeeded() const;
    void SetRotationScheduled();
    void ResetRotationScheduled();
    void Rotate(bool createNew);
    void CreateActiveStore();
    
private:
    TTabletManagerConfigPtr Config_;
    TTablet* Tablet_;

    bool RotationScheduled_;
    yhash_set<TDynamicMemoryStorePtr> LockedStores_;

    TChunkedMemoryPool LookupPool_;
    std::vector<NVersionedTableClient::TUnversionedRow> PooledKeys_;
    std::vector<NVersionedTableClient::TUnversionedRow> UnversionedPooledRows_;
    std::vector<NVersionedTableClient::TVersionedRow> VersionedPooledRows_;


    TDynamicRow MaybeMigrateRow(const TDynamicRowRef& rowRef);

    void CheckLockAndMaybeMigrateRow(
        TTransaction* transaction,
        NVersionedTableClient::TUnversionedRow key,
        ERowLockMode mode);

    void CheckForUnlockedStore(const TDynamicMemoryStorePtr& store);

};

DEFINE_REFCOUNTED_TYPE(TStoreManager)

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
