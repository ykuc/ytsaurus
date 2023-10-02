from .test_sorted_dynamic_tables import TestSortedDynamicTablesBase

from yt_helpers import profiler_factory

from yt_commands import (
    authors, wait, create, exists, get, set, ls, set_banned_flag, insert_rows, remove, select_rows,
    lookup_rows, delete_rows, remount_table, build_snapshot,
    write_table, alter_table, read_table, map, sync_reshard_table, sync_create_cells,
    sync_mount_table, sync_unmount_table, sync_flush_table, sync_compact_table, gc_collect,
    start_transaction, commit_transaction, get_singular_chunk_id, write_file, read_hunks,
    write_journal)

from yt_type_helpers import make_schema

from yt_env_setup import (
    Restarter,
    NODES_SERVICE
)

from yt.common import YtError
from yt.test_helpers import assert_items_equal

import pytest
import yt.yson as yson

import time

import builtins

################################################################################

HUNK_COMPATIBLE_CHUNK_FORMATS = [
    "table_versioned_simple",
    "table_versioned_columnar",
    "table_versioned_slim",
    "table_versioned_indexed",
]

################################################################################


class TestSortedDynamicTablesHunks(TestSortedDynamicTablesBase):
    NUM_TEST_PARTITIONS = 7

    NUM_NODES = 15

    NUM_SCHEDULERS = 1

    SCHEMA = [
        {"name": "key", "type": "int64", "sort_order": "ascending"},
        {"name": "value", "type": "string"},
    ]

    ROWS_WITH_VARIOUS_ANY_VALUES = [
        {"key": 0, "value": "0"},
        {"key": 1, "value": 123},
        {"key": 2, "value": 3.14},
        {"key": 3, "value": True},
        {"key": 4, "value": [{}, {}]},
        {"key": 5, "value": "0" * 20},
        {"key": 6, "value": yson.YsonEntity()}
    ]

    KEYS_OF_ROWS_WITH_VARIOUS_ANY_VALUES = [{"key": x["key"]} for x in ROWS_WITH_VARIOUS_ANY_VALUES]

    DELTA_DYNAMIC_MASTER_CONFIG = {
        "tablet_manager": {
            "enable_hunks": True
        }
    }

    # Do not allow multiple erasure parts per node.
    DELTA_MASTER_CONFIG = {}

    def _get_table_schema(self, schema, max_inline_hunk_size):
        schema[1]["max_inline_hunk_size"] = max_inline_hunk_size
        return schema

    def _create_table(self, chunk_format="table_versioned_simple", max_inline_hunk_size=10, hunk_erasure_codec="none", schema=SCHEMA, dynamic=True):
        create_table_function = self._create_simple_table if dynamic else self._create_simple_static_table
        schema = self._get_table_schema(schema, max_inline_hunk_size)
        if not dynamic:
            schema = make_schema(
                schema,
                unique_keys=True,
            )

        create_table_function("//tmp/t",
                              schema=schema,
                              enable_dynamic_store_read=False,
                              hunk_chunk_reader={
                                  "max_hunk_count_per_read": 2,
                                  "max_total_hunk_length_per_read": 60,
                                  "fragment_read_hedging_delay": 1,
                                  "max_inflight_fragment_length": 60,
                                  "max_inflight_fragment_count": 2,
                              },
                              hunk_chunk_writer={
                                  "desired_block_size": 50,
                              },
                              max_hunk_compaction_garbage_ratio=0.5,
                              enable_lsm_verbose_logging=True,
                              chunk_format=chunk_format,
                              hunk_erasure_codec=hunk_erasure_codec)

    @authors("babenko")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_flush_inline(self, chunk_format, hunk_erasure_codec):
        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, hunk_erasure_codec=hunk_erasure_codec)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")

        sync_mount_table("//tmp/t")
        keys = [{"key": i} for i in range(10)]
        rows = [{"key": i, "value": "value" + str(i)} for i in range(10)]
        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)
        assert_items_equal(lookup_rows("//tmp/t", keys), rows)
        assert_items_equal(select_rows("* from [//tmp/t] where value = \"{}\"".format(rows[0]["value"])), [rows[0]])
        sync_unmount_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 0

        assert get("#{}/@hunk_chunk_refs".format(store_chunk_ids[0])) == []

        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), rows)
        assert_items_equal(lookup_rows("//tmp/t", keys), rows)

    @authors("babenko")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_flush_to_hunk_chunk(self, chunk_format, hunk_erasure_codec):
        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, hunk_erasure_codec=hunk_erasure_codec)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")

        sync_mount_table("//tmp/t")
        keys = [{"key": i} for i in range(10)]
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)
        assert_items_equal(lookup_rows("//tmp/t", keys), rows)
        sync_unmount_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]

        assert get("#{}/@ref_counter".format(store_chunk_id)) == 1

        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id = hunk_chunk_ids[0]

        assert get("#{}/@hunk_chunk_refs".format(store_chunk_id)) == [
            {"chunk_id": hunk_chunk_ids[0], "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": hunk_erasure_codec}
        ]

        assert get("#{}/@ref_counter".format(hunk_chunk_id)) == 2
        assert get("#{}/@data_weight".format(hunk_chunk_id)) == 260
        assert get("#{}/@uncompressed_data_size".format(hunk_chunk_id)) == 340
        assert get("#{}/@compressed_data_size".format(hunk_chunk_id)) == 340

        assert get("//tmp/t/@chunk_format_statistics/hunk_default/chunk_count") == 1

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 1
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 10
        assert hunk_statistics["referenced_hunk_count"] == 10
        assert hunk_statistics["total_hunk_length"] == 260
        assert hunk_statistics["total_referenced_hunk_length"] == 260

        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), rows)
        assert_items_equal(lookup_rows("//tmp/t", keys), rows)
        assert_items_equal(select_rows("* from [//tmp/t] where value = \"{}\"".format(rows[0]["value"])), [rows[0]])

        remove("//tmp/t")

        wait(lambda: not exists("#{}".format(store_chunk_id)) and not exists("#{}".format(hunk_chunk_id)))

    @authors("gritukan")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_flush_nulls_to_hunk_chunk(self, chunk_format, hunk_erasure_codec):
        sync_create_cells(1)

        SCHEMA_WITH_NULL = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "string"},
            {"name": "null_value", "type": "string", "max_inline_hunk_size": 20},
        ]
        self._create_table(chunk_format=chunk_format, hunk_erasure_codec=hunk_erasure_codec, schema=SCHEMA_WITH_NULL)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")

        sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20, "null_value": yson.YsonEntity()} for i in range(10)]
        insert_rows("//tmp/t", rows)

        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        remove("//tmp/t")

    @authors("gritukan")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    def test_lookup_hunk_chunk_with_repair(self, chunk_format):
        self._separate_tablet_and_data_nodes()
        set("//sys/@config/chunk_manager/enable_chunk_replicator", False)

        sync_create_cells(1)[0]
        self._create_table(chunk_format=chunk_format, hunk_erasure_codec="isa_reed_solomon_6_3")
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")

        sync_mount_table("//tmp/t")
        keys = [{"key": i} for i in range(10)]
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)
        assert_items_equal(lookup_rows("//tmp/t", keys), rows)
        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id = hunk_chunk_ids[0]

        def set_ban_for_parts(part_indices, banned_flag):
            chunk_replicas = get("#{}/@stored_replicas".format(hunk_chunk_id))

            nodes_to_ban = []
            for part_index in part_indices:
                nodes = list(str(r) for r in chunk_replicas if r.attributes["index"] == part_index)
                nodes_to_ban += nodes

            set_banned_flag(banned_flag, nodes_to_ban)

        set_ban_for_parts([0, 1, 4], True)
        assert_items_equal(lookup_rows("//tmp/t", keys), rows)
        set_ban_for_parts([0, 1, 4], False)

        set_ban_for_parts([0, 1, 2, 4], True)
        with pytest.raises(YtError):
            lookup_rows("//tmp/t", keys)
        set_ban_for_parts([0, 1, 2, 4], False)

    @authors("gritukan")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("available", [False, True])
    def test_repair_erasure_hunk_chunk(self, chunk_format, available):
        self._separate_tablet_and_data_nodes()

        sync_create_cells(1)[0]
        self._create_table(chunk_format=chunk_format, hunk_erasure_codec="isa_reed_solomon_6_3")
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")

        sync_mount_table("//tmp/t")
        keys = [{"key": i} for i in range(10)]
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)
        assert_items_equal(lookup_rows("//tmp/t", keys), rows)
        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id = hunk_chunk_ids[0]

        def set_ban_for_parts(part_indices, banned_flag):
            chunk_replicas = get("#{}/@stored_replicas".format(hunk_chunk_id))

            nodes_to_ban = []
            for part_index in part_indices:
                nodes = list(str(r) for r in chunk_replicas if r.attributes["index"] == part_index)
                nodes_to_ban += nodes

            set_banned_flag(banned_flag, nodes_to_ban)

        if available:
            set_ban_for_parts([0, 1, 4], True)
            time.sleep(1)
            wait(lambda: get("//sys/data_missing_chunks/@count") == 0)
            wait(lambda: get("//sys/parity_missing_chunks/@count") == 0)
            assert_items_equal(lookup_rows("//tmp/t", keys), rows)
        else:
            set_ban_for_parts([0, 1, 2, 8], True)
            time.sleep(2)
            assert hunk_chunk_id in ls("//sys/data_missing_chunks")
            assert hunk_chunk_id in ls("//sys/parity_missing_chunks")

    @authors("babenko")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_compaction(self, chunk_format, hunk_erasure_codec):
        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, hunk_erasure_codec=hunk_erasure_codec)
        set("//tmp/t/@max_hunk_compaction_size", 1)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")
        sync_mount_table("//tmp/t")
        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        keys1 = [{"key": i} for i in range(10)]
        insert_rows("//tmp/t", rows1)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows1)
        assert_items_equal(lookup_rows("//tmp/t", keys1), rows1)
        sync_unmount_table("//tmp/t")

        assert len(self._get_store_chunk_ids("//tmp/t")) == 1
        assert len(self._get_hunk_chunk_ids("//tmp/t")) == 1

        sync_mount_table("//tmp/t")
        rows2 = [{"key": i, "value": "value" + str(i) + "y" * 20} for i in range(10, 20)]
        keys2 = [{"key": i} for i in range(10, 20)]
        insert_rows("//tmp/t", rows2)
        select_rows("* from [//tmp/t]")
        assert_items_equal(lookup_rows("//tmp/t", keys1 + keys2), rows1 + rows2)

        sync_unmount_table("//tmp/t")
        wait(lambda: get("//tmp/t/@chunk_count") == 4)

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 2
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 2

        store_chunk_id1 = store_chunk_ids[0]
        store_chunk_id2 = store_chunk_ids[1]
        hunk_chunk_id1 = get("#{}/@hunk_chunk_refs/0/chunk_id".format(store_chunk_id1))
        hunk_chunk_id2 = get("#{}/@hunk_chunk_refs/0/chunk_id".format(store_chunk_id2))
        assert_items_equal(hunk_chunk_ids, [hunk_chunk_id1, hunk_chunk_id2])

        if get("#{}/@hunk_chunk_refs/0/total_hunk_length".format(store_chunk_id1)) \
                > get("#{}/@hunk_chunk_refs/0/total_hunk_length".format(store_chunk_id2)):
            store_chunk_id1, store_chunk_id2 = store_chunk_id2, store_chunk_id1
            hunk_chunk_id1, hunk_chunk_id2 = hunk_chunk_id2, hunk_chunk_id1

        assert get("#{}/@hunk_chunk_refs".format(store_chunk_id1)) == [
            {"chunk_id": hunk_chunk_id1, "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": hunk_erasure_codec},
        ]
        assert get("#{}/@hunk_chunk_refs".format(store_chunk_id2)) == [
            {"chunk_id": hunk_chunk_id2, "hunk_count": 10, "total_hunk_length": 270, "erasure_codec": hunk_erasure_codec},
        ]

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 2
        assert hunk_statistics["store_chunk_count"] == 2
        assert hunk_statistics["hunk_count"] == 20
        assert hunk_statistics["referenced_hunk_count"] == 20
        assert hunk_statistics["total_hunk_length"] == 530
        assert hunk_statistics["total_referenced_hunk_length"] == 530

        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), rows1 + rows2)
        assert_items_equal(lookup_rows("//tmp/t", keys1 + keys2), rows1 + rows2)

        set("//tmp/t/@forced_store_compaction_revision", 1)
        remount_table("//tmp/t")

        wait(lambda: get("//tmp/t/@chunk_count") == 3)

        compacted_store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(compacted_store_chunk_ids) == 1
        compacted_store_id = compacted_store_chunk_ids[0]

        assert_items_equal(get("#{}/@hunk_chunk_refs".format(compacted_store_id)), [
            {"chunk_id": hunk_chunk_id1, "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": hunk_erasure_codec},
            {"chunk_id": hunk_chunk_id2, "hunk_count": 10, "total_hunk_length": 270, "erasure_codec": hunk_erasure_codec},
        ])

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 2
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 20
        assert hunk_statistics["referenced_hunk_count"] == 20
        assert hunk_statistics["total_hunk_length"] == 530
        assert hunk_statistics["total_referenced_hunk_length"] == 530

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), rows1 + rows2)
        assert_items_equal(lookup_rows("//tmp/t", keys1 + keys2), rows1 + rows2)

    @authors("babenko")
    def test_hunk_sweep(self):
        sync_create_cells(1)
        self._create_table()

        sync_mount_table("//tmp/t")
        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        keys1 = [{"key": i} for i in range(10)]
        rows2 = [{"key": i, "value": "value" + str(i)} for i in range(10, 20)]
        keys2 = [{"key": i} for i in range(10, 20)]
        insert_rows("//tmp/t", rows1 + rows2)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows1 + rows2)
        assert_items_equal(lookup_rows("//tmp/t", keys1 + keys2), rows1 + rows2)
        sync_unmount_table("//tmp/t")

        assert len(self._get_store_chunk_ids("//tmp/t")) == 1
        assert len(self._get_hunk_chunk_ids("//tmp/t")) == 1

        sync_mount_table("//tmp/t")
        delete_rows("//tmp/t", keys1)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows2)
        assert_items_equal(lookup_rows("//tmp/t", keys1 + keys2), rows2)
        sync_unmount_table("//tmp/t")

        assert len(self._get_store_chunk_ids("//tmp/t")) == 2
        assert len(self._get_hunk_chunk_ids("//tmp/t")) == 1

        sync_mount_table("//tmp/t")
        assert_items_equal(select_rows("* from [//tmp/t]"), rows2)
        assert_items_equal(lookup_rows("//tmp/t", keys1 + keys2), rows2)

        set("//tmp/t/@min_data_ttl", 60000)
        sync_compact_table("//tmp/t")

        wait(lambda: get("//tmp/t/@chunk_count") == 2)
        assert len(self._get_store_chunk_ids("//tmp/t")) == 1
        assert len(self._get_hunk_chunk_ids("//tmp/t")) == 1

        set("//tmp/t/@min_data_ttl", 0)
        set("//tmp/t/@min_data_versions", 1)
        sync_compact_table("//tmp/t")

        wait(lambda: get("//tmp/t/@chunk_count") == 1)
        assert len(self._get_store_chunk_ids("//tmp/t")) == 1
        assert len(self._get_hunk_chunk_ids("//tmp/t")) == 0

    @authors("babenko")
    def test_reshard(self):
        sync_create_cells(1)
        self._create_table()

        sync_mount_table("//tmp/t")
        # This chunk will intersect both of new tablets and will produce chunk views.
        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in [0, 10, 20, 30, 40, 50]]
        insert_rows("//tmp/t", rows1)
        sync_unmount_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id1 = store_chunk_ids[0]

        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id1 = hunk_chunk_ids[0]

        sync_mount_table("//tmp/t")
        # This chunk will be fully contained in the first tablet.
        rows2 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in [11, 12, 13]]
        insert_rows("//tmp/t", rows2)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows1 + rows2)
        sync_unmount_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 2
        store_chunk_id2 = list(builtins.set(store_chunk_ids) - builtins.set([store_chunk_id1]))[0]

        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 2
        hunk_chunk_id2 = list(builtins.set(hunk_chunk_ids) - builtins.set([hunk_chunk_id1]))[0]

        gc_collect()
        assert get("#{}/@ref_counter".format(store_chunk_id1)) == 1
        assert get("#{}/@ref_counter".format(hunk_chunk_id1)) == 2
        assert get("#{}/@ref_counter".format(store_chunk_id2)) == 1
        assert get("#{}/@ref_counter".format(hunk_chunk_id2)) == 2

        sync_reshard_table("//tmp/t", [[], [30]])

        gc_collect()
        assert get("#{}/@ref_counter".format(store_chunk_id1)) == 2
        assert get("#{}/@ref_counter".format(hunk_chunk_id1)) == 3
        assert get("#{}/@ref_counter".format(store_chunk_id2)) == 1
        assert get("#{}/@ref_counter".format(hunk_chunk_id2)) == 2

        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), rows1 + rows2)

    @authors("ifsmirnov")
    def test_reshard_with_tablet_count(self):
        sync_create_cells(1)
        self._create_table()

        sync_reshard_table("//tmp/t", [[], [30]])
        sync_mount_table("//tmp/t")
        rows = [
            {"key": i, "value": "value" + str(i) + "x" * 20}
            for i in [0, 10, 20, 30, 40, 50]]
        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        # Expel chunks from Eden.
        sync_flush_table("//tmp/t")
        sync_compact_table("//tmp/t")

        sync_unmount_table("//tmp/t")
        sync_reshard_table("//tmp/t", 1)
        assert get("//tmp/t/@tablet_state") == "unmounted"
        assert get("//tmp/t/@tablet_count") == 1

        set("//tmp/t/@enable_compaction_and_partitioning", False)
        sync_mount_table("//tmp/t")
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        sync_unmount_table("//tmp/t")
        sync_reshard_table("//tmp/t", 2)
        assert get("//tmp/t/@tablet_state") == "unmounted"
        assert get("//tmp/t/@tablet_count") == 2

        sync_mount_table("//tmp/t")
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

    @authors("babenko")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_compaction_writes_hunk_chunk(self, chunk_format, hunk_erasure_codec):
        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, max_inline_hunk_size=1000, hunk_erasure_codec=hunk_erasure_codec)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")

        sync_mount_table("//tmp/t")
        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        rows2 = [{"key": i, "value": "value" + str(i)} for i in range(10, 20)]
        insert_rows("//tmp/t", rows1 + rows2)
        sync_unmount_table("//tmp/t")

        alter_table("//tmp/t", schema=self._get_table_schema(schema=self.SCHEMA, max_inline_hunk_size=10))

        chunk_ids_before_compaction = get("//tmp/t/@chunk_ids")
        assert len(chunk_ids_before_compaction) == 1
        chunk_id_before_compaction = chunk_ids_before_compaction[0]
        assert get("#{}/@hunk_chunk_refs".format(chunk_id_before_compaction)) == []

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 0
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 0
        assert hunk_statistics["referenced_hunk_count"] == 0
        assert hunk_statistics["total_hunk_length"] == 0
        assert hunk_statistics["total_referenced_hunk_length"] == 0

        sync_mount_table("//tmp/t")

        sync_compact_table("//tmp/t")

        wait(lambda: get("//tmp/t/@chunk_ids") != chunk_ids_before_compaction)
        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        compacted_store_id = store_chunk_ids[0]
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id = hunk_chunk_ids[0]

        assert_items_equal(get("#{}/@hunk_chunk_refs".format(compacted_store_id)), [
            {"chunk_id": hunk_chunk_id, "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": hunk_erasure_codec},
        ])

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 1
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 10
        assert hunk_statistics["referenced_hunk_count"] == 10
        assert hunk_statistics["total_hunk_length"] == 260
        assert hunk_statistics["total_referenced_hunk_length"] == 260

    @authors("babenko")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_compaction_inlines_hunks(self, chunk_format, hunk_erasure_codec):
        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format,
                           max_inline_hunk_size=10,
                           hunk_erasure_codec=hunk_erasure_codec)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")

        sync_mount_table("//tmp/t")
        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        rows2 = [{"key": i, "value": "value" + str(i)} for i in range(10, 20)]
        insert_rows("//tmp/t", rows1 + rows2)
        sync_unmount_table("//tmp/t")

        alter_table("//tmp/t", schema=self._get_table_schema(schema=self.SCHEMA, max_inline_hunk_size=1000))

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id = hunk_chunk_ids[0]
        assert_items_equal(get("#{}/@hunk_chunk_refs".format(store_chunk_id)), [
            {"chunk_id": hunk_chunk_id, "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": hunk_erasure_codec},
        ])

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 1
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 10
        assert hunk_statistics["referenced_hunk_count"] == 10
        assert hunk_statistics["total_hunk_length"] == 260
        assert hunk_statistics["total_referenced_hunk_length"] == 260

        sync_mount_table("//tmp/t")

        sync_compact_table("//tmp/t")

        wait(lambda: len(get("//tmp/t/@chunk_ids")) == 1)
        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]
        assert get("#{}/@hunk_chunk_refs".format(store_chunk_id)) == []

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 0
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 0
        assert hunk_statistics["referenced_hunk_count"] == 0
        assert hunk_statistics["total_hunk_length"] == 0
        assert hunk_statistics["total_referenced_hunk_length"] == 0

    @authors("babenko")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_compaction_rewrites_hunk_chunk(self, chunk_format, hunk_erasure_codec):
        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, max_inline_hunk_size=10, hunk_erasure_codec=hunk_erasure_codec)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")

        sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        keys = [{"key": i} for i in range(10)]
        insert_rows("//tmp/t", rows)
        sync_unmount_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id0 = hunk_chunk_ids[0]
        assert_items_equal(get("#{}/@hunk_chunk_refs".format(store_chunk_id)), [
            {"chunk_id": hunk_chunk_id0, "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": hunk_erasure_codec},
        ])

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 1
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 10
        assert hunk_statistics["referenced_hunk_count"] == 10
        assert hunk_statistics["total_hunk_length"] == 260
        assert hunk_statistics["total_referenced_hunk_length"] == 260

        sync_mount_table("//tmp/t")
        delete_rows("//tmp/t", keys[1:])
        sync_unmount_table("//tmp/t")

        sync_mount_table("//tmp/t")

        chunk_ids_before_compaction1 = get("//tmp/t/@chunk_ids")
        assert len(chunk_ids_before_compaction1) == 3

        set("//tmp/t/@min_data_ttl", 0)
        set("//tmp/t/@min_data_versions", 1)
        set("//tmp/t/@forced_store_compaction_revision", 1)
        remount_table("//tmp/t")

        def _check1():
            chunk_ids = get("//tmp/t/@chunk_ids")
            return chunk_ids != chunk_ids_before_compaction1 and len(chunk_ids) == 2
        wait(_check1)

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id1 = hunk_chunk_ids[0]
        assert hunk_chunk_id0 == hunk_chunk_id1
        assert_items_equal(get("#{}/@hunk_chunk_refs".format(store_chunk_id)), [
            {"chunk_id": hunk_chunk_id1, "hunk_count": 1, "total_hunk_length": 26, "erasure_codec": hunk_erasure_codec},
        ])

        chunk_ids_before_compaction2 = get("//tmp/t/@chunk_ids")
        assert len(chunk_ids_before_compaction2) == 2

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 1
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 10
        assert hunk_statistics["referenced_hunk_count"] == 1
        assert hunk_statistics["total_hunk_length"] == 260
        assert hunk_statistics["total_referenced_hunk_length"] == 26

        set("//tmp/t/@forced_store_compaction_revision", 1)
        remount_table("//tmp/t")

        def _check2():
            chunk_ids = get("//tmp/t/@chunk_ids")
            return chunk_ids != chunk_ids_before_compaction2 and len(chunk_ids) == 2
        wait(_check2)

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id2 = hunk_chunk_ids[0]
        assert hunk_chunk_id1 != hunk_chunk_id2
        assert_items_equal(get("#{}/@hunk_chunk_refs".format(store_chunk_id)), [
            {"chunk_id": hunk_chunk_id2, "hunk_count": 1, "total_hunk_length": 26, "erasure_codec": hunk_erasure_codec},
        ])
        assert get("#{}/@hunk_count".format(hunk_chunk_id2)) == 1
        assert get("#{}/@total_hunk_length".format(hunk_chunk_id2)) == 26

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["hunk_chunk_count"] == 1
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["hunk_count"] == 1
        assert hunk_statistics["referenced_hunk_count"] == 1
        assert hunk_statistics["total_hunk_length"] == 26
        assert hunk_statistics["total_referenced_hunk_length"] == 26

    @authors("ifsmirnov")
    @pytest.mark.parametrize("in_memory_mode", ["compressed", "uncompressed"])
    def test_hunks_not_counted_in_tablet_static(self, in_memory_mode):
        def _check_account_resource_usage(expected_memory_usage):
            return \
                get("//sys/accounts/tmp/@resource_usage/tablet_static_memory") == expected_memory_usage and \
                get("//sys/tablet_cell_bundles/default/@resource_usage/tablet_static_memory") == expected_memory_usage

        sync_create_cells(1)
        self._create_table()

        set("//tmp/t/@in_memory_mode", in_memory_mode)
        sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        sync_flush_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1
        hunk_chunk_id = hunk_chunk_ids[0]

        compressed_size = get("#{}/@compressed_data_size".format(store_chunk_id))
        uncompressed_size = get("#{}/@uncompressed_data_size".format(store_chunk_id))
        hunk_compressed_size = get("#{}/@compressed_data_size".format(hunk_chunk_id))
        hunk_uncompressed_size = get("#{}/@uncompressed_data_size".format(hunk_chunk_id))

        def _validate_tablet_statistics():
            tablet_statistics = get("//tmp/t/@tablet_statistics")
            return \
                tablet_statistics["compressed_data_size"] == compressed_size + hunk_compressed_size and \
                tablet_statistics["uncompressed_data_size"] == uncompressed_size + hunk_uncompressed_size and \
                tablet_statistics["hunk_compressed_data_size"] == hunk_compressed_size and \
                tablet_statistics["hunk_uncompressed_data_size"] == hunk_uncompressed_size
        wait(_validate_tablet_statistics)

        memory_size = compressed_size if in_memory_mode == "compressed" else uncompressed_size

        assert get("//tmp/t/@tablet_statistics/memory_size") == memory_size

        wait(lambda: _check_account_resource_usage(memory_size))

        sync_unmount_table("//tmp/t")
        wait(lambda: _check_account_resource_usage(0))

    @authors("gritukan")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_type", ["inline", "chunk"])
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_hunks_in_operation(self, chunk_format, hunk_type, hunk_erasure_codec):
        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, hunk_erasure_codec=hunk_erasure_codec)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")
        sync_mount_table("//tmp/t")

        if hunk_type == "inline":
            rows = [{"key": i, "value": "value" + str(i)} for i in range(10)]
        else:
            rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        sync_unmount_table("//tmp/t")

        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        if hunk_type == "inline":
            assert len(hunk_chunk_ids) == 0
        else:
            assert len(hunk_chunk_ids) == 1

        create("table", "//tmp/t_out")
        map(
            in_="//tmp/t",
            out="//tmp/t_out",
            command="cat",
        )
        assert read_table("//tmp/t_out") == rows

    @authors("akozhikhov")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    def test_lookup_any_value_with_hunks(self, chunk_format):
        schema = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "any", "max_inline_hunk_size": 10},
        ]

        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, schema=schema)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", self.ROWS_WITH_VARIOUS_ANY_VALUES)
        sync_flush_table("//tmp/t")

        assert lookup_rows("//tmp/t", self.KEYS_OF_ROWS_WITH_VARIOUS_ANY_VALUES) == self.ROWS_WITH_VARIOUS_ANY_VALUES

    @authors("babenko")
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    def test_any_value_with_hunks_from_static(self, optimize_for):
        schema = make_schema(
            [
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "value", "type": "any", "max_inline_hunk_size": 10},
            ],
            unique_keys=True,
        )

        create("table", "//tmp/t", attributes={
            "schema": schema,
            "optimize_for": optimize_for,
        })

        write_table("//tmp/t", self.ROWS_WITH_VARIOUS_ANY_VALUES)
        assert read_table("//tmp/t") == self.ROWS_WITH_VARIOUS_ANY_VALUES

        alter_table("//tmp/t", dynamic=True)

        sync_create_cells(1)
        sync_mount_table("//tmp/t")

        assert lookup_rows("//tmp/t", self.KEYS_OF_ROWS_WITH_VARIOUS_ANY_VALUES) == self.ROWS_WITH_VARIOUS_ANY_VALUES
        assert read_table("//tmp/t") == self.ROWS_WITH_VARIOUS_ANY_VALUES

    @authors("gritukan")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_type", ["inline", "chunk"])
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_hunks_in_operation_any_value(self, chunk_format, hunk_type, hunk_erasure_codec):
        schema = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "any", "max_inline_hunk_size": 10},
        ]

        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, schema=schema, hunk_erasure_codec=hunk_erasure_codec)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")
        sync_mount_table("//tmp/t")

        if hunk_type == "inline":
            rows = [{"key": i, "value": "value" + str(i)} for i in range(10)]
        else:
            rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        sync_unmount_table("//tmp/t")

        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        if hunk_type == "inline":
            assert len(hunk_chunk_ids) == 0
        else:
            assert len(hunk_chunk_ids) == 1

        create("table", "//tmp/t_out")
        map(
            in_="//tmp/t",
            out="//tmp/t_out",
            command="cat",
        )
        assert read_table("//tmp/t_out") == rows

    @authors("babenko")
    @pytest.mark.parametrize("chunk_format", HUNK_COMPATIBLE_CHUNK_FORMATS)
    @pytest.mark.parametrize("hunk_erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_alter_to_hunks(self, chunk_format, hunk_erasure_codec):
        sync_create_cells(1)
        self._create_table(chunk_format=chunk_format, max_inline_hunk_size=None, hunk_erasure_codec=hunk_erasure_codec)
        if chunk_format == "table_versioned_indexed":
            self._enable_hash_chunk_index("//tmp/t")
        sync_mount_table("//tmp/t")
        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows1)
        sync_unmount_table("//tmp/t")

        assert len(self._get_store_chunk_ids("//tmp/t")) == 1
        assert len(self._get_hunk_chunk_ids("//tmp/t")) == 0

        alter_table("//tmp/t", schema=self._get_table_schema(schema=self.SCHEMA, max_inline_hunk_size=10))

        sync_mount_table("//tmp/t")
        rows2 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10, 20)]
        insert_rows("//tmp/t", rows2)

        assert_items_equal(select_rows("* from [//tmp/t]"), rows1 + rows2)

        sync_unmount_table("//tmp/t")

        assert len(self._get_store_chunk_ids("//tmp/t")) == 2
        assert len(self._get_hunk_chunk_ids("//tmp/t")) == 1

    @authors("babenko")
    def test_alter_must_preserve_hunks(self):
        self._create_table()
        with pytest.raises(YtError):
            alter_table("//tmp/t", schema=self._get_table_schema(schema=self.SCHEMA, max_inline_hunk_size=None))

    @authors("akozhikhov")
    def test_hunks_with_filtered_columns(self):
        sync_create_cells(1)
        self._create_table()
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", rows)
        sync_flush_table("//tmp/t")

        def _check(column_names):
            row = [{column: rows[0][column] for column in column_names}]
            assert_items_equal(
                lookup_rows("//tmp/t", [{"key": 0}], column_names=column_names),
                row)
            assert_items_equal(
                select_rows("{} from [//tmp/t] where value = \"{}\"".format(
                    ", ".join(column_names),
                    rows[0]["value"])),
                row)

        _check(["key"])
        _check(["value"])
        _check(["value", "key"])

    @authors("akozhikhov")
    def test_hunks_profiling_flush(self):
        sync_create_cells(1)
        self._create_table()
        set("//tmp/t/@enable_hunk_columnar_profiling", True)
        sync_mount_table("//tmp/t")

        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(5)]
        rows2 = [{"key": i, "value": "value" + str(i)} for i in range(5, 15)]
        insert_rows("//tmp/t", rows1 + rows2)

        chunk_data_weight = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_writer/data_weight",
            tags={"method": "store_flush"})
        hunk_chunk_data_weight = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_writer/hunks/data_weight",
            tags={"method": "store_flush"})

        inline_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_writer/hunks/inline_value_count",
            tags={"column": "value", "method": "store_flush"})
        ref_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_writer/hunks/ref_value_count",
            tags={"column": "value", "method": "store_flush"})

        sync_flush_table("//tmp/t")

        wait(lambda: chunk_data_weight.get_delta() > 0)
        wait(lambda: hunk_chunk_data_weight.get_delta() > 0)

        wait(lambda: inline_hunk_value_count.get_delta() == 10)
        wait(lambda: ref_hunk_value_count.get_delta() == 5)

    @authors("akozhikhov")
    def test_hunks_profiling_compaction(self):
        sync_create_cells(1)
        self._create_table(max_inline_hunk_size=10)
        set("//tmp/t/@enable_hunk_columnar_profiling", True)
        sync_mount_table("//tmp/t")

        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(5)]
        rows2 = [{"key": i, "value": "value" + str(i)} for i in range(5, 15)]
        insert_rows("//tmp/t", rows1 + rows2)

        reader_hunk_chunk_transmitted = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_reader/hunks/chunk_reader_statistics/data_bytes_transmitted",
            tags={"method": "compaction"})
        reader_hunk_chunk_data_weight = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_reader/hunks/data_weight",
            tags={"method": "compaction"})

        reader_inline_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_reader/hunks/inline_value_count",
            tags={"column": "value"})
        reader_ref_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_reader/hunks/ref_value_count",
            tags={"column": "value"})

        writer_chunk_data_weight = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_writer/data_weight",
            tags={"method": "compaction"})
        writer_hunk_chunk_data_weight = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_writer/hunks/data_weight",
            tags={"method": "compaction"})

        writer_inline_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_writer/hunks/inline_value_count",
            tags={"column": "value", "method": "compaction"})
        writer_ref_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="chunk_writer/hunks/ref_value_count",
            tags={"column": "value", "method": "compaction"})

        sync_unmount_table("//tmp/t")
        alter_table("//tmp/t", schema=self._get_table_schema(schema=self.SCHEMA, max_inline_hunk_size=30))
        sync_mount_table("//tmp/t")

        sync_compact_table("//tmp/t")

        wait(lambda: reader_hunk_chunk_transmitted.get_delta() > 0)
        wait(lambda: reader_hunk_chunk_data_weight.get_delta() > 0)

        wait(lambda: reader_inline_hunk_value_count.get_delta() == 10)
        wait(lambda: reader_ref_hunk_value_count.get_delta() == 5)

        wait(lambda: writer_chunk_data_weight.get_delta() > 0)
        assert writer_hunk_chunk_data_weight.get_delta() == 0

        wait(lambda: writer_inline_hunk_value_count.get_delta() == 15)
        assert writer_ref_hunk_value_count.get_delta() == 0

    @authors("akozhikhov")
    def test_hunks_profiling_lookup(self):
        sync_create_cells(1)
        self._create_table()
        set("//tmp/t/@enable_hunk_columnar_profiling", True)
        sync_mount_table("//tmp/t")

        keys1 = [{"key": i} for i in range(10)]
        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(5)]
        keys2 = [{"key": i} for i in range(10, 20)]
        rows2 = [{"key": i, "value": "value" + str(i)} for i in range(5, 15)]
        insert_rows("//tmp/t", rows1 + rows2)
        sync_flush_table("//tmp/t")

        hunk_chunk_transmitted = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="lookup/hunks/chunk_reader_statistics/data_bytes_transmitted")
        hunk_chunk_data_weight = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="lookup/hunks/data_weight")

        inline_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="lookup/hunks/inline_value_count")
        ref_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="lookup/hunks/ref_value_count")

        columnar_inline_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="lookup/hunks/inline_value_count",
            tags={"column": "value"})
        columnar_ref_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="lookup/hunks/ref_value_count",
            tags={"column": "value"})

        backend_read_request_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="lookup/hunks/backend_read_request_count")

        assert_items_equal(lookup_rows("//tmp/t", keys1 + keys2), rows1 + rows2)

        wait(lambda: hunk_chunk_transmitted.get_delta() > 0)
        wait(lambda: hunk_chunk_data_weight.get_delta() > 0)

        wait(lambda: inline_hunk_value_count.get_delta() == 20)
        wait(lambda: ref_hunk_value_count.get_delta() == 10)

        wait(lambda: columnar_inline_hunk_value_count.get_delta() == 10)
        wait(lambda: columnar_ref_hunk_value_count.get_delta() == 5)

        # Multiple requests due to max_inflight_fragment_* constraints.
        wait(lambda: backend_read_request_count.get_delta() > 2)

    @authors("akozhikhov")
    def test_hunks_profiling_select(self):
        sync_create_cells(1)
        self._create_table()
        set("//tmp/t/@enable_hunk_columnar_profiling", True)
        sync_mount_table("//tmp/t")

        rows1 = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(5)]
        rows2 = [{"key": i, "value": "value" + str(i)} for i in range(5, 15)]
        insert_rows("//tmp/t", rows1 + rows2)
        sync_flush_table("//tmp/t")

        hunk_chunk_transmitted = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="select/hunks/chunk_reader_statistics/data_bytes_transmitted")
        hunk_chunk_data_weight = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="select/hunks/data_weight")

        inline_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="select/hunks/inline_value_count",
            tags={"column": "value"})
        ref_hunk_value_count = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="select/hunks/ref_value_count",
            tags={"column": "value"})

        assert_items_equal(select_rows("* from [//tmp/t]"), rows1 + rows2)

        wait(lambda: hunk_chunk_transmitted.get_delta() > 0)
        wait(lambda: hunk_chunk_data_weight.get_delta() > 0)

        wait(lambda: inline_hunk_value_count.get_delta() == 10)
        wait(lambda: ref_hunk_value_count.get_delta() == 5)

    @authors("ifsmirnov")
    def test_small_hunk_compaction(self):
        sync_create_cells(1)
        self._create_table()
        set("//tmp/t/@max_hunk_compaction_chunk_count", 2)
        sync_mount_table("//tmp/t")

        for i in range(3):
            insert_rows("//tmp/t", [{"key": i, "value": "verylongvalue" + str(i)}])
            sync_flush_table("//tmp/t")

        wait(lambda: get("//tmp/t/@chunk_count") == 1 + 2)
        (store_chunk_id, ) = self._get_store_chunk_ids("//tmp/t")
        assert len(get("#{}/@hunk_chunk_refs".format(store_chunk_id))) == 2

        set("//tmp/t/@forced_store_compaction_revision", 1)
        remount_table("//tmp/t")
        wait(lambda: get("//tmp/t/@chunk_count") == 1 + 1)
        (store_chunk_id, ) = self._get_store_chunk_ids("//tmp/t")
        assert len(get("#{}/@hunk_chunk_refs".format(store_chunk_id))) == 1

    @authors("akozhikhov")
    def test_no_small_hunk_compaction(self):
        sync_create_cells(1)
        self._create_table()
        set("//tmp/t/@max_hunk_compaction_size", 5)
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key": 0, "value": "0" * 20}])
        sync_flush_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": 1, "value": "1" * 20}])
        sync_flush_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 2
        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 2

        set("//tmp/t/@forced_store_compaction_revision", 1)
        remount_table("//tmp/t")

        def _check():
            new_store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
            if len(new_store_chunk_ids) != 1:
                return False
            return new_store_chunk_ids[0] not in store_chunk_ids

        wait(lambda: _check())

        assert hunk_chunk_ids == self._get_hunk_chunk_ids("//tmp/t")

    @authors("akozhikhov")
    def test_hunk_chunks_orchid(self):
        sync_create_cells(1)
        self._create_table()
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key": 0, "value": "0" * 20}])
        sync_flush_table("//tmp/t")

        hunk_chunk_ids = self._get_hunk_chunk_ids("//tmp/t")
        assert len(hunk_chunk_ids) == 1

        tablet_info = get("//tmp/t/@tablets/0")
        hunk_chunks_info = get("//sys/cluster_nodes/{}/orchid/tablet_cells/{}/tablets/{}/hunk_chunks/{}".format(
            tablet_info["cell_leader_address"],
            tablet_info["cell_id"],
            tablet_info["tablet_id"],
            hunk_chunk_ids[0],
        ))

        assert hunk_chunks_info["referenced_total_hunk_length"] == 20
        assert hunk_chunks_info["total_hunk_length"] == 20
        assert hunk_chunks_info["hunk_count"] == 1
        assert hunk_chunks_info["referenced_hunk_count"] == 1
        assert hunk_chunks_info["store_ref_count"] == 1
        assert not hunk_chunks_info["dangling"]

    @authors("babenko")
    def test_hunk_erasure_codec_cannot_be_set_for_nontable(self):
        create("file", "//tmp/f")
        assert not exists("//tmp/f/@hunk_erasure_codec")
        with pytest.raises(YtError):
            set("//tmp/f/@hunk_erasure_codec", "isa_lrc_12_2_2")

    @authors("babenko", "gritukan")
    def test_hunk_erasure_codec_for_table(self):
        create("table", "//tmp/t")

        assert get("//tmp/t/@hunk_erasure_codec") == "none"

        set("//tmp/t/@hunk_erasure_codec", "isa_lrc_12_2_2")
        assert get("//tmp/t/@hunk_erasure_codec") == "isa_lrc_12_2_2"

        tx = start_transaction()
        set("//tmp/t/@hunk_erasure_codec", "reed_solomon_3_3", tx=tx)
        assert get("//tmp/t/@hunk_erasure_codec") == "isa_lrc_12_2_2"
        assert get("//tmp/t/@hunk_erasure_codec", tx=tx) == "reed_solomon_3_3"

        commit_transaction(tx)
        assert get("//tmp/t/@hunk_erasure_codec") == "reed_solomon_3_3"

        set("//tmp/t/@hunk_erasure_codec", "none")
        assert get("//tmp/t/@hunk_erasure_codec") == "none"

    @authors("babenko")
    def test_hunk_erasure_codec_inheritance(self):
        create("map_node", "//tmp/m")
        set("//tmp/m/@hunk_erasure_codec", "isa_lrc_12_2_2")

        create("file", "//tmp/m/f")
        assert not exists("//tmp/m/f/@hunk_erasure_codec")

        create("table", "//tmp/m/t")
        assert get("//tmp/m/t/@hunk_erasure_codec") == "isa_lrc_12_2_2"

    @authors("akozhikhov")
    def test_hedging_manager_sensors(self):
        sync_create_cells(1)
        self._create_table()
        set("//tmp/t/@hunk_chunk_reader/hedging_manager", {"max_backup_request_ratio": 0.5})
        sync_mount_table("//tmp/t")

        keys = [{"key": i} for i in range(5)]
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(5)]
        insert_rows("//tmp/t", rows)
        sync_flush_table("//tmp/t")

        request_counter = profiler_factory().at_tablet_node("//tmp/t").counter(
            name="hunks/hedging_manager/primary_request_count")

        assert_items_equal(lookup_rows("//tmp/t", keys), rows)

        time.sleep(1)

        assert_items_equal(lookup_rows("//tmp/t", keys), rows)

        wait(lambda: request_counter.get_delta() > 0)

    BLOB_HUNK_PAYLOAD = [
        {"payload": b"abcdefghijklmnopqrstuvwxyz"}
    ]

    def _write_blob_hunks(self, erasure_codec):
        create("file", "//tmp/f", attributes={
            "erasure_codec": erasure_codec,
            "enable_striped_erasure": True
        })
        assert len(self.BLOB_HUNK_PAYLOAD) == 1
        write_file("//tmp/f", self.BLOB_HUNK_PAYLOAD[0]["payload"])
        return get_singular_chunk_id("//tmp/f")

    def _prepare_hunk_read_requests(self, requests, erasure_codec, payload):
        if erasure_codec != "none":
            for request in requests:
                request["erasure_codec"] = erasure_codec
                if "block_size" not in request:
                    request["block_size"] = len(payload[request["block_index"]]["payload"])

    @authors("babenko")
    @pytest.mark.parametrize("erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_blob_hunk_read(self, erasure_codec):
        chunk_id = self._write_blob_hunks(erasure_codec)
        requests = [
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 0, "length": 26},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 5, "length": 0},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 10, "length": 3}
        ]
        self._prepare_hunk_read_requests(requests, erasure_codec, self.BLOB_HUNK_PAYLOAD)
        responses = read_hunks(requests, parse_header=False)
        assert len(responses) == 3
        assert responses[0]["payload"] == "abcdefghijklmnopqrstuvwxyz"
        assert responses[1]["payload"] == ""
        assert responses[2]["payload"] == "klm"

    @authors("babenko")
    @pytest.mark.parametrize("erasure_codec", ["none", "isa_reed_solomon_6_3"])
    def test_blob_hunk_read_failed(self, erasure_codec):
        chunk_id = self._write_blob_hunks(erasure_codec)
        chunk_id = get_singular_chunk_id("//tmp/f")
        requests = [
            {"chunk_id": "1-2-3-4", "block_index": 0, "block_offset": 0, "length": 0},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": -100, "length": 0},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 0, "length": -1},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 100, "length": 1},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 0, "length": 100},
            # TODO(babenko,gritukan): consider failing these (zero-length!) requests.
            # {"chunk_id": chunk_id, "block_index": 1, "block_offset": 0, "length": 0},
            # {"chunk_id": chunk_id, "block_index": 0, "block_offset": 100, "length": 0},
        ]
        self._prepare_hunk_read_requests(requests, erasure_codec, self.BLOB_HUNK_PAYLOAD)
        for request in requests:
            with pytest.raises(YtError):
                read_hunks([request], parse_header=False)

    JOURNAL_HUNK_PAYLOAD = [
        {"payload": "abcdefghijklmnopqrstuvwxyz"},
        {"payload": "0123456789"},
        {"payload": "abababababababab"}
    ]

    def _write_journal_hunks(self, erasure_codec, replication_factor, read_quorum, write_quorum):
        create("journal", "//tmp/j", attributes={
            "erasure_codec": erasure_codec,
            "replication_factor": replication_factor,
            "read_quorum": read_quorum,
            "write_quorum": write_quorum
        })
        write_journal("//tmp/j", self.JOURNAL_HUNK_PAYLOAD)
        return get_singular_chunk_id("//tmp/j")

    @authors("babenko")
    @pytest.mark.parametrize("erasure_codec,replication_factor,read_quorum,write_quorum",
                             [("none", 3, 2, 2), ("isa_reed_solomon_3_3", 1, 4, 5)])
    def test_journal_hunk_read(self, erasure_codec, replication_factor, read_quorum, write_quorum):
        chunk_id = self._write_journal_hunks(erasure_codec, replication_factor, read_quorum, write_quorum)
        requests = [
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 0, "length": 26},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 3, "length": 4},
            {"chunk_id": chunk_id, "block_index": 1, "block_offset": 2, "length": 5},
            {"chunk_id": chunk_id, "block_index": 2, "block_offset": 1, "length": 1},
            {"chunk_id": chunk_id, "block_index": 1, "block_offset": 7, "length": 0}
        ]
        self._prepare_hunk_read_requests(requests, erasure_codec, self.JOURNAL_HUNK_PAYLOAD)
        responses = read_hunks(requests, parse_header=False)
        assert len(responses) == 5
        assert responses[0]["payload"] == "abcdefghijklmnopqrstuvwxyz"
        assert responses[1]["payload"] == "defg"
        assert responses[2]["payload"] == "23456"
        assert responses[3]["payload"] == "b"
        assert responses[4]["payload"] == ""

    @authors("babenko")
    @pytest.mark.parametrize("erasure_codec,replication_factor,read_quorum,write_quorum",
                             [("none", 3, 2, 2), ("isa_reed_solomon_3_3", 1, 4, 5)])
    def test_journal_hunk_read_failed(self, erasure_codec, replication_factor, read_quorum, write_quorum):
        chunk_id = self._write_journal_hunks(erasure_codec, replication_factor, read_quorum, write_quorum)
        requests = [
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": -1, "length": 1},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 100, "length": 1},
            {"chunk_id": chunk_id, "block_index": 0, "block_offset": 0, "length": -1},
            {"chunk_id": chunk_id, "block_index": 100, "block_offset": 0, "length": 1, "block_size": 100},
            # TODO(babenko,gritukan): consider failing these (zero-length!) requests.
            # {"chunk_id": chunk_id, "block_index": 100, "block_offset": 0, "length": 0},
            # {"chunk_id": chunk_id, "block_index": 0, "block_offset": 100, "length": 0},
        ]
        self._prepare_hunk_read_requests(requests, erasure_codec, self.JOURNAL_HUNK_PAYLOAD)
        for request in requests:
            with pytest.raises(YtError):
                read_hunks([request], parse_header=False)

    @authors("akozhikhov")
    @pytest.mark.parametrize("chunk_format", ["table_unversioned_schemaless_horizontal", "table_unversioned_columnar"])
    @pytest.mark.parametrize("in_memory_mode", ["none", "compressed", "uncompressed"])
    @pytest.mark.parametrize("versioned", [False, True])
    @pytest.mark.parametrize("enable_lookup_hash_table", [False, True])
    def test_unversioned_lookup_all_formats(self, chunk_format, in_memory_mode, versioned, enable_lookup_hash_table):
        if enable_lookup_hash_table and in_memory_mode != "uncompressed":
            return

        self._create_table(chunk_format=chunk_format, max_inline_hunk_size=5, dynamic=False)

        rows = [{"key": 0, "value": "0"}, {"key": 1, "value": "1111111111"}]
        write_table("//tmp/t", rows)
        alter_table("//tmp/t", dynamic=True)
        set("//tmp/t/@in_memory_mode", in_memory_mode)
        set("//tmp/t/@enable_lookup_hash_table", enable_lookup_hash_table)
        set("//tmp/t/@optimize_for", "lookup")
        set("//tmp/t/@chunk_format", "table_versioned_simple")

        sync_create_cells(1)
        sync_mount_table("//tmp/t")
        if in_memory_mode != "none":
            wait(lambda: get("//tmp/t/@preload_state") == "complete")

        read_rows = lookup_rows("//tmp/t", [{"key": 0}, {"key": 1}, {"key": 2}], versioned=versioned)
        if not versioned:
            assert read_rows == rows
        else:
            assert len(read_rows) == 2

            def _check_row(actual, expected):
                assert actual["key"] == expected["key"]
                value = actual["value"]
                assert len(value) == 1
                assert str(value[0]) == expected["value"]
            _check_row(read_rows[0], rows[0])

################################################################################


class TestOrderedDynamicTablesHunks(TestSortedDynamicTablesBase):
    NUM_TEST_PARTITIONS = 7

    NUM_NODES = 5

    NUM_SCHEDULERS = 1

    SCHEMA = [
        {"name": "key", "type": "int64"},
        {"name": "value", "type": "string"},
    ]

    DELTA_DYNAMIC_MASTER_CONFIG = {
        "tablet_manager": {
            "enable_hunks": True
        }
    }

    # Do not allow multiple erasure parts per node.
    DELTA_MASTER_CONFIG = {}

    def _get_table_schema(self, schema, max_inline_hunk_size):
        schema[1]["max_inline_hunk_size"] = max_inline_hunk_size
        return schema

    def _create_table(self, optimize_for="lookup", max_inline_hunk_size=10, hunk_erasure_codec="none", schema=SCHEMA):
        self._create_simple_table("//tmp/t",
                                  schema=self._get_table_schema(schema, max_inline_hunk_size),
                                  enable_dynamic_store_read=False,
                                  hunk_chunk_reader={
                                      "max_hunk_count_per_read": 2,
                                      "max_total_hunk_length_per_read": 60,
                                      "fragment_read_hedging_delay": 1
                                  },
                                  hunk_chunk_writer={
                                      "desired_block_size": 50,
                                  },
                                  max_hunk_compaction_garbage_ratio=0.5,
                                  enable_lsm_verbose_logging=True,
                                  optimize_for=optimize_for,
                                  hunk_erasure_codec=hunk_erasure_codec)

    def _get_store_chunk_ids(self, path):
        chunk_ids = get(path + "/@chunk_ids")
        return [chunk_id for chunk_id in chunk_ids if get("#{}/@chunk_type".format(chunk_id)) == "table"]

    def _get_active_store_id(self, hunk_storage, tablet_index=0):
        tablets = get("{}/@tablets".format(hunk_storage))
        tablet_id = tablets[tablet_index]["tablet_id"]
        wait(lambda: exists("//sys/tablets/{}/orchid/active_store_id".format(tablet_id)))
        return get("//sys/tablets/{}/orchid/active_store_id".format(tablet_id))

    def _get_active_store_orchid(self, hunk_storage, tablet_index=0):
        tablets = get("{}/@tablets".format(hunk_storage))
        tablet_id = tablets[tablet_index]["tablet_id"]
        get("//sys/tablets/{}/orchid".format(tablet_id))

    @authors("aleksandra-zh")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_flush_inline(self, optimize_for):
        sync_create_cells(1)
        self._create_table(optimize_for=optimize_for)

        sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": "value" + str(i)} for i in range(10)]
        insert_rows("//tmp/t", rows)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        chunk_ids = get("//tmp/t/@chunk_ids")
        get("#{}/@chunk_format".format(chunk_ids[0]))

        assert_items_equal(select_rows("key, value from [//tmp/t]"), rows)
        sync_unmount_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1

        assert get("#{}/@hunk_chunk_refs".format(store_chunk_ids[0])) == []

        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("key, value from [//tmp/t]"), rows)

    @authors("aleksandra-zh")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_flush_to_hunk_chunk(self, optimize_for):
        sync_create_cells(1)
        self._create_table(optimize_for=optimize_for)

        create("hunk_storage", "//tmp/h")
        set("//tmp/t/@hunk_storage_node", "//tmp/h")
        sync_mount_table("//tmp/h")

        sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        hunk_store_id = self._get_active_store_id("//tmp/h")

        for i in range(len(rows)):
            rows[i]["$tablet_index"] = 0
            rows[i]["$row_index"] = i

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        sync_unmount_table("//tmp/h")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]

        assert get("#{}/@ref_counter".format(store_chunk_id)) == 1

        assert get("#{}/@hunk_chunk_refs".format(store_chunk_id)) == [
            {"chunk_id": hunk_store_id, "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": "none"}
        ]

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["referenced_hunk_count"] == 10
        assert hunk_statistics["total_referenced_hunk_length"] == 260

        sync_mount_table("//tmp/t")
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        remove("//tmp/t")
        wait(lambda: not exists("#{}".format(store_chunk_id)))

    @authors("aleksandra-zh")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    @pytest.mark.parametrize("enable_dynamic_store_read", [True, False])
    def test_journal_hunk_chunk_parents(self, optimize_for, enable_dynamic_store_read):
        set("//sys/@config/tablet_manager/enable_dynamic_store_read_by_default", enable_dynamic_store_read)

        sync_create_cells(1)
        self._create_table(optimize_for=optimize_for)
        set("//tmp/t/@enable_dynamic_store_read", enable_dynamic_store_read)

        create("hunk_storage", "//tmp/h", attributes={
            "store_rotation_period": 20000,
            "store_removal_grace_period": 4000,
        })
        set("//tmp/t/@hunk_storage_node", "//tmp/h")
        sync_mount_table("//tmp/h")

        sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        for i in range(len(rows)):
            rows[i]["$tablet_index"] = 0
            rows[i]["$row_index"] = i

        hunk_store_id = self._get_active_store_id("//tmp/h")
        set("//sys/cluster_nodes/@config", {"%true": {
            "tablet_node": {"hunk_lock_manager": {"hunk_store_extra_lifetime": 123, "unlock_check_period": 127}}
        }})

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]

        assert get("#{}/@ref_counter".format(store_chunk_id)) == 1

        assert get("#{}/@hunk_chunk_refs".format(store_chunk_id)) == [
            {"chunk_id": hunk_store_id, "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": "none"}
        ]

        hunk_store_parents = get("#{}/@owning_nodes".format(hunk_store_id))
        assert sorted(hunk_store_parents) == ["//tmp/h", "//tmp/t"]

        sync_mount_table("//tmp/t")
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        set("//tmp/h/@store_removal_grace_period", 100)
        set("//tmp/h/@store_rotation_period", 500)
        wait(lambda: get("#{}/@owning_nodes".format(hunk_store_id)) == ["//tmp/t"])

        remove("//tmp/t")
        wait(lambda: not exists("#{}".format(store_chunk_id)))

    @authors("aleksandra-zh")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_restart_preserves_locks(self, optimize_for):
        sync_create_cells(1)
        self._create_table(optimize_for=optimize_for)

        create("hunk_storage", "//tmp/h", attributes={
            "store_removal_grace_period": 100,
        })
        set("//tmp/t/@hunk_storage_node", "//tmp/h")
        sync_mount_table("//tmp/h")

        sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": "value" + str(i) + "x" * 20} for i in range(10)]
        insert_rows("//tmp/t", rows)
        for i in range(len(rows)):
            rows[i]["$tablet_index"] = 0
            rows[i]["$row_index"] = i

        hunk_store_id = self._get_active_store_id("//tmp/h")
        set("//sys/cluster_nodes/@config", {"%true": {
            "tablet_node": {"hunk_lock_manager": {"hunk_store_extra_lifetime": 123, "unlock_check_period": 127}}
        }})

        build_snapshot(cell_id=None)
        with Restarter(self.Env, NODES_SERVICE):
            pass

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        sync_unmount_table("//tmp/h")

        store_chunk_ids = self._get_store_chunk_ids("//tmp/t")
        assert len(store_chunk_ids) == 1
        store_chunk_id = store_chunk_ids[0]

        assert get("#{}/@ref_counter".format(store_chunk_id)) == 1

        assert get("#{}/@hunk_chunk_refs".format(store_chunk_id)) == [
            {"chunk_id": hunk_store_id, "hunk_count": 10, "total_hunk_length": 260, "erasure_codec": "none"}
        ]

        hunk_statistics = get("//tmp/t/@hunk_statistics")
        assert hunk_statistics["store_chunk_count"] == 1
        assert hunk_statistics["referenced_hunk_count"] == 10
        assert hunk_statistics["total_referenced_hunk_length"] == 260

        sync_mount_table("//tmp/t")
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        remove("//tmp/t")
        wait(lambda: not exists("#{}".format(store_chunk_id)))
