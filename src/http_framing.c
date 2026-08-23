#include "http_framing.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NOT_FOUND ((size_t)-1)


static unsigned char ascii_lower(
    unsigned char value
)
{
    if (value >= 'A' && value <= 'Z') {
        return (unsigned char)(
            value - 'A' + 'a'
        );
    }

    return value;
}


static int bytes_equal_case_insensitive(
    const unsigned char *data,
    size_t length,
    const char *expected
)
{
    size_t expected_length =
        strlen(expected);

    if (length != expected_length) {
        return 0;
    }

    for (size_t i = 0;
         i < length;
         ++i) {

        if (ascii_lower(data[i])
            != ascii_lower(
                (unsigned char)expected[i]
            )) {

            return 0;
        }
    }

    return 1;
}


static size_t find_crlf(
    const unsigned char *data,
    size_t start,
    size_t limit
)
{
    for (size_t i = start;
         i + 1 < limit;
         ++i) {

        if (data[i] == '\r'
            && data[i + 1] == '\n') {

            return i;
        }
    }

    return NOT_FOUND;
}


static size_t find_header_end(
    const unsigned char *data,
    size_t length
)
{
    if (length < 4) {
        return NOT_FOUND;
    }

    for (size_t i = 0;
         i + 3 < length;
         ++i) {

        if (data[i] == '\r'
            && data[i + 1] == '\n'
            && data[i + 2] == '\r'
            && data[i + 3] == '\n') {

            /*
             * Return number of bytes in
             * request line + headers +
             * final empty CRLF line.
             */
            return i + 4;
        }
    }

    return NOT_FOUND;
}


static int validate_request_line(
    const unsigned char *data,
    size_t line_length
)
{
    size_t first_space = NOT_FOUND;
    size_t second_space = NOT_FOUND;

    for (size_t i = 0;
         i < line_length;
         ++i) {

        if (data[i] != ' ') {
            continue;
        }

        if (first_space == NOT_FOUND) {
            first_space = i;
        } else {
            second_space = i;
            break;
        }
    }

    if (first_space == NOT_FOUND
        || second_space == NOT_FOUND) {

        return -1;
    }

    /*
     * METHOD must not be empty.
     */
    if (first_space == 0) {
        return -1;
    }

    /*
     * Request target must not be empty.
     */
    if (second_space
        <= first_space + 1) {

        return -1;
    }

    /*
     * HTTP version must not be empty.
     */
    if (second_space + 1
        >= line_length) {

        return -1;
    }

    const unsigned char *version =
        data + second_space + 1;

    size_t version_length =
        line_length
        - second_space
        - 1;

    if (version_length == 8
        && memcmp(
            version,
            "HTTP/1.1",
            8
        ) == 0) {

        return 0;
    }

    if (version_length == 8
        && memcmp(
            version,
            "HTTP/1.0",
            8
        ) == 0) {

        return 0;
    }

    return -1;
}


static int parse_decimal_size(
    const unsigned char *data,
    size_t length,
    size_t *value_out
)
{
    if (length == 0) {
        return -1;
    }

    size_t value = 0;

    for (size_t i = 0;
         i < length;
         ++i) {

        if (data[i] < '0'
            || data[i] > '9') {

            return -1;
        }

        size_t digit =
            (size_t)(
                data[i] - '0'
            );

        if (value
            > (
                (SIZE_MAX - digit)
                / 10
            )) {

            return -1;
        }

        value =
            (value * 10)
            + digit;
    }

    *value_out = value;

    return 0;
}


http_frame_result_t
http_request_frame_inspect(
    const unsigned char *data,
    size_t length,
    http_request_frame_t *frame
)
{
    if (data == NULL
        || frame == NULL) {

        return HTTP_FRAME_ERROR;
    }

    memset(
        frame,
        0,
        sizeof(*frame)
    );

    size_t header_bytes =
        find_header_end(
            data,
            length
        );

    if (header_bytes == NOT_FOUND) {
        if (length
            >= HTTP_MAX_HEADER_BYTES) {

            return HTTP_FRAME_ERROR;
        }

        return HTTP_FRAME_INCOMPLETE;
    }

    size_t request_line_end =
        find_crlf(
            data,
            0,
            header_bytes
        );

    if (request_line_end
        == NOT_FOUND) {

        return HTTP_FRAME_ERROR;
    }

    if (validate_request_line(
            data,
            request_line_end)
        == -1) {

        return HTTP_FRAME_ERROR;
    }

    size_t cursor =
        request_line_end + 2;

    int content_length_seen = 0;

    size_t content_length = 0;

    while (cursor < header_bytes) {
        size_t line_end =
            find_crlf(
                data,
                cursor,
                header_bytes
            );

        if (line_end == NOT_FOUND) {
            return HTTP_FRAME_ERROR;
        }

        /*
         * Empty line marks end of headers.
         */
        if (line_end == cursor) {
            cursor += 2;
            break;
        }

        size_t colon = NOT_FOUND;

        for (size_t i = cursor;
             i < line_end;
             ++i) {

            if (data[i] == ':') {
                colon = i;
                break;
            }
        }

        if (colon == NOT_FOUND
            || colon == cursor) {

            return HTTP_FRAME_ERROR;
        }

        const unsigned char *name =
            data + cursor;

        size_t name_length =
            colon - cursor;

        size_t value_start =
            colon + 1;

        while (value_start < line_end
               && (
                   data[value_start] == ' '
                   || data[value_start] == '\t'
               )) {

            value_start++;
        }

        size_t value_end =
            line_end;

        while (value_end > value_start
               && (
                   data[value_end - 1] == ' '
                   || data[value_end - 1] == '\t'
               )) {

            value_end--;
        }

        size_t value_length =
            value_end
            - value_start;

        if (bytes_equal_case_insensitive(
                name,
                name_length,
                "Content-Length")) {

            size_t parsed_length = 0;

            if (parse_decimal_size(
                    data + value_start,
                    value_length,
                    &parsed_length)
                == -1) {

                return HTTP_FRAME_ERROR;
            }

            if (content_length_seen
                && parsed_length
                   != content_length) {

                return HTTP_FRAME_ERROR;
            }

            content_length_seen = 1;

            content_length =
                parsed_length;
        }

        if (bytes_equal_case_insensitive(
                name,
                name_length,
                "Transfer-Encoding")) {

            /*
             * Chunked framing is deliberately
             * unsupported in Phase 12.
             */
            return HTTP_FRAME_ERROR;
        }

        cursor =
            line_end + 2;
    }

    if (cursor != header_bytes) {
        return HTTP_FRAME_ERROR;
    }

    if (content_length
        > SIZE_MAX - header_bytes) {

        return HTTP_FRAME_ERROR;
    }

    size_t total_bytes =
        header_bytes
        + content_length;

    frame->header_bytes =
        header_bytes;

    frame->content_length =
        content_length;

    frame->total_bytes =
        total_bytes;

    if (length < total_bytes) {
        return HTTP_FRAME_INCOMPLETE;
    }

    return HTTP_FRAME_COMPLETE;
}
