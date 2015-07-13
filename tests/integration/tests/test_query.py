import pytest

import os

from yt_env_setup import YTEnvSetup
from yt_commands import *

from random import randint
from random import shuffle
from distutils.spawn import find_executable

##################################################################

@pytest.mark.skipif('os.environ.get("BUILD_ENABLE_LLVM", None) == "NO"')
class TestQuery(YTEnvSetup):
    NUM_MASTERS = 3
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    def _sample_data(self, path="//tmp/t", chunks=3, stripe=3):
        create("table", path,
            attributes = {
                "schema": [{"name": "a", "type": "int64"}, {"name": "b", "type": "int64"}]
            })

        for i in xrange(chunks):
            data = [
                {"a": (i * stripe + j), "b": (i * stripe + j) * 10}
                for j in xrange(1, 1 + stripe)]
            write_table("<append=true>" + path, data)

        sort(in_=path, out=path, sort_by=["a", "b"])

    def _create_table(self, path, schema, key_columns, data):
        create("table", path,
            attributes = {
                "schema": schema,
                "key_columns": key_columns
            })
        mount_table(path)
        self._wait_for_tablet_state(path, ["mounted"])
        insert_rows(path, data)

    # TODO(sandello): TableMountCache is not invalidated at the moment,
    # so table names must be unique.

    def test_simple(self):
        for i in xrange(0, 50, 10):
            path = "//tmp/t{0}".format(i)

            self._sample_data(path=path, chunks=i, stripe=10)
            result = select_rows("a, b from [{0}]".format(path), verbose=False)

            assert len(result) == 10 * i

    def test_invalid_data(self):
        path = "//tmp/t"
        create("table", path,
            attributes = {
                "schema": [{"name": "a", "type": "int64"}, {"name": "b", "type": "int64"}]
            })
        data = [{"a" : 1, "b" : 2}, 
                {"a" : 1, "b" : 2.2}, 
                {"a" : 1, "b" : "smth"}]

        write_table("<sorted_by=[a; b]>" + path, data)
        with pytest.raises(YtError):
            result = select_rows("a, b from [{0}] where b=b".format(path), verbose=False)
            print result

    def test_project1(self):
        self._sample_data(path="//tmp/t")
        expected = [{"s": 2 * i + 10 * i - 1} for i in xrange(1, 10)]
        actual = select_rows("2 * a + b - 1 as s from [//tmp/t]")
        assert expected == actual

    def test_group_by1(self):
        self._sample_data(path="//tmp/t")
        expected = [{"s": 450}]
        actual = select_rows("sum(b) as s from [//tmp/t] group by 1 as k")
        self.assertItemsEqual(actual, expected)

    def test_group_by2(self):
        self._sample_data(path="//tmp/t")
        expected = [{"k": 0, "s": 200}, {"k": 1, "s": 250}]
        actual = select_rows("k, sum(b) as s from [//tmp/t] group by a % 2 as k")
        self.assertItemsEqual(actual, expected)

    def test_merging_group_by(self):
        self._sync_create_cells(3, 3)

        create("table", "//tmp/t",
            attributes = {
                "schema": [
                    {"name": "a", "type": "int64"},
                    {"name": "b", "type": "int64"}],
                "key_columns": ["a"]
            })

        pivots = [[i*5] for i in xrange(0,20)]
        pivots.insert(0, [])
        reshard_table("//tmp/t", pivots)

        mount_table("//tmp/t")

        self._wait_for_tablet_state("//tmp/t", ["mounted"])

        data = [{"a" : i, "b" : i * 10} for i in xrange(0,100)]
        insert_rows("//tmp/t", data)

        expected = [
            {"k": 0, "aa": 49.0, "mb": 0, "ab": 490.0},
            {"k": 1, "aa": 50.0, "mb": 10, "ab": 500.0}]
        actual = select_rows("""
            k, avg(a) as aa, min(b) as mb, avg(b) as ab
            from [//tmp/t]
            group by a % 2 as k
            order by k limit 2""")
        assert expected == actual

    def test_merging_group_by2(self):
        self._sync_create_cells(3, 3)

        create("table", "//tmp/t",
            attributes = {
                "schema": [
                    {"name": "a", "type": "int64"},
                    {"name": "b", "type": "string"}],
                "key_columns": ["a"]
            })

        pivots = [[i*5] for i in xrange(0,20)]
        pivots.insert(0, [])
        reshard_table("//tmp/t", pivots)

        mount_table("//tmp/t")

        self._wait_for_tablet_state("//tmp/t", ["mounted"])

        data = [{"a" : i, "b" : str(i)} for i in xrange(0,100)]
        insert_rows("//tmp/t", data)

        expected = [
            {"k": 0, "m": "98"},
            {"k": 1, "m": "99"}]
        actual = select_rows("k, max(b) as m from [//tmp/t] group by a % 2 as k order by k limit 2")
        assert expected == actual

    def test_limit(self):
        self._sample_data(path="//tmp/t")
        expected = [{"a": 1, "b": 10}]
        actual = select_rows("* from [//tmp/t] limit 1")
        assert expected == actual

    def test_order_by(self):
        self._sync_create_cells(3, 1)

        create("table", "//tmp/t",
            attributes = {
                "schema": [
                    {"name": "k", "type": "int64"},
                    {"name": "u", "type": "int64"},
                    {"name": "v", "type": "int64"}],
                "key_columns": ["k"]
            })

        mount_table("//tmp/t")
        self._wait_for_tablet_state("//tmp/t", ["mounted"])

        values = [i for i in xrange(0, 300)]
        shuffle(values)

        data = [
            {"k": i, "v": values[i], "u": randint(0, 1000)}
            for i in xrange(0, 100)]
        insert_rows("//tmp/t", data)

        expected = [{col: v for col, v in row.iteritems() if col in ['k', 'v']} for row in data if row['u'] > 500]
        expected = sorted(expected, cmp=lambda x, y: x['v'] - y['v'])[0:10]

        actual = select_rows("k, v from [//tmp/t] where u > 500 order by v limit 10")
        assert expected == actual

    def test_join(self):
        self._sync_create_cells(3, 1)

        self._create_table(
            "//tmp/jl",
            [
                {"name": "a", "type": "int64"},
                {"name": "b", "type": "int64"},
                {"name": "c", "type": "int64"}],
            ["a", "b"],
            [
                {"a": 1, "b": 2, "c": 80 },
                {"a": 1, "b": 3, "c": 71 },
                {"a": 1, "b": 4, "c": 62 },
                {"a": 2, "b": 1, "c": 53 },
                {"a": 2, "b": 2, "c": 44 },
                {"a": 2, "b": 3, "c": 35 },
                {"a": 2, "b": 4, "c": 26 },
                {"a": 3, "b": 1, "c": 17 }
            ]);

        self._create_table(
            "//tmp/jr",
            [
                {"name": "c", "type": "int64"},
                {"name": "d", "type": "int64"},
                {"name": "e", "type": "int64"}],
            ["c"],
            [
                {"d": 1, "e": 2, "c": 80 },
                {"d": 1, "e": 3, "c": 71 },
                {"d": 1, "e": 4, "c": 62 },
                {"d": 2, "e": 1, "c": 53 },
                {"d": 2, "e": 2, "c": 44 },
                {"d": 2, "e": 3, "c": 35 },
                {"d": 2, "e": 4, "c": 26 },
                {"d": 3, "e": 1, "c": 17 }
            ]);

        expected = [
            {"a": 1, "b": 2, "c": 80, "d": 1, "e": 2},
            {"a": 1, "b": 3, "c": 71, "d": 1, "e": 3},
            {"a": 1, "b": 4, "c": 62, "d": 1, "e": 4},
            {"a": 2, "b": 1, "c": 53, "d": 2, "e": 1},
            {"a": 2, "b": 2, "c": 44, "d": 2, "e": 2},
            {"a": 2, "b": 3, "c": 35, "d": 2, "e": 3},
            {"a": 2, "b": 4, "c": 26, "d": 2, "e": 4},
            {"a": 3, "b": 1, "c": 17, "d": 3, "e": 1}]

        actual = select_rows("* from [//tmp/jl] join [//tmp/jr] using c where a < 4")
        assert expected == actual

        expected = [
            {"a": 2, "b": 1, "c": 53, "d": 2, "e": 1}]

        actual = select_rows("* from [//tmp/jl] join [//tmp/jr] using c where (a, b) IN ((2, 1))")
        assert expected == actual

        expected = [
            {"l.a": 2, "l.b": 1, "l.c": 53, "r.c": 53, "r.d": 2, "r.e": 1}]

        actual = select_rows("""
            * from [//tmp/jl] as l
            join [//tmp/jr] as r on l.c + 1 = r.c + 1
             where (l.a, l.b) in ((2, 1))""")
        assert expected == actual

    def test_join_many(self):
        self._sync_create_cells(1, 1)

        self._create_table(
            "//tmp/a",
            [
                {"name": "a", "type": "int64"},
                {"name": "c", "type": "string"}],
            ["a"],
            [
                {"a": 1, "c": "a"},
                {"a": 2, "c": "b"},
                {"a": 3, "c": "c"},
                {"a": 4, "c": "a"},
                {"a": 5, "c": "b"},
                {"a": 6, "c": "c"}
            ]);

        self._create_table(
            "//tmp/b",
            [
                {"name": "b", "type": "int64"},
                {"name": "c", "type": "string"},
                {"name": "d", "type": "string"}],
            ["b"],
            [
                {"b": 100, "c": "a", "d": "X"},
                {"b": 200, "c": "b", "d": "Y"},
                {"b": 300, "c": "c", "d": "X"},
                {"b": 400, "c": "a", "d": "Y"},
                {"b": 500, "c": "b", "d": "X"},
                {"b": 600, "c": "c", "d": "Y"}
            ]);

        self._create_table(
            "//tmp/c",
            [
                {"name": "d", "type": "string"},
                {"name": "e", "type": "int64"}],
            ["d"],
            [
                {"d": "X", "e": 1234},
                {"d": "Y", "e": 5678}
            ]);

        expected = [
            {"a": 2, "c": "b", "b": 200, "d": "Y", "e": 5678},
            {"a": 2, "c": "b", "b": 500, "d": "X", "e": 1234},

            {"a": 3, "c": "c", "b": 300, "d": "X", "e": 1234},
            {"a": 3, "c": "c", "b": 600, "d": "Y", "e": 5678},

            {"a": 4, "c": "a", "b": 100, "d": "X", "e": 1234},
            {"a": 4, "c": "a", "b": 400, "d": "Y", "e": 5678}]

        actual = select_rows("* from [//tmp/a] join [//tmp/b] using c join [//tmp/c] using d where a in (2,3,4)") 
        assert sorted(expected) == sorted(actual)

    def test_types(self):
        create("table", "//tmp/t")

        format = yson.loads("<boolean_as_string=false;format=text>yson")
        write_table(
            "//tmp/t",
            '{a=10;b=%false;c="hello";d=32u};{a=20;b=%true;c="world";d=64u};',
            input_format=format,
            is_raw=True)

        sort(in_="//tmp/t", out="//tmp/t", sort_by=["a", "b", "c", "d"])
        set("//tmp/t/@schema", [
            {"name": "a", "type": "int64"},
            {"name": "b", "type": "boolean"},
            {"name": "c", "type": "string"},
            {"name": "d", "type": "uint64"},
        ])

        assert select_rows('a, b, c, d from [//tmp/t] where c="hello"', output_format=format) == \
                '{"a"=10;"b"=%false;"c"="hello";"d"=32u};\n'

    def test_tablets(self):
        self._sync_create_cells(3, 1)

        create("table", "//tmp/t",
            attributes = {
                "schema": [{"name": "key", "type": "int64"}, {"name": "value", "type": "int64"}],
                "key_columns": ["key"]
            })

        mount_table("//tmp/t")
        self._wait_for_tablet_state("//tmp/t", ["mounted"])

        stripe = 10

        for i in xrange(0, 10):
            data = [
                {"key": (i * stripe + j), "value": (i * stripe + j) * 10}
                for j in xrange(1, 1 + stripe)]
            insert_rows("//tmp/t", data)

        unmount_table("//tmp/t")
        self._wait_for_tablet_state("//tmp/t", ["unmounted"])
        reshard_table("//tmp/t", [[], [10], [30], [50], [70], [90]])
        mount_table("//tmp/t", first_tablet_index=0, last_tablet_index=2)
        self._wait_for_tablet_state("//tmp/t", ["unmounted", "mounted"])

        select_rows("* from [//tmp/t] where key < 50")

        with pytest.raises(YtError): select_rows("* from [//tmp/t] where key < 51")

    def test_computed_column_simple(self):
        self._sync_create_cells(3, 1)

        create("table", "//tmp/t",
            attributes = {
                "schema": [
                    {"name": "hash", "type": "int64", "expression": "key * 33"},
                    {"name": "key", "type": "int64"},
                    {"name": "value", "type": "int64"}],
                "key_columns": ["hash", "key"]
            })
        reshard_table("//tmp/t", [[]] + [[i] for i in xrange(1, 100 * 33, 1000)])
        mount_table("//tmp/t")
        self._wait_for_tablet_state("//tmp/t", ["mounted"])

        insert_rows("//tmp/t", [{"key": i, "value": i * 2} for i in xrange(0,100)])

        expected = [{"hash": 42 * 33, "key": 42, "value": 42 * 2}]
        actual = select_rows("* from [//tmp/t] where key = 42")
        self.assertItemsEqual(actual, expected)

        expected = [{"hash": i * 33, "key": i, "value": i * 2} for i in xrange(10,80)]
        actual = sorted(select_rows("* from [//tmp/t] where key >= 10 and key < 80"))
        self.assertItemsEqual(actual, expected)

        expected = [{"hash": i * 33, "key": i, "value": i * 2} for i in [10, 20, 30]]
        actual = sorted(select_rows("* from [//tmp/t] where key in (10, 20, 30)"))
        self.assertItemsEqual(actual, expected)

    def test_computed_column_far_divide(self):
        self._sync_create_cells(3, 1)

        create("table", "//tmp/t",
            attributes = {
                "schema": [
                    {"name": "hash", "type": "int64", "expression": "key2 / 2"},
                    {"name": "key1", "type": "int64"},
                    {"name": "key2", "type": "int64"},
                    {"name": "value", "type": "int64"}],
                "key_columns": ["hash", "key1", "key2"]
            })
        reshard_table("//tmp/t", [[]] + [[i] for i in xrange(1, 500, 10)])
        mount_table("//tmp/t")
        self._wait_for_tablet_state("//tmp/t", ["mounted"])

        def expected(key_range):
            return [{"hash": i / 2, "key1": i, "key2": i, "value": i * 2} for i in key_range]

        insert_rows("//tmp/t", [{"key1": i, "key2": i, "value": i * 2} for i in xrange(0,1000)])

        actual = select_rows("* from [//tmp/t] where key2 = 42")
        self.assertItemsEqual(actual, expected([42]))

        actual = sorted(select_rows("* from [//tmp/t] where key2 >= 10 and key2 < 80"))
        self.assertItemsEqual(actual, expected(xrange(10,80)))

        actual = sorted(select_rows("* from [//tmp/t] where key2 in (10, 20, 30)"))
        self.assertItemsEqual(actual, expected([10, 20, 30]))

        actual = sorted(select_rows("* from [//tmp/t] where key2 in (10, 20, 30) and key1 in (30, 40)"))
        self.assertItemsEqual(actual, expected([30]))

    def test_computed_column_modulo(self):
        self._sync_create_cells(3, 1)

        create("table", "//tmp/t",
            attributes = {
                "schema": [
                    {"name": "hash", "type": "int64", "expression": "key2 % 2"},
                    {"name": "key1", "type": "int64"},
                    {"name": "key2", "type": "int64"},
                    {"name": "value", "type": "int64"}],
                "key_columns": ["hash", "key1", "key2"]
            })
        reshard_table("//tmp/t", [[]] + [[i] for i in xrange(1, 500, 10)])
        mount_table("//tmp/t")
        self._wait_for_tablet_state("//tmp/t", ["mounted"])

        def expected(key_range):
            return [{"hash": i % 2, "key1": i, "key2": i, "value": i * 2} for i in key_range]

        insert_rows("//tmp/t", [{"key1": i, "key2": i, "value": i * 2} for i in xrange(0,1000)])

        actual = select_rows("* from [//tmp/t] where key2 = 42")
        self.assertItemsEqual(actual, expected([42]))

        actual = sorted(select_rows("* from [//tmp/t] where key1 >= 10 and key1 < 80"))
        self.assertItemsEqual(actual, expected(xrange(10,80)))

        actual = sorted(select_rows("* from [//tmp/t] where key1 in (10, 20, 30)"))
        self.assertItemsEqual(actual, expected([10, 20, 30]))

        actual = sorted(select_rows("* from [//tmp/t] where key1 in (10, 20, 30) and key2 in (30, 40)"))
        self.assertItemsEqual(actual, expected([30]))

    def test_udf(self):
        registry_path =  "//tmp/udfs"
        create("map_node", registry_path)

        abs_path = os.path.join(registry_path, "abs_udf")
        create("file", abs_path,
            attributes = { "function_descriptor": {
                "name": "abs_udf",
                "argument_types": [{
                    "tag": "concrete_type",
                    "value": "int64"}],
                "result_type": {
                    "tag": "concrete_type",
                    "value": "int64"},
                "calling_convention": "simple"}})

        sum_path = os.path.join(registry_path, "sum_udf2")
        create("file", sum_path,
            attributes = { "function_descriptor": {
                "name": "sum_udf2",
                "argument_types": [{
                    "tag": "concrete_type",
                    "value": "int64"}],
                "repeated_argument_type": {
                    "tag": "concrete_type",
                    "value": "int64"},
                "result_type": {
                    "tag": "concrete_type",
                    "value": "int64"},
                "calling_convention": "unversioned_value"}})

        local_bitcode_path = find_executable("test_udfs.bc")
        local_bitcode_path2 = find_executable("sum_udf2.bc")
        write_local_file(abs_path, local_bitcode_path)
        write_local_file(sum_path, local_bitcode_path2)

        self._sample_data(path="//tmp/u")
        expected = [{"s": 2 * i} for i in xrange(1, 10)]
        actual = select_rows("abs_udf(-2 * a) as s from [//tmp/u] where sum_udf2(b, 1, 2) = sum_udf2(3, b)")
        self.assertItemsEqual(actual, expected)

    def test_udaf(self):
        registry_path = "//tmp/udfs"
        create("map_node", registry_path)

        avg_path = os.path.join(registry_path, "avg_udaf")
        create("file", avg_path,
            attributes = { "aggregate_descriptor": {
                "name": "avg_udaf",
                "argument_type": {
                    "tag": "concrete_type",
                    "value": "int64"},
                "state_type": {
                    "tag": "concrete_type",
                    "value": "string"},
                "result_type": {
                    "tag": "concrete_type",
                    "value": "double"},
                "calling_convention": "unversioned_value"}})

        local_implementation_path = find_executable("test_udfs.bc")
        write_local_file(avg_path, local_implementation_path)

        self._sample_data(path="//tmp/ua")
        expected = [{"x": 5.0}]
        actual = select_rows("avg_udaf(a) as x from [//tmp/ua] group by 1")
        self.assertItemsEqual(actual, expected)

    def test_aggregate_string_capture(self):
        create("table", "//tmp/t",
            attributes = {
                "schema": [{"name": "a", "type": "string"}]
            })

        # Need at least 1024 items to ensure a second batch in the scan operator
        data = [
            {"a": "A" + str(j) + "BCD"}
            for j in xrange(1, 2048)]
        write_table("//tmp/t", data)
        sort(in_="//tmp/t", out="//tmp/t", sort_by=["a"])

        expected = [{"m": "a1000bcd"}]
        actual = select_rows("min(lower(a)) as m from [//tmp/t] group by 1")
        self.assertItemsEqual(actual, expected)

    def test_cardinality(self):
        self._sync_create_cells(3, 3)

        create("table", "//tmp/card",
            attributes = {
                "schema": [
                    {"name": "a", "type": "int64"},
                    {"name": "b", "type": "int64"}],
                "key_columns": ["a"]
            })

        pivots = [[i*1000] for i in xrange(0,20)]
        pivots.insert(0, [])
        reshard_table("//tmp/card", pivots)

        mount_table("//tmp/card")

        self._wait_for_tablet_state("//tmp/card", ["mounted"])

        data = [{"a" : i} for i in xrange(0,20000)]
        insert_rows("//tmp/card", data)
        insert_rows("//tmp/card", data)
        insert_rows("//tmp/card", data)
        insert_rows("//tmp/card", data)

        actual = select_rows("cardinality(a) as b from [//tmp/card] group by a % 2 as k")
        assert actual[0]["b"] > .95 * 10000
        assert actual[0]["b"] < 1.05 * 10000
        assert actual[1]["b"] > .95 * 10000
        assert actual[1]["b"] < 1.05 * 10000

    def test_object_udf(self):
        registry_path =  "//tmp/udfs"
        create("map_node", registry_path)

        abs_path = os.path.join(registry_path, "abs_udf")
        create("file", abs_path,
            attributes = { "function_descriptor": {
                "name": "abs_udf",
                "argument_types": [{
                    "tag": "concrete_type",
                    "value": "int64"}],
                "result_type": {
                    "tag": "concrete_type",
                    "value": "int64"},
                "calling_convention": "simple"}})

        local_bitcode_path = find_executable("test_udfs_o.o")
        write_local_file(abs_path, local_bitcode_path)

        self._sample_data(path="//tmp/sou")
        expected = [{"s": 2 * i} for i in xrange(1, 10)]
        actual = select_rows("abs_udf(-2 * a) as s from [//tmp/sou]")
        self.assertItemsEqual(actual, expected)

