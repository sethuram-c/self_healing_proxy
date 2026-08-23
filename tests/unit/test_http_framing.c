#include "http_framing.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>


static void test_simple_get(void)
{
    const unsigned char request[] =
        "GET /hello HTTP/1.1\r\n"
        "Host: example\r\n"
        "\r\n";

    http_request_frame_t frame;

    http_frame_result_t result =
        http_request_frame_inspect(
            request,
            sizeof(request) - 1,
            &frame
        );

    assert(
        result == HTTP_FRAME_COMPLETE
    );

    assert(
        frame.content_length == 0
    );

    assert(
        frame.total_bytes
        == sizeof(request) - 1
    );
}


static void test_fragmented_headers(void)
{
    const unsigned char request[] =
        "GET /fragmented HTTP/1.1\r\n"
        "Host: example\r\n"
        "X-Test: yes\r\n"
        "\r\n";

    size_t request_length =
        sizeof(request) - 1;

    for (size_t length = 0;
         length < request_length;
         ++length) {

        http_request_frame_t frame;

        http_frame_result_t result =
            http_request_frame_inspect(
                request,
                length,
                &frame
            );

        assert(
            result
            == HTTP_FRAME_INCOMPLETE
        );
    }

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            request,
            request_length,
            &frame
        )
        == HTTP_FRAME_COMPLETE
    );
}


static void test_content_length_body(void)
{
    const unsigned char request[] =
        "POST /submit HTTP/1.1\r\n"
        "Host: example\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    size_t request_length =
        sizeof(request) - 1;

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            request,
            request_length - 1,
            &frame
        )
        == HTTP_FRAME_INCOMPLETE
    );

    assert(
        http_request_frame_inspect(
            request,
            request_length,
            &frame
        )
        == HTTP_FRAME_COMPLETE
    );

    assert(
        frame.content_length == 5
    );

    assert(
        frame.total_bytes
        == request_length
    );
}


static void test_case_insensitive_content_length(void)
{
    const unsigned char request[] =
        "POST /x HTTP/1.1\r\n"
        "Host: example\r\n"
        "cOnTeNt-LeNgTh: 3\r\n"
        "\r\n"
        "abc";

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            request,
            sizeof(request) - 1,
            &frame
        )
        == HTTP_FRAME_COMPLETE
    );

    assert(
        frame.content_length == 3
    );
}


static void test_duplicate_equal_content_length(void)
{
    const unsigned char request[] =
        "POST /x HTTP/1.1\r\n"
        "Content-Length: 3\r\n"
        "Content-Length: 3\r\n"
        "\r\n"
        "abc";

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            request,
            sizeof(request) - 1,
            &frame
        )
        == HTTP_FRAME_COMPLETE
    );

    assert(
        frame.content_length == 3
    );
}


static void test_conflicting_content_length(void)
{
    const unsigned char request[] =
        "POST /x HTTP/1.1\r\n"
        "Content-Length: 3\r\n"
        "Content-Length: 7\r\n"
        "\r\n"
        "abcdefg";

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            request,
            sizeof(request) - 1,
            &frame
        )
        == HTTP_FRAME_ERROR
    );
}


static void test_invalid_content_length(void)
{
    const unsigned char request[] =
        "POST /x HTTP/1.1\r\n"
        "Content-Length: abc\r\n"
        "\r\n";

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            request,
            sizeof(request) - 1,
            &frame
        )
        == HTTP_FRAME_ERROR
    );
}


static void test_transfer_encoding_rejected(void)
{
    const unsigned char request[] =
        "POST /x HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n\r\n";

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            request,
            sizeof(request) - 1,
            &frame
        )
        == HTTP_FRAME_ERROR
    );
}


static void test_bad_request_line(void)
{
    const unsigned char request[] =
        "GET /missing-version\r\n"
        "Host: example\r\n"
        "\r\n";

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            request,
            sizeof(request) - 1,
            &frame
        )
        == HTTP_FRAME_ERROR
    );
}


static void test_coalesced_requests(void)
{
    static const char first[] =
        "GET /one HTTP/1.1\r\n"
        "Host: example\r\n"
        "\r\n";

    static const char second[] =
        "GET /two HTTP/1.1\r\n"
        "Host: example\r\n"
        "\r\n";

    unsigned char combined[
        sizeof(first)
        + sizeof(second)
    ];

    size_t first_length =
        sizeof(first) - 1;

    size_t second_length =
        sizeof(second) - 1;

    memcpy(
        combined,
        first,
        first_length
    );

    memcpy(
        combined + first_length,
        second,
        second_length
    );

    http_request_frame_t frame;

    assert(
        http_request_frame_inspect(
            combined,
            first_length
                + second_length,
            &frame
        )
        == HTTP_FRAME_COMPLETE
    );

    /*
     * Critical:
     * parser identifies only request #1.
     */
    assert(
        frame.total_bytes
        == first_length
    );
}


int main(void)
{
    test_simple_get();

    test_fragmented_headers();

    test_content_length_body();

    test_case_insensitive_content_length();

    test_duplicate_equal_content_length();

    test_conflicting_content_length();

    test_invalid_content_length();

    test_transfer_encoding_rejected();

    test_bad_request_line();

    test_coalesced_requests();

    puts(
        "HTTP framing tests passed"
    );

    return 0;
}
