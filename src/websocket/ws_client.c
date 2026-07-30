/*
  +----------------------------------------------------------------------+
  | Copyright (c) TrueAsync                                              |
  +----------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0                       |
  +----------------------------------------------------------------------+
*/

#include "php.h"
#include "ext/standard/url.h"
#include "zend_smart_str.h"

#include "websocket/ws_client.h"
#include "websocket/ws_handshake.h"
#ifndef WS_CLIENT_HANDSHAKE_TEST
#include "ext/standard/base64.h"
#include "ext/random/php_random_csprng.h"
#include "main/php_streams.h"
#include "main/streams/php_stream_transport.h"
#include "websocket/php_websocket.h"

#include "../../stubs/WebSocketClient.php_arginfo.h"

#define WS_CLIENT_READ_CHUNK 8192u
#define WS_CLIENT_MAX_RESPONSE_HEADERS (16u * 1024u)

struct ws_client_connection_t {
    php_stream   *stream;
    ws_session_t *session;
    bool          closed;
};
#endif

static bool ws_client_contains_crlf(const zend_string *value);
static bool ws_client_header_value_is_safe(const zend_string *value);
#ifndef WS_CLIENT_HANDSHAKE_TEST
static bool ws_client_transport_send(void *ctx, const uint8_t *data, size_t len);
static bool ws_client_find_header_end(const char *data, size_t len, size_t *end);
static bool ws_client_response_subprotocol(const char *response, size_t response_len,
                                           const zend_string *const *offered,
                                           size_t offered_count, zend_string **selected);

static const ws_transport_ops_t ws_client_transport_ops = {
    ws_client_transport_send,
    ws_client_transport_send,
    NULL,
};
#endif

bool ws_client_url_parse(const zend_string *url, ws_client_url_t *out)
{
    if (url == NULL || out == NULL || ws_client_contains_crlf(url)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    php_url *parsed = php_url_parse_ex2(ZSTR_VAL(url), ZSTR_LEN(url), NULL);
    if (parsed == NULL || parsed->scheme == NULL || parsed->host == NULL ||
        parsed->user != NULL || parsed->pass != NULL || parsed->fragment != NULL) {
        if (parsed != NULL) {
            php_url_free(parsed);
        }
        return false;
    }

    const bool secure = zend_string_equals_literal_ci(parsed->scheme, "wss");
    if (!secure && !zend_string_equals_literal_ci(parsed->scheme, "ws")) {
        php_url_free(parsed);
        return false;
    }

    if (ZSTR_LEN(parsed->host) == 0 || ws_client_contains_crlf(parsed->host) ||
        (parsed->path != NULL && ws_client_contains_crlf(parsed->path)) ||
        (parsed->query != NULL && ws_client_contains_crlf(parsed->query))) {
        php_url_free(parsed);
        return false;
    }

    out->host = zend_string_copy(parsed->host);
    smart_str target = {0};
    if (parsed->path == NULL || ZSTR_LEN(parsed->path) == 0) {
        smart_str_appendc(&target, '/');
    } else {
        smart_str_append(&target, parsed->path);
    }
    if (parsed->query != NULL) {
        smart_str_appendc(&target, '?');
        smart_str_append(&target, parsed->query);
    }
    smart_str_0(&target);
    out->target = target.s;
    out->port = parsed->port != 0 ? parsed->port : (secure ? 443 : 80);
    out->secure = secure;
    php_url_free(parsed);
    return true;
}

void ws_client_url_release(ws_client_url_t *url)
{
    if (url == NULL) {
        return;
    }
    if (url->host != NULL) {
        zend_string_release(url->host);
    }
    if (url->target != NULL) {
        zend_string_release(url->target);
    }
    memset(url, 0, sizeof(*url));
}

static bool ws_client_contains_crlf(const zend_string *const value)
{
    return value == NULL || memchr(ZSTR_VAL(value), '\r', ZSTR_LEN(value)) != NULL ||
           memchr(ZSTR_VAL(value), '\n', ZSTR_LEN(value)) != NULL;
}

static bool ws_client_header_value_is_safe(const zend_string *const value)
{
    if (value == NULL) {
        return false;
    }
    for (size_t i = 0; i < ZSTR_LEN(value); i++) {
        const unsigned char c = (unsigned char) ZSTR_VAL(value)[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return false;
        }
    }
    return true;
}

static bool ws_client_is_token(const zend_string *const value)
{
    if (value == NULL || ZSTR_LEN(value) == 0) {
        return false;
    }

    for (size_t index = 0; index < ZSTR_LEN(value); index++) {
        const unsigned char c = (unsigned char) ZSTR_VAL(value)[index];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || strchr("!#$%&'*+-.^_`|~", c) != NULL)) {
            return false;
        }
    }

    return true;
}

static bool ws_client_header_name_equals(const char *const name,
                                         const size_t name_len,
                                         const char *const expected)
{
    const size_t expected_len = strlen(expected);

    if (name_len != expected_len) {
        return false;
    }

    for (size_t index = 0; index < name_len; index++) {
        const unsigned char actual = (unsigned char) name[index];
        const unsigned char wanted = (unsigned char) expected[index];
        const unsigned char actual_lower = actual >= 'A' && actual <= 'Z' ? actual + ('a' - 'A') : actual;
        const unsigned char wanted_lower = wanted >= 'A' && wanted <= 'Z' ? wanted + ('a' - 'A') : wanted;

        if (actual_lower != wanted_lower) {
            return false;
        }
    }

    return true;
}

static bool ws_client_header_has_token(const char *value, size_t value_len,
                                       const char *const expected)
{
    while (value_len > 0) {
        while (value_len > 0 && (*value == ' ' || *value == '\t' || *value == ',')) {
            value++;
            value_len--;
        }

        const char *const token = value;
        while (value_len > 0 && *value != ',') {
            value++;
            value_len--;
        }
        const char *token_end = value;
        while (token_end > token && (token_end[-1] == ' ' || token_end[-1] == '\t')) {
            token_end--;
        }

        if (token_end > token && ws_client_header_name_equals(token,
                (size_t) (token_end - token), expected)) {
            return true;
        }
    }

    return false;
}

bool ws_client_handshake_init(ws_client_handshake_t *const handshake,
                              const zend_string *const host,
                              const zend_string *const target,
                              const zend_string *const key)
{
    if (handshake == NULL || host == NULL || target == NULL || key == NULL ||
        ZSTR_LEN(host) == 0 || ZSTR_LEN(target) == 0 || ZSTR_VAL(target)[0] != '/' ||
        ws_client_contains_crlf(host) || ws_client_contains_crlf(target) ||
        ws_client_contains_crlf(key) || ZSTR_LEN(key) != WS_CLIENT_KEY_LEN) {
        return false;
    }

    char accept[WS_ACCEPT_LEN];
    if (ws_handshake_compute_accept(ZSTR_VAL(key), ZSTR_LEN(key), accept) != 0) {
        return false;
    }

    ws_client_handshake_release(handshake);
    handshake->host = zend_string_copy(host);
    handshake->target = zend_string_copy(target);
    handshake->key = zend_string_copy(key);
    handshake->expected_accept = zend_string_init(accept, sizeof(accept), 0);
    return true;
}

static bool ws_client_is_reserved_header(const zend_string *const name)
{
    return ws_client_header_name_equals(ZSTR_VAL(name), ZSTR_LEN(name), "Host") ||
           ws_client_header_name_equals(ZSTR_VAL(name), ZSTR_LEN(name), "Connection") ||
           ws_client_header_name_equals(ZSTR_VAL(name), ZSTR_LEN(name), "Upgrade") ||
           ws_client_header_name_equals(ZSTR_VAL(name), ZSTR_LEN(name), "Sec-WebSocket-Key") ||
           ws_client_header_name_equals(ZSTR_VAL(name), ZSTR_LEN(name), "Sec-WebSocket-Version") ||
           ws_client_header_name_equals(ZSTR_VAL(name), ZSTR_LEN(name), "Sec-WebSocket-Protocol");
}

static bool ws_client_append_header(smart_str *const request, const zend_string *const name,
                                    const zend_string *const value)
{
    if (!ws_client_is_token(name) || ws_client_is_reserved_header(name) ||
        !ws_client_header_value_is_safe(value)) {
        return false;
    }
    smart_str_append(request, name);
    smart_str_appendl(request, ": ", 2);
    smart_str_append(request, value);
    smart_str_appendl(request, "\r\n", 2);
    return true;
}

zend_string *ws_client_handshake_build_request_ex(const ws_client_handshake_t *const handshake,
                                                  HashTable *const headers,
                                                  const zend_string *const *const subprotocols,
                                                  const size_t subprotocol_count)
{
    if (UNEXPECTED(handshake == NULL || handshake->host == NULL || handshake->target == NULL ||
                   handshake->key == NULL ||
                   ws_client_contains_crlf(handshake->host) ||
                   ws_client_contains_crlf(handshake->target) ||
                   ws_client_contains_crlf(handshake->key))) {
        return NULL;
    }

    smart_str request = {0};

    smart_str_appendl(&request, "GET ", sizeof("GET ") - 1);
    smart_str_append(&request, handshake->target);
    smart_str_appendl(&request, " HTTP/1.1\r\nHost: ", sizeof(" HTTP/1.1\r\nHost: ") - 1);
    smart_str_append(&request, handshake->host);
    smart_str_appendl(&request, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ",
                      sizeof("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ") - 1);
    smart_str_append(&request, handshake->key);
    smart_str_appendl(&request, "\r\nSec-WebSocket-Version: 13\r\n", sizeof("\r\nSec-WebSocket-Version: 13\r\n") - 1);

    if (subprotocol_count > 0) {
        smart_str_appendl(&request, "Sec-WebSocket-Protocol: ", sizeof("Sec-WebSocket-Protocol: ") - 1);

        for (size_t index = 0; index < subprotocol_count; index++) {
            if (UNEXPECTED(!ws_client_is_token(subprotocols[index]))) {
                smart_str_free(&request);
                return NULL;
            }

            if (index > 0) {
                smart_str_appendl(&request, ", ", 2);
            }

            smart_str_append(&request, subprotocols[index]);
        }

        smart_str_appendl(&request, "\r\n", 2);
    }

    if (headers != NULL) {
        zend_string *name;
        zval *value;
        ZEND_HASH_FOREACH_STR_KEY_VAL(headers, name, value) {
            if (name == NULL) {
                smart_str_free(&request);
                return NULL;
            }
            if (Z_TYPE_P(value) == IS_STRING) {
                if (!ws_client_append_header(&request, name, Z_STR_P(value))) {
                    smart_str_free(&request);
                    return NULL;
                }
            } else if (Z_TYPE_P(value) == IS_ARRAY) {
                zval *item;
                ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), item) {
                    if (Z_TYPE_P(item) != IS_STRING ||
                        !ws_client_append_header(&request, name, Z_STR_P(item))) {
                        smart_str_free(&request);
                        return NULL;
                    }
                } ZEND_HASH_FOREACH_END();
            } else {
                smart_str_free(&request);
                return NULL;
            }
        } ZEND_HASH_FOREACH_END();
    }

    smart_str_appendl(&request, "\r\n", 2);
    smart_str_0(&request);

    return request.s;
}

zend_string *ws_client_handshake_build_request(const ws_client_handshake_t *const handshake,
                                               const zend_string *const *const subprotocols,
                                               const size_t subprotocol_count)
{
    return ws_client_handshake_build_request_ex(handshake, NULL, subprotocols, subprotocol_count);
}

bool ws_client_handshake_validate_response(const char *const response,
                                           const size_t response_len,
                                           const zend_string *const expected_accept)
{
    if (UNEXPECTED(response == NULL || expected_accept == NULL || response_len < 17 ||
                   memcmp(response, "HTTP/1.1 101 ", sizeof("HTTP/1.1 101 ") - 1) != 0)) {
        return false;
    }

    bool accept_seen = false;
    bool upgrade_seen = false;
    bool connection_seen = false;
    size_t line_start = 0;

    while (line_start < response_len) {
        const char *const line = response + line_start;
        const char *const line_end = memchr(line, '\n', response_len - line_start);

        if (line_end == NULL) {
            return false;
        }

        const size_t line_len = (size_t) (line_end - line);
        line_start += line_len + 1;

        if (line_len == 1 && line[0] == '\r') {
            return accept_seen && upgrade_seen && connection_seen;
        }

        const char *const colon = memchr(line, ':', line_len);
        if (colon == NULL) {
            continue;
        }

        const size_t name_len = (size_t) (colon - line);
        const char *value = colon + 1;
        const char *value_end = line + line_len;

        while (value < value_end && (*value == ' ' || *value == '\t')) {
            value++;
        }

        size_t value_len = (size_t) (value_end - value);
        if (value_len > 0 && value[value_len - 1] == '\r') {
            value_len--;
        }

        if (ws_client_header_name_equals(line, name_len, "Sec-WebSocket-Accept")) {
            /* More than one Accept header is ambiguous and must not be
             * accepted by selecting the last value. */
            if (accept_seen || value_len != ZSTR_LEN(expected_accept) ||
                memcmp(value, ZSTR_VAL(expected_accept), value_len) != 0) {
                return false;
            }
            accept_seen = true;
        } else if (ws_client_header_name_equals(line, name_len, "Upgrade")) {
            upgrade_seen = ws_client_header_has_token(value, value_len, "websocket");
        } else if (ws_client_header_name_equals(line, name_len, "Connection")) {
            connection_seen = ws_client_header_has_token(value, value_len, "Upgrade");
        }
    }

    return false;
}

void ws_client_handshake_release(ws_client_handshake_t *const handshake)
{
    if (handshake == NULL) {
        return;
    }

    if (handshake->host != NULL) {
        zend_string_release(handshake->host);
    }
    if (handshake->target != NULL) {
        zend_string_release(handshake->target);
    }
    if (handshake->key != NULL) {
        zend_string_release(handshake->key);
    }
    if (handshake->expected_accept != NULL) {
        zend_string_release(handshake->expected_accept);
    }
    memset(handshake, 0, sizeof(*handshake));
}

#ifndef WS_CLIENT_HANDSHAKE_TEST
static bool ws_client_transport_send(void *const ctx, const uint8_t *data, size_t len)
{
    ws_client_connection_t *const connection = ctx;
    if (connection == NULL || connection->closed || connection->stream == NULL) {
        return false;
    }

    while (len > 0) {
        const ssize_t written = php_stream_write(connection->stream, (const char *) data, len);
        if (written <= 0) {
            connection->closed = true;
            return false;
        }
        data += written;
        len -= (size_t) written;
    }

    return true;
}

static bool ws_client_find_header_end(const char *const data, const size_t len, size_t *const end)
{
    if (len < 4) {
        return false;
    }

    for (size_t i = 0; i <= len - 4; i++) {
        if (memcmp(data + i, "\r\n\r\n", 4) == 0) {
            *end = i + 4;
            return true;
        }
    }

    return false;
}

static bool ws_client_response_subprotocol(const char *const response, const size_t response_len,
                                           const zend_string *const *const offered,
                                           const size_t offered_count,
                                           zend_string **const selected)
{
    bool seen = false;
    size_t line_start = 0;

    if (selected != NULL) {
        *selected = NULL;
    }
    while (line_start < response_len) {
        const char *const line = response + line_start;
        const char *const newline = memchr(line, '\n', response_len - line_start);
        if (newline == NULL) {
            return false;
        }
        const size_t line_len = (size_t) (newline - line);
        line_start += line_len + 1;
        if (line_len == 1 && line[0] == '\r') {
            return true;
        }
        const char *const colon = memchr(line, ':', line_len);
        if (colon == NULL ||
            !ws_client_header_name_equals(line, (size_t) (colon - line),
                                          "Sec-WebSocket-Protocol")) {
            continue;
        }
        if (seen || offered_count == 0) {
            return false;
        }
        const char *value = colon + 1;
        const char *value_end = line + line_len;
        while (value < value_end && (*value == ' ' || *value == '\t')) {
            value++;
        }
        while (value_end > value && (value_end[-1] == '\r' ||
                                     value_end[-1] == ' ' || value_end[-1] == '\t')) {
            value_end--;
        }
        zend_string *const candidate = zend_string_init(value, (size_t) (value_end - value), 0);
        if (!ws_client_is_token(candidate)) {
            zend_string_release(candidate);
            return false;
        }
        bool offered_match = false;
        for (size_t i = 0; i < offered_count; i++) {
            if (zend_string_equals(candidate, offered[i])) {
                offered_match = true;
                break;
            }
        }
        if (!offered_match) {
            zend_string_release(candidate);
            return false;
        }
        if (selected != NULL) {
            *selected = candidate;
        } else {
            zend_string_release(candidate);
        }
        seen = true;
    }

    return false;
}

static zend_string *ws_client_format_authority(const ws_client_url_t *const url)
{
    const bool ipv6 = memchr(ZSTR_VAL(url->host), ':', ZSTR_LEN(url->host)) != NULL;
    const uint16_t default_port = url->secure ? 443 : 80;
    if (url->port == default_port) {
        return ipv6
            ? strpprintf(0, "[%s]", ZSTR_VAL(url->host))
            : zend_string_copy(url->host);
    }

    return ipv6
        ? strpprintf(0, "[%s]:%u", ZSTR_VAL(url->host), (unsigned) url->port)
        : strpprintf(0, "%s:%u", ZSTR_VAL(url->host), (unsigned) url->port);
}

static php_stream *ws_client_open_stream(const ws_client_url_t *const url)
{
    /* ws:// and wss:// are the only public schemes. PHP maps their already
     * validated WebSocket transport to its internal tcp:// or ssl:// stream
     * provider; these strings never cross the PHP API boundary. */
    const bool ipv6 = memchr(ZSTR_VAL(url->host), ':', ZSTR_LEN(url->host)) != NULL;
    zend_string *const transport = ipv6
        ? strpprintf(0, "%s://[%s]:%u", url->secure ? "ssl" : "tcp",
                     ZSTR_VAL(url->host), (unsigned) url->port)
        : strpprintf(0, "%s://%s:%u", url->secure ? "ssl" : "tcp",
                     ZSTR_VAL(url->host), (unsigned) url->port);
    zend_string *error = NULL;
    int error_code = 0;
    php_stream *const stream = php_stream_xport_create(
        ZSTR_VAL(transport), ZSTR_LEN(transport), REPORT_ERRORS,
        STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT, NULL, NULL, NULL,
        &error, &error_code);
    zend_string_release(transport);

    if (stream == NULL) {
        zend_throw_exception_ex(websocket_exception_ce, error_code,
            "WebSocket connection failed%s%s", error != NULL ? ": " : "",
            error != NULL ? ZSTR_VAL(error) : "");
    }
    if (error != NULL) {
        zend_string_release(error);
    }

    return stream;
}

ws_client_connection_t *ws_client_connection_open(
    const ws_client_url_t *const url, HashTable *const headers,
    const zend_string *const *const subprotocols, const size_t subprotocol_count,
    zend_string **const selected_subprotocol)
{
    if (selected_subprotocol != NULL) {
        *selected_subprotocol = NULL;
    }
    if (url == NULL || url->host == NULL || url->target == NULL) {
        return NULL;
    }
    if (ZEND_ASYNC_CURRENT_COROUTINE == NULL) {
        zend_throw_exception(websocket_exception_ce,
            "WebSocketClient::connect() must run in a TrueAsync coroutine", 0);
        return NULL;
    }

    php_stream *const stream = ws_client_open_stream(url);
    if (stream == NULL) {
        return NULL;
    }

    uint8_t random_key[16];
    if (php_random_bytes_throw(random_key, sizeof(random_key)) == FAILURE) {
        php_stream_close(stream);
        return NULL;
    }
    zend_string *const key = php_base64_encode(random_key, sizeof(random_key));
    zend_string *const authority = ws_client_format_authority(url);
    ws_client_handshake_t handshake = {0};
    zend_string *request = NULL;
    smart_str response = {0};
    ws_client_connection_t *connection = NULL;

    if (key == NULL ||
        !ws_client_handshake_init(&handshake, authority, url->target, key) ||
        (request = ws_client_handshake_build_request_ex(&handshake, headers,
                                                         subprotocols,
                                                         subprotocol_count)) == NULL) {
        zend_throw_exception(websocket_exception_ce, "Cannot prepare WebSocket handshake", 0);
        goto fail;
    }

    connection = ecalloc(1, sizeof(*connection));
    connection->stream = stream;
    if (!ws_client_transport_send(connection, (const uint8_t *) ZSTR_VAL(request),
                                  ZSTR_LEN(request))) {
        zend_throw_exception(websocket_exception_ce, "Cannot write WebSocket handshake", 0);
        goto fail;
    }

    while (response.s == NULL || !ws_client_find_header_end(ZSTR_VAL(response.s),
                                                              ZSTR_LEN(response.s), &(size_t){0})) {
        char buf[WS_CLIENT_READ_CHUNK];
        const ssize_t nread = php_stream_read(stream, buf, sizeof(buf));
        if (nread <= 0) {
            zend_throw_exception(websocket_exception_ce,
                "Connection closed during WebSocket handshake", 0);
            goto fail;
        }
        smart_str_appendl(&response, buf, (size_t) nread);
        if (response.s != NULL && ZSTR_LEN(response.s) > WS_CLIENT_MAX_RESPONSE_HEADERS) {
            zend_throw_exception(websocket_exception_ce, "WebSocket response headers exceed 16 KiB", 0);
            goto fail;
        }
    }

    size_t header_end = 0;
    (void) ws_client_find_header_end(ZSTR_VAL(response.s), ZSTR_LEN(response.s), &header_end);
    if (!ws_client_handshake_validate_response(ZSTR_VAL(response.s), header_end,
                                               handshake.expected_accept)) {
        zend_throw_exception(websocket_exception_ce, "Invalid WebSocket upgrade response", 0);
        goto fail;
    }
    if (!ws_client_response_subprotocol(ZSTR_VAL(response.s), header_end, subprotocols,
                                        subprotocol_count, selected_subprotocol)) {
        zend_throw_exception(websocket_exception_ce, "Invalid WebSocket subprotocol response", 0);
        goto fail;
    }

    connection->session = ws_session_init_client(&ws_client_transport_ops, connection);
    if (connection->session == NULL) {
        zend_throw_exception(websocket_exception_ce, "Cannot initialize WebSocket client session", 0);
        goto fail;
    }
    if (header_end < ZSTR_LEN(response.s) &&
        ws_session_feed(connection->session,
                        (const uint8_t *) ZSTR_VAL(response.s) + header_end,
                        ZSTR_LEN(response.s) - header_end) != 0) {
        zend_throw_exception(websocket_exception_ce, "Invalid WebSocket frame after upgrade", 0);
        goto fail;
    }

    zend_string_release(authority);
    zend_string_release(key);
    zend_string_release(request);
    smart_str_free(&response);
    ws_client_handshake_release(&handshake);
    return connection;

fail:
    if (selected_subprotocol != NULL && *selected_subprotocol != NULL) {
        zend_string_release(*selected_subprotocol);
        *selected_subprotocol = NULL;
    }
    if (connection != NULL) {
        ws_client_connection_close(connection);
    } else {
        php_stream_close(stream);
    }
    if (authority != NULL) zend_string_release(authority);
    if (key != NULL) zend_string_release(key);
    if (request != NULL) zend_string_release(request);
    smart_str_free(&response);
    ws_client_handshake_release(&handshake);
    return NULL;
}

void ws_client_connection_close(ws_client_connection_t *const connection)
{
    if (connection == NULL) {
        return;
    }
    connection->closed = true;
    if (connection->session != NULL) {
        ws_session_mark_peer_closed(connection->session);
        ws_session_destroy(connection->session);
    }
    if (connection->stream != NULL) {
        php_stream_close(connection->stream);
    }
    efree(connection);
}

bool ws_client_connection_recv(ws_client_connection_t *const connection)
{
    if (connection == NULL || connection->closed || connection->stream == NULL ||
        connection->session == NULL) {
        return false;
    }
    uint8_t buf[WS_CLIENT_READ_CHUNK];
    const ssize_t nread = php_stream_read(connection->stream, (char *) buf, sizeof(buf));
    if (nread <= 0) {
        connection->closed = true;
        ws_session_mark_peer_closed(connection->session);
        return false;
    }
    if (ws_session_feed(connection->session, buf, (size_t) nread) != 0) {
        connection->closed = true;
        return false;
    }
    return true;
}

bool ws_client_connection_is_closed(const ws_client_connection_t *const connection)
{
    return connection == NULL || connection->closed;
}

ws_session_t *ws_client_connection_session(ws_client_connection_t *const connection)
{
    return connection != NULL ? connection->session : NULL;
}

static zend_class_entry *websocket_client_config_ce;

ZEND_METHOD(TrueAsync_WebSocketClient, __construct)
{
    ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(TrueAsync_WebSocketClientConfig, __construct)
{
    ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(TrueAsync_WebSocketClient, connect)
{
    zend_string *url;
    HashTable *headers = NULL;
    HashTable *protocols = NULL;
    zval *config = NULL;

    ZEND_PARSE_PARAMETERS_START(1, 4)
        Z_PARAM_STR(url)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY(headers)
        Z_PARAM_ARRAY(protocols)
        Z_PARAM_OBJECT_OF_CLASS_OR_NULL(config, websocket_client_config_ce)
    ZEND_PARSE_PARAMETERS_END();

    (void) config; /* Reserved for validated PHP stream/TLS options. */
    ws_client_url_t parsed = {0};
    if (!ws_client_url_parse(url, &parsed)) {
        zend_argument_value_error(1,
            "must be a valid ws:// or wss:// URL without credentials or a fragment");
        RETURN_THROWS();
    }

    const uint32_t count = protocols != NULL ? zend_hash_num_elements(protocols) : 0;
    const zend_string **items = count == 0 ? NULL : safe_emalloc(count, sizeof(*items), 0);
    uint32_t index = 0;
    zval *protocol;
    if (protocols != NULL) {
        ZEND_HASH_FOREACH_VAL(protocols, protocol) {
            if (Z_TYPE_P(protocol) != IS_STRING) {
                if (items != NULL) efree(items);
                ws_client_url_release(&parsed);
                zend_argument_type_error(3, "must contain only strings");
                RETURN_THROWS();
            }
            items[index++] = Z_STR_P(protocol);
        } ZEND_HASH_FOREACH_END();
    }

    zend_string *selected = NULL;
    ws_client_connection_t *connection = ws_client_connection_open(
        &parsed, headers, items, count, &selected);
    if (items != NULL) efree(items);
    ws_client_url_release(&parsed);

    if (connection == NULL) {
        if (selected != NULL) zend_string_release(selected);
        RETURN_THROWS();
    }

    ws_session_t *session = ws_client_connection_session(connection);
    zend_object *object = websocket_object_create_client(session, connection,
                                                           selected, NULL, 0);
    if (selected != NULL) zend_string_release(selected);
    RETURN_OBJ(object);
}

void ws_client_php_classes_register(void)
{
    websocket_client_config_ce = register_class_TrueAsync_WebSocketClientConfig();
    (void) register_class_TrueAsync_WebSocketClient();
}
#endif
