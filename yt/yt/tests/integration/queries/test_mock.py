from yt_env_setup import YTEnvSetup

from yt_commands import (authors, raises_yt_error, wait, create_user, print_debug, select_rows)

from yt.test_helpers import assert_items_equal

from queries.environment import start_query, list_queries

from collections import Counter


class TestQueriesMock(YTEnvSetup):
    DELTA_DRIVER_CONFIG = {
        "cluster_connection_dynamic_config_policy": "from_cluster_directory",
    }

    @authors("max42")
    def test_fail(self, query_tracker):
        query = start_query("mock", "fail")
        with raises_yt_error("failed"):
            query.track()
        assert query.get_state() == "failed"

        query = start_query("mock", "fail_by_exception")
        with raises_yt_error("failed"):
            query.track()
        assert query.get_state() == "failed"

        query = start_query("mock", "fail_after", settings={"duration": 3000})
        with raises_yt_error("failed"):
            query.track()
        assert query.get_state() == "failed"

    @authors("max42")
    def test_abort(self, query_tracker):
        query = start_query("mock", "run_forever")
        wait(lambda: query.get_state() == "running")
        query.abort()
        with raises_yt_error("aborted"):
            query.track()
        assert query.get_state() == "aborted"

    @authors("max42")
    def test_complete(self, query_tracker):
        error = {"code": 42, "message": "Mock query execution error", "attributes": {"some_attr": "some_value"}}
        schema = [{"name": "foo", "type": "int64"}, {"name": "bar", "type": "string"}]
        rows = [{"foo": 42, "bar": "abc"}, {"foo": -17, "bar": "def"}, {"foo": 123, "bar": "ghi"}]

        query = start_query("mock", "complete_after", settings={
            "duration": 3000,
            "results": [
                {"error": error},
                {"schema": schema, "rows": rows},
            ]
        })

        query.track()
        query_info = query.get()
        assert query_info["result_count"] == 2

        result_0_info = query.get_result(0)
        assert result_0_info["error"] == error
        with raises_yt_error("Mock query execution error"):
            query.read_result(0)

        result_1_info = query.get_result(1)
        for column in result_1_info["schema"]:
            del column["type_v3"]
            del column["required"]
        result_1_info["schema"] = list(result_1_info["schema"])
        assert result_1_info["data_statistics"]["row_count"] == 3
        assert result_1_info["data_statistics"]["data_weight"] == 36
        assert result_1_info["schema"] == schema
        assert_items_equal(query.read_result(1), rows)

        assert_items_equal(query.read_result(1, lower_row_index=1, upper_row_index=2), rows[1:2])
        assert_items_equal(query.read_result(1, lower_row_index=-1, upper_row_index=5), rows)
        assert_items_equal(query.read_result(1, lower_row_index=2, upper_row_index=1), [])

    @authors("max42")
    def test_list(self, query_tracker):
        create_user("u1")
        create_user("u2")
        q0 = start_query("mock", "fail", authenticated_user="u1")
        q1 = start_query("mock", "complete_after", settings={"duration": 16000}, authenticated_user="u2")
        q2 = start_query("mock", "fail_by_exception", authenticated_user="u1")
        q3 = start_query("mock", "blahblah", authenticated_user="u2")
        q4 = start_query("mock", "fail_after", settings={"duration": 8000}, authenticated_user="u2")
        q5 = start_query("mock", "run_forever", authenticated_user="u1")
        q6 = start_query("mock", "run_forever", authenticated_user="u1")

        def collect_batch(attribute):
            queries = list_queries(cursor_direction="future", attributes=["id", attribute])
            result = []
            for q, expected_q in zip(queries["queries"], (q0, q1, q2, q3, q4, q5, q6)):
                assert q["id"] == expected_q.id
                result.append(q[attribute])
            return result

        q_times = collect_batch("start_time")

        wait(lambda: q6.get_state() == "running")
        q6.abort()

        def expect_queries(queries, list_result, incomplete=False):
            try:
                assert set(q.id for q in queries) == set(q["id"] for q in list_result["queries"])
                assert list_result["incomplete"] == incomplete
            except AssertionError:
                timestamp = list_result["timestamp"]
                print_debug(f"Assertion failed, dumping content of dynamic tables by timestamp {timestamp}")
                for table in ("active_queries", "finished_queries", "finished_queries_by_start_time"):
                    print_debug(f"{table}:")
                    select_rows(f"* from [//sys/query_tracker/{table}]", timestamp=timestamp)
                raise

        def run_checks():
            expect_queries([q6, q5, q4, q3, q2, q1, q0], list_queries())
            expect_queries([q0, q1, q2, q3, q4, q5, q6], list_queries(cursor_direction="future"))

            expect_queries([q6, q5, q2, q0], list_queries(user="u1"))
            expect_queries([q4, q3, q1], list_queries(user="u2"))

            expect_queries([q4, q2, q0], list_queries(filter="fail"))

            expect_queries([q5, q4],
                           list_queries(from_time=q_times[1], to_time=q_times[5], limit=2),
                           incomplete=True)
            expect_queries([q3, q2],
                           list_queries(from_time=q_times[1], to_time=q_times[5], limit=2, cursor_time=q_times[4]),
                           incomplete=True)
            expect_queries([q1],
                           list_queries(from_time=q_times[1], to_time=q_times[5], limit=2, cursor_time=q_times[2]))
            expect_queries([q1, q2, q3],
                           list_queries(from_time=q_times[1], to_time=q_times[5], limit=3, cursor_direction="future"),
                           incomplete=True)
            expect_queries([q4, q5],
                           list_queries(from_time=q_times[1], to_time=q_times[5], limit=3, cursor_direction="future",
                                        cursor_time=q_times[3]))

        while True:
            run_checks()
            q_states = collect_batch("state")
            print_debug("Query states:", q_states, "counts:", Counter(q_states))
            if q_states == ["failed", "completed", "failed", "failed", "failed", "running", "aborted"]:
                break

        expect_queries([q6], list_queries(state="aborted"))
        expect_queries([q4, q3, q2, q0], list_queries(state="failed"))
        expect_queries([q5], list_queries(state="running"))
        expect_queries([q1], list_queries(state="completed"))
