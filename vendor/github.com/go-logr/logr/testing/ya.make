GO_LIBRARY()

LICENSE(Apache-2.0)

SRCS(
    test.go
)

GO_TEST_SRCS(test_test.go)

END()

RECURSE(
    gotest
)
