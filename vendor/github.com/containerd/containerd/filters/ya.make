GO_LIBRARY()

LICENSE(Apache-2.0)

SRCS(
    adaptor.go
    filter.go
    parser.go
    quote.go
    scanner.go
)

GO_TEST_SRCS(
    filter_test.go
    scanner_test.go
)

END()

RECURSE(
    gotest
)
