GO_LIBRARY()

LICENSE(Apache-2.0)

SRCS(
    ignorefile.go
)

GO_TEST_SRCS(ignorefile_test.go)

END()

RECURSE(
    gotest
)
