from yt_env_setup import YTEnvSetup, Restarter, SCHEDULERS_SERVICE, is_asan_build

from yt_commands import (
    authors, print_debug, wait, create, get, set, exists,
    create_account, read_table,
    write_table, map, reduce, merge, sync_create_cells, sync_mount_table)

from yt_type_helpers import normalize_schema, make_schema

from yt.common import YtError

import yt.yson as yson

import pytest

from time import sleep

##################################################################


class TestSchedulerAutoMerge(YTEnvSetup):
    NUM_TEST_PARTITIONS = 8
    NUM_MASTERS = 1
    NUM_NODES = 4
    NUM_SCHEDULERS = 1
    USE_DYNAMIC_TABLES = True
    ENABLE_BULK_INSERT = True

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "watchers_update_period": 100,
            "operations_update_period": 10,
            "running_jobs_update_period": 10,
        },
    }

    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "snapshot_period": 3000,
            "operations_update_period": 10,
            "chunk_unstage_period": 10,
        }
    }

    DELTA_MASTER_CONFIG = {
        "chunk_manager": {
            "allow_multiple_erasure_parts_per_node": True,
        },
    }

    DELTA_NODE_CONFIG = {
        "exec_agent": {
            "job_controller": {
                "resource_limits": {
                    "user_slots": 4,
                    "cpu": 4,
                    "memory": 4 * 1024 ** 3,
                }
            }
        },
    }

    def _create_account(self, chunk_count):
        create_account("acc")
        set("//sys/accounts/acc/@resource_limits/chunk_count", chunk_count)

    def _track_and_report_peak_chunk_count(self, op, with_revive=False):
        peak_chunk_count = 0
        i = 0
        while True:
            suspended = get(op.get_path() + "/@suspended", default=False, verbose=False)
            if suspended:
                print_debug("Operation suspended, trying to re-suspend it")
                op.resume()

            state = op.get_state(verbose=False)
            if state == "completed":
                break
            if op.get_state() == "failed":
                op.track()  # this should raise an exception
            current_chunk_count = get("//sys/accounts/acc/@resource_usage/chunk_count", verbose=False)
            peak_chunk_count = max(peak_chunk_count, current_chunk_count)
            print_debug("Peak chunk count = {}, current chunk count = {}".format(peak_chunk_count, current_chunk_count))
            sleep(2)
            if with_revive:
                i += 1
                if i == 5:
                    with Restarter(self.Env, SCHEDULERS_SERVICE):
                        pass
                    i = 0
        print_debug("Peak chunk count = {}".format(peak_chunk_count))

    # Bugs in auto-merge usually lead to the operation being stuck without scheduling any new jobs.
    # This is why we use the pytest timeout decorator.
    @authors("max42")
    @pytest.mark.timeout(480)
    @pytest.mark.parametrize("op_type", ["map", "reduce"])
    @pytest.mark.skipif(is_asan_build(), reason="Test is too slow to fit into timeout")
    def test_auto_merge_does_not_stuck(self, op_type):
        create(
            "table",
            "//tmp/t_in",
            attributes={"schema": [{"name": "a", "type": "int64", "sort_order": "ascending"}]},
        )
        create("table", "//tmp/t_out")

        parameters = [
            [1, 1, 1],
            [9, 1, 1],
            [9, 9, 1],
            [9, 9, 9],
            [9, 4, 2],
            [16, 5, 3],
            [100, 28, 14],
            [9, 20, 15],
            [9, 20, 3],
        ]

        run_op = map if op_type == "map" else reduce

        for (
            row_count,
            max_intermediate_chunk_count,
            chunk_count_per_merge_job,
        ) in parameters:
            write_table(
                "//tmp/t_in",
                [{"a": i} for i in range(row_count)],
                max_row_buffer_size=1,
                table_writer={"desired_chunk_size": 1},
            )

            assert get("//tmp/t_in/@chunk_count") == row_count

            op = run_op(
                track=False,
                in_="//tmp/t_in",
                out="//tmp/t_out",
                command="cat",
                reduce_by=["a"],
                spec={
                    "auto_merge": {
                        "mode": "manual",
                        "max_intermediate_chunk_count": max_intermediate_chunk_count,
                        "chunk_count_per_merge_job": chunk_count_per_merge_job,
                    },
                    "data_size_per_job": 1,
                },
            )
            op.track()
            assert (
                get("//tmp/t_out/@chunk_count")
                == (row_count - 1) // min(chunk_count_per_merge_job, max_intermediate_chunk_count) + 1
            )
            assert get("//tmp/t_out/@row_count") == row_count

    @authors("max42")
    @pytest.mark.timeout(480)
    @pytest.mark.parametrize("op_type", ["map", "reduce"])
    def test_account_chunk_limit(self, op_type):
        self._create_account(35)

        create(
            "table",
            "//tmp/t_in",
            attributes={"schema": [{"name": "a", "type": "int64", "sort_order": "ascending"}]},
        )
        create("table", "//tmp/t_out")

        row_count = 300
        write_table(
            "//tmp/t_in",
            [{"a": i} for i in range(row_count)],
            max_row_buffer_size=1,
            table_writer={"desired_chunk_size": 1},
        )

        assert get("//tmp/t_in/@chunk_count") == row_count

        run_op = map if op_type == "map" else reduce
        op = run_op(
            track=False,
            in_="//tmp/t_in",
            out="//tmp/t_out",
            reduce_by=["a"],  # ignored for maps
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "max_intermediate_chunk_count": 35,
                    "chunk_count_per_merge_job": 20,
                    "use_intermediate_data_account": True,
                },
                "data_size_per_job": 1,
                "suspend_operation_if_account_limit_exceeded": True,
                "intermediate_data_account": "acc",
            },
        )

        self._track_and_report_peak_chunk_count(op)

        assert get("//tmp/t_out/@row_count") == row_count

    @authors("max42")
    def test_several_auto_merge_output_tables(self):
        self._create_account(20)

        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out1")
        create("table", "//tmp/t_out2")

        row_count = 100
        write_table("//tmp/t_in", [{"a": i} for i in range(row_count)])

        op = map(
            track=False,
            in_="//tmp/t_in",
            out=["//tmp/t_out1", "//tmp/t_out2"],
            command="read x; echo $x >&$(($x % 2 * 3 + 1))",
            format="<columns=[a]>schemaful_dsv",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "max_intermediate_chunk_count": 20,
                    "chunk_count_per_merge_job": 15,
                    "use_intermediate_data_account": True,
                },
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
                "data_size_per_job": 1,
                "suspend_operation_if_account_limit_exceeded": True,
                "intermediate_data_account": "acc",
            },
        )

        self._track_and_report_peak_chunk_count(op)

        assert get("//tmp/t_out1/@row_count") == row_count // 2
        assert get("//tmp/t_out2/@row_count") == row_count // 2

    @authors("max42")
    @pytest.mark.timeout(480)
    @pytest.mark.parametrize("with_revive", [True, False])
    def test_only_auto_merge_output_table(self, with_revive):
        chunk_limit = 1000 if with_revive else 20
        self._create_account(chunk_limit)

        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out1")
        create("table", "//tmp/t_out2")

        row_count = 100
        write_table("//tmp/t_in", [{"a": i} for i in range(row_count)])

        op = map(
            track=False,
            in_="//tmp/t_in",
            out=["<auto_merge=%false>//tmp/t_out1", "//tmp/t_out2"],
            command="read x; if [[ $(($x % 10)) == 0 ]]; then echo $x >&1; else echo $x >&4; fi",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "max_intermediate_chunk_count": 20,
                    "chunk_count_per_merge_job": 15,
                    "use_intermediate_data_account": True,
                },
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
                "data_size_per_job": 1,
                "suspend_operation_if_account_limit_exceeded": True,
                "intermediate_data_account": "acc",
            },
        )

        self._track_and_report_peak_chunk_count(op, with_revive=with_revive)

        assert get("//tmp/t_out1/@row_count") == row_count // 10
        assert get("//tmp/t_out2/@row_count") == row_count * 9 // 10

    @authors("max42", "ermolovd")
    @pytest.mark.timeout(240)
    def test_auto_merge_with_schema_and_append(self):
        schema_in = make_schema(
            [
                {
                    "name": "a",
                    "type": "int64",
                    "sort_order": "ascending",
                },
                {"name": "b", "type": "string"},
            ],
            unique_keys=False,
            strict=True,
        )
        create("table", "//tmp/t_in", attributes={"schema": schema_in})

        schema_out = make_schema(
            [
                {
                    "name": "a",
                    "type": "int64",
                    "required": False,
                },
                {
                    "name": "b",
                    "type": "string",
                    "required": False,
                },
            ],
            unique_keys=False,
            strict=True,
        )
        create("table", "//tmp/t_out", attributes={"schema": schema_out})

        data = [{"a": i, "b": str(i * i)} for i in range(10)]
        write_table("<append=%true>//tmp/t_in", data)

        init_content = [{"a": -1, "b": "1"}]
        write_table("<append=%true>//tmp/t_out", init_content)

        map(
            in_="//tmp/t_in",
            out=["<append=%true>//tmp/t_out"],
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "max_intermediate_chunk_count": 4,
                    "chunk_count_per_merge_job": 2,
                },
                "data_size_per_job": 1,
            },
        )

        assert get("//tmp/t_out/@row_count") == 11
        assert get("//tmp/t_out/@chunk_count") == 6
        content = read_table("//tmp/t_out")
        assert sorted(content[1:]) == sorted(data)
        assert content[:1] == init_content
        assert normalize_schema(get("//tmp/t_out/@schema")) == schema_out

    @authors("max42")
    @pytest.mark.timeout(60)
    def test_teleport_large_chunks(self):
        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out1")
        create("table", "//tmp/t_out2")
        for i in range(10):
            write_table("<append=%true>//tmp/t_in", [{"a": i}])

        # For even lines output a long random string, for odd lines output a single character.
        op = map(
            in_="//tmp/t_in",
            out=["//tmp/t_out1", "//tmp/t_out2"],
            command="read x; if [[ $(($x % 2)) == 0 ]]; then head -c 1000000 /dev/urandom | base64 -w 0; echo -ne '\n'; else echo $x; fi >&4",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "max_intermediate_chunk_count": 50,
                    "chunk_count_per_merge_job": 50,
                    "chunk_size_threshold": 100 * 1024,
                },
                "data_size_per_job": 1,
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
            },
        )
        assert get("//tmp/t_out1/@chunk_count") == 0
        assert get("//tmp/t_out2/@chunk_count") == 6
        chunk_ids = get("//tmp/t_out2/@chunk_ids")
        row_counts = []
        for chunk_id in chunk_ids:
            row_counts.append(get("#{0}/@row_count".format(chunk_id)))
        row_counts = sorted(row_counts)
        assert row_counts == [1, 1, 1, 1, 1, 5]

        data_flow = get(op.get_path() + "/@progress/data_flow")
        directions = {}
        for direction in data_flow:
            directions[(direction["source_name"], direction["target_name"])] = direction

        assert len(directions) == 3
        assert directions[("input", "map")]["job_data_statistics"]["chunk_count"] == 0
        assert directions[("input", "map")]["teleport_data_statistics"]["chunk_count"] == 10
        assert directions[("map", "auto_merge")]["job_data_statistics"]["chunk_count"] == 10
        assert directions[("map", "auto_merge")]["teleport_data_statistics"]["chunk_count"] == 0
        assert directions[("auto_merge", "output")]["job_data_statistics"]["chunk_count"] == 1
        assert directions[("auto_merge", "output")]["teleport_data_statistics"]["chunk_count"] == 5

    @authors("max42")
    @pytest.mark.timeout(60)
    def test_erasure_output(self):
        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out")
        set("//tmp/t_out/@erasure_codec", "lrc_12_2_2")
        for i in range(10):
            write_table("<append=%true>//tmp/t_in", [{"a": i}])

        map(
            in_="//tmp/t_in",
            out=["//tmp/t_out"],
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "chunk_count_per_merge_job": 2,
                    "max_intermediate_chunk_count": 100,
                },
                "data_size_per_job": 1,
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
            },
        )
        assert get("//tmp/t_out/@chunk_count") == 5
        chunk_ids = get("//tmp/t_out/@chunk_ids")
        for chunk_id in chunk_ids:
            assert get("#{0}/@erasure_codec".format(chunk_id)) == "lrc_12_2_2"

    @authors("max42")
    @pytest.mark.timeout(60)
    def test_replicated_and_compressed_output(self):
        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out")
        set("//tmp/t_out/@replication_factor", 5)
        set("//tmp/t_out/@compression_codec", "zstd_17")
        for i in range(10):
            write_table("<append=%true>//tmp/t_in", [{"a": i}])

        map(
            in_="//tmp/t_in",
            out=["//tmp/t_out"],
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "chunk_count_per_merge_job": 2,
                    "max_intermediate_chunk_count": 100,
                },
                "data_size_per_job": 1,
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
            },
        )
        assert get("//tmp/t_out/@chunk_count") == 5
        chunk_ids = get("//tmp/t_out/@chunk_ids")
        for chunk_id in chunk_ids:
            assert get("#{0}/@media/default/replication_factor".format(chunk_id)) == 5
            assert get("#{0}/@compression_codec".format(chunk_id)) == "zstd_17"

    @authors("max42")
    @pytest.mark.timeout(60)
    def test_row_count_limit_disables_auto_merge(self):
        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out")
        for i in range(10):
            write_table("<append=%true>//tmp/t_in", [{"a": i}])

        op = map(
            in_="//tmp/t_in",
            out=["<row_count_limit=5>//tmp/t_out"],
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "chunk_count_per_merge_job": 4,
                    "max_intermediate_chunk_count": 100,
                },
                "data_size_per_job": 1,
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
            },
        )
        assert get("//tmp/t_out/@chunk_count") >= 5
        assert "auto_merge_disabled" in op.get_alerts()

    @authors("max42")
    @pytest.mark.timeout(60)
    def test_sorted_output_disables_auto_merge(self):
        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out")

        schema_out = make_schema(
            [
                {
                    "name": "a",
                    "type": "string",
                    "sort_order": "ascending",
                },
            ],
            unique_keys=False,
            strict=True,
        )
        create("table", "//tmp/t_out_with_schema", attributes={"schema": schema_out})

        for i in range(10):
            write_table("<append=%true>//tmp/t_in", [{"a": i}])

        op = map(
            in_="//tmp/t_in",
            out=["<sorted_by=[a]>//tmp/t_out"],
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "chunk_count_per_merge_job": 4,
                    "max_intermediate_chunk_count": 100,
                },
                "data_size_per_job": 1,
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
            },
        )
        assert get("//tmp/t_out/@chunk_count") >= 5
        assert "auto_merge_disabled" in op.get_alerts()

        op = map(
            in_="//tmp/t_in",
            out=["//tmp/t_out_with_schema"],
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "chunk_count_per_merge_job": 4,
                    "max_intermediate_chunk_count": 100,
                },
                "data_size_per_job": 1,
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
            },
        )
        assert get("//tmp/t_out_with_schema/@chunk_count") >= 5
        assert "auto_merge_disabled" in op.get_alerts()

    @authors("max42")
    @pytest.mark.timeout(60)
    def test_live_preview(self):
        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out1")
        create("table", "//tmp/t_out2")

        row_count = 50
        write_table("//tmp/t_in", [{"a": i} for i in range(row_count)])

        op = map(
            track=False,
            in_="//tmp/t_in",
            out=["//tmp/t_out1", "//tmp/t_out2"],
            command="read x; echo $x >&$(($x % 2 * 3 + 1))",
            format="<columns=[a]>schemaful_dsv",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "max_intermediate_chunk_count": 20,
                    "chunk_count_per_merge_job": 15,
                },
                "mapper": {"format": yson.loads("<columns=[a]>schemaful_dsv")},
                "data_size_per_job": 1,
            },
        )

        # We check that each of 4 possible live previews was non-empty at some moment.
        live_preview_appeared = {"map": [False, False], "auto_merge": [False, False]}

        while True:
            state = op.get_state(verbose=False)
            if state == "completed":
                break
            elif state == "failed":
                op.track()  # this should raise an exception
            for i in range(2):
                for vertex in live_preview_appeared:
                    path = op.get_path() + "/orchid/data_flow_graph/vertices/{0}/live_previews/{1}".format(vertex, i)
                    try:
                        if not exists(path, verbose=False):
                            continue
                        data = read_table(
                            path,
                            verbose=False,
                            table_reader={"unavailable_chunk_strategy": "skip"},
                        )
                        if len(data) > 0 and not live_preview_appeared[vertex][i]:
                            print_debug("Live preview of type {0} and index {1} appeared".format(vertex, i))
                        live_preview_appeared[vertex][i] = True
                    except YtError:
                        pass
            sleep(0.5)
            print_debug("{0} jobs completed".format(op.get_job_count("completed")))

            if all(live_preview_appeared["map"]) and all(live_preview_appeared["auto_merge"]):
                break

        op.track()

    @authors("ifsmirnov")
    def test_unversioned_update_no_auto_merge(self):
        sync_create_cells(1)
        create(
            "table",
            "//tmp/t_out",
            attributes={
                "dynamic": True,
                "schema": [
                    {"name": "key", "type": "int64", "sort_order": "ascending"},
                    {"name": "value", "type": "string"},
                ],
            },
        )
        sync_mount_table("//tmp/t_out")
        create("table", "//tmp/t_in")

        rows = [
            {"key": 1, "$change_type": 0, "$value:value": "a"},
            {"key": 2, "$change_type": 0, "$value:value": "b"},
        ]
        versioned_rows = [
            {"key": 1, "value": "a"},
            {"key": 2, "value": "b"},
        ]
        write_table("//tmp/t_in", rows)

        map(
            in_="//tmp/t_in",
            out="<append=%true;schema_modification=unversioned_update>//tmp/t_out",
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "relaxed",
                },
                "job_count": 2,
            },
        )

        assert read_table("//tmp/t_out") == versioned_rows
        wait(lambda: get("//tmp/t_out/@chunk_count") == 2)

    @authors("gritukan")
    def test_auto_merge_disabled_in_unordered_merge(self):
        create("table", "//tmp/in")
        create("table", "//tmp/out")
        write_table("//tmp/in", [{"x": 1}, {"x": 2}])

        op = merge(
            mode="unordered",
            in_=["//tmp/in"],
            out="//tmp/out",
            spec={
                "auto_merge": {
                    "mode": "relaxed",
                },
            },
        )
        op.track()
        assert read_table("//tmp/out") == [{"x": 1}, {"x": 2}]
        assert "auto_merge_disabled" in op.get_alerts()

    @authors("cookiedoth")
    def test_transaction_chunk_usage(self):
        self._create_account(35)

        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out")

        row_count = 300
        write_table(
            "//tmp/t_in",
            [{"a": i} for i in range(row_count)],
            max_row_buffer_size=1,
            table_writer={"desired_chunk_size": 1},
        )
        op = map(
            track=False,
            in_="//tmp/t_in",
            out="//tmp/t_out",
            command="cat",
            spec={
                "auto_merge": {
                    "mode": "manual",
                    "max_intermediate_chunk_count": 35,
                    "chunk_count_per_merge_job": 20,
                    "use_intermediate_data_account": True,
                },
                "data_size_per_job": 1,
                "suspend_operation_if_account_limit_exceeded": True,
                "intermediate_data_account": "acc",
            }
        )

        wait(lambda: op.get_job_count("completed") >= 100, None, 300)
        op.suspend()

        if (exists(op.get_path())):
            tx = get(op.get_path() + "/@output_transaction_id")
            transaction_chunk_count = get("#" + tx + "/@resource_usage/acc/chunk_count")
            chunk_count = get("//sys/accounts/acc/@resource_usage/chunk_count")
            print_debug("chunk count =", chunk_count)
            print_debug("transaction_chunk_count =", transaction_chunk_count)
            assert abs(transaction_chunk_count - chunk_count) <= 10
            assert transaction_chunk_count <= 40
            assert chunk_count <= 40
