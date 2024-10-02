# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(3.5.0)

LICENSE(Apache-2.0)

PEERDIR(
    contrib/python/protobuf
    contrib/python/types-protobuf
)

NO_LINT()

PY_SRCS(
    TOP_LEVEL
    mypy_protobuf/__init__.py
    mypy_protobuf/extensions_pb2.py
    mypy_protobuf/main.py
)

RESOURCE_FILES(
    PREFIX contrib/python/mypy-protobuf/
    .dist-info/METADATA
    .dist-info/entry_points.txt
    .dist-info/top_level.txt
)

END()

RECURSE(
    bin
)
