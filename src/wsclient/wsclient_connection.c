/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#include "php.h"
#include "ext/random/php_random_csprng.h"
#include "ext/standard/base64.h"
#include "ext/standard/url.h"
#include "main/php_streams.h"
#include "zend_exceptions.h"
#include "zend_smart_str.h"

#include <stddef.h>

#include "wsclient/php_wsclient.h"
#include "wsclient/wsclient_connection.h"
#include "wsclient/wsclient_handshake.h"

#define WSCLIENT_READ_CHUNK 8192u
#define WSCLIENT_MAX_HEADERS (16u * 1024u)

static zend_object_handlers wsclient_connection_handlers;
static zend_object_handlers wsclient_message_handlers;

wsclient_connection_object *wsclient_connection_from_obj(zend_object *object)
{
    return (wsclient_connection_object *)((char *) object - offsetof(wsclient_connection_object, std));
}

wsclient_message_object *wsclient_message_from_obj(zend_object *object)
{
    return (wsclient_message_object *)((char *) object - offsetof(wsclient_message_object, std));
}

static void wsclient_connection_free(zend_object *object)
{
    wsclient_connection_object *connection = wsclient_connection_from_obj(object);
    if (connection->session != NULL) wsclient_session_destroy(connection->session);
    if (connection->stream != NULL) php_stream_close(connection->stream);
    if (connection->subprotocol != NULL) zend_string_release(connection->subprotocol);
    zend_object_std_dtor(&connection->std);
}

static void wsclient_message_free(zend_object *object)
{
    wsclient_message_object *message = wsclient_message_from_obj(object);
    if (message->data != NULL) zend_string_release(message->data);
    zend_object_std_dtor(&message->std);
}

zend_object *wsclient_connection_new(zend_class_entry *ce)
{
    if (wsclient_connection_handlers.free_obj == NULL) {
        wsclient_connection_handlers_init();
    }
    wsclient_connection_object *connection = zend_object_alloc(sizeof(*connection), ce);
    zend_object_std_init(&connection->std, ce);
    object_properties_init(&connection->std, ce);
    connection->std.handlers = &wsclient_connection_handlers;
    return &connection->std;
}

zend_object *wsclient_message_new(zend_class_entry *ce)
{
    if (wsclient_message_handlers.free_obj == NULL) {
        wsclient_connection_handlers_init();
    }
    wsclient_message_object *message = zend_object_alloc(sizeof(*message), ce);
    zend_object_std_init(&message->std, ce);
    object_properties_init(&message->std, ce);
    message->std.handlers = &wsclient_message_handlers;
    return &message->std;
}

void wsclient_connection_handlers_init(void)
{
    memcpy(&wsclient_connection_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    wsclient_connection_handlers.offset = offsetof(wsclient_connection_object, std);
    wsclient_connection_handlers.free_obj = wsclient_connection_free;
    memcpy(&wsclient_message_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    wsclient_message_handlers.offset = offsetof(wsclient_message_object, std);
    wsclient_message_handlers.free_obj = wsclient_message_free;
}

zend_object *wsclient_message_create(zend_string *data, bool binary)
{
    zend_object *object = wsclient_message_new(wsclient_message_ce);
    wsclient_message_object *message = wsclient_message_from_obj(object);
    message->data = data;
    message->binary = binary;
    return object;
}

static bool wsclient_contains_crlf(const zend_string *value)
{
    return value == NULL || memchr(ZSTR_VAL(value), '\r', ZSTR_LEN(value)) != NULL ||
        memchr(ZSTR_VAL(value), '\n', ZSTR_LEN(value)) != NULL;
}

static bool wsclient_token(const zend_string *value)
{
    size_t i;
    if (value == NULL || ZSTR_LEN(value) == 0) return false;
    for (i = 0; i < ZSTR_LEN(value); i++) {
        unsigned char c = (unsigned char) ZSTR_VAL(value)[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || strchr("!#$%&'*+-.^_`|~", c) != NULL)) return false;
    }
    return true;
}

static bool wsclient_name_eq(const char *name, size_t length, const char *expected)
{
    return length == strlen(expected) && zend_binary_strcasecmp(name, length, expected, strlen(expected)) == 0;
}

static bool wsclient_reserved_header(zend_string *name)
{
    return wsclient_name_eq(ZSTR_VAL(name), ZSTR_LEN(name), "Host") ||
        wsclient_name_eq(ZSTR_VAL(name), ZSTR_LEN(name), "Connection") ||
        wsclient_name_eq(ZSTR_VAL(name), ZSTR_LEN(name), "Upgrade") ||
        wsclient_name_eq(ZSTR_VAL(name), ZSTR_LEN(name), "Sec-WebSocket-Key") ||
        wsclient_name_eq(ZSTR_VAL(name), ZSTR_LEN(name), "Sec-WebSocket-Version") ||
        wsclient_name_eq(ZSTR_VAL(name), ZSTR_LEN(name), "Sec-WebSocket-Protocol");
}

static bool wsclient_append_headers(smart_str *request, HashTable *headers)
{
    zend_string *name;
    zval *value;
    if (headers == NULL) return true;
    ZEND_HASH_FOREACH_STR_KEY_VAL(headers, name, value) {
        if (name == NULL || !wsclient_token(name) || wsclient_reserved_header(name)) return false;
        if (Z_TYPE_P(value) != IS_STRING || wsclient_contains_crlf(Z_STR_P(value))) return false;
        smart_str_append(request, name);
        smart_str_appendl(request, ": ", 2);
        smart_str_append(request, Z_STR_P(value));
        smart_str_appendl(request, "\r\n", 2);
    } ZEND_HASH_FOREACH_END();
    return true;
}

static zend_string *wsclient_build_request(zend_string *authority, zend_string *target,
    zend_string *key, HashTable *headers, HashTable *protocols)
{
    smart_str request = {0};
    zval *protocol;
    uint32_t index = 0;
    if (wsclient_contains_crlf(authority) || wsclient_contains_crlf(target) ||
        ZSTR_LEN(target) == 0 || ZSTR_VAL(target)[0] != '/') return NULL;
    smart_str_appendl(&request, "GET ", 4); smart_str_append(&request, target);
    smart_str_appendl(&request, " HTTP/1.1\r\nHost: ", 17); smart_str_append(&request, authority);
    smart_str_appendl(&request, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ", 62);
    smart_str_append(&request, key);
    smart_str_appendl(&request, "\r\nSec-WebSocket-Version: 13\r\n", 29);
    if (protocols != NULL && zend_hash_num_elements(protocols) > 0) {
        smart_str_appendl(&request, "Sec-WebSocket-Protocol: ", 24);
        ZEND_HASH_FOREACH_VAL(protocols, protocol) {
            if (Z_TYPE_P(protocol) != IS_STRING || !wsclient_token(Z_STR_P(protocol))) {
                smart_str_free(&request); return NULL;
            }
            if (index++ > 0) smart_str_appendl(&request, ", ", 2);
            smart_str_append(&request, Z_STR_P(protocol));
        } ZEND_HASH_FOREACH_END();
        smart_str_appendl(&request, "\r\n", 2);
    }
    if (!wsclient_append_headers(&request, headers)) { smart_str_free(&request); return NULL; }
    smart_str_appendl(&request, "\r\n", 2); smart_str_0(&request);
    return request.s;
}

static bool wsclient_header_end(const char *data, size_t length, size_t *end)
{
    size_t i;
    for (i = 0; i + 4 <= length; i++) if (memcmp(data + i, "\r\n\r\n", 4) == 0) {
        *end = i + 4; return true;
    }
    return false;
}

static bool wsclient_header_token(const char *value, size_t length, const char *token)
{
    const char *end = value + length;
    while (value < end) {
        const char *part; const char *part_end;
        while (value < end && (*value == ' ' || *value == '\t' || *value == ',')) value++;
        part = value; while (value < end && *value != ',') value++;
        part_end = value; while (part_end > part && (part_end[-1] == ' ' || part_end[-1] == '\t')) part_end--;
        if (wsclient_name_eq(part, (size_t)(part_end - part), token)) return true;
    }
    return false;
}

static bool wsclient_validate_response(const char *response, size_t length, zend_string *expected)
{
    bool accept = false, upgrade = false, connection = false;
    size_t start = 0;
    if (length < 17 || memcmp(response, "HTTP/1.1 101 ", 13) != 0) return false;
    while (start < length) {
        const char *line = response + start, *nl = memchr(line, '\n', length - start), *colon;
        size_t line_len, value_len; const char *value;
        if (nl == NULL) return false;
        line_len = (size_t)(nl - line); start += line_len + 1;
        if (line_len == 1 && line[0] == '\r') return accept && upgrade && connection;
        colon = memchr(line, ':', line_len); if (colon == NULL) continue;
        value = colon + 1; while (value < line + line_len && (*value == ' ' || *value == '\t')) value++;
        value_len = (size_t)(line + line_len - value); if (value_len && value[value_len - 1] == '\r') value_len--;
        if (wsclient_name_eq(line, (size_t)(colon - line), "Sec-WebSocket-Accept")) {
            if (accept || value_len != ZSTR_LEN(expected) || memcmp(value, ZSTR_VAL(expected), value_len)) return false;
            accept = true;
        } else if (wsclient_name_eq(line, (size_t)(colon - line), "Upgrade")) upgrade = wsclient_header_token(value, value_len, "websocket");
        else if (wsclient_name_eq(line, (size_t)(colon - line), "Connection")) connection = wsclient_header_token(value, value_len, "Upgrade");
    }
    return false;
}

static php_stream *wsclient_open_stream(zend_string *host, uint16_t port, bool secure)
{
    bool ipv6 = memchr(ZSTR_VAL(host), ':', ZSTR_LEN(host)) != NULL;
    zend_string *transport = ipv6 ? strpprintf(0, "%s://[%s]:%u", secure ? "ssl" : "tcp", ZSTR_VAL(host), (unsigned)port)
        : strpprintf(0, "%s://%s:%u", secure ? "ssl" : "tcp", ZSTR_VAL(host), (unsigned)port);
    zend_string *error = NULL; int code = 0;
    php_stream *stream = php_stream_xport_create(ZSTR_VAL(transport), ZSTR_LEN(transport), REPORT_ERRORS,
        STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT, NULL, NULL, NULL, &error, &code);
    zend_string_release(transport);
    if (stream == NULL) zend_throw_exception_ex(wsclient_exception_ce, code, "WebSocket connection failed%s%s",
        error ? ": " : "", error ? ZSTR_VAL(error) : "");
    if (error != NULL) zend_string_release(error);
    return stream;
}

zend_object *wsclient_connection_create(zend_string *url, HashTable *headers, HashTable *protocols)
{
    bool has_port = false;
    php_url *parsed = php_url_parse_ex2(ZSTR_VAL(url), ZSTR_LEN(url), &has_port);
    bool secure; uint16_t port; zend_string *host = NULL, *target = NULL, *authority = NULL, *key = NULL, *request = NULL, *expected = NULL;
    php_stream *stream = NULL; smart_str response = {0}; size_t header_end = 0; zend_object *object = NULL;
    if (parsed == NULL || parsed->scheme == NULL || parsed->host == NULL || parsed->user || parsed->pass || parsed->fragment ||
        (!zend_string_equals_literal_ci(parsed->scheme, "ws") && !zend_string_equals_literal_ci(parsed->scheme, "wss"))) {
        if (parsed) php_url_free(parsed); zend_argument_value_error(1, "must be a valid ws:// or wss:// URL without credentials or a fragment"); return NULL;
    }
    secure = zend_string_equals_literal_ci(parsed->scheme, "wss"); port = has_port ? parsed->port : (secure ? 443 : 80);
    host = zend_string_copy(parsed->host);
    if (wsclient_contains_crlf(host) || (parsed->path && wsclient_contains_crlf(parsed->path)) || (parsed->query && wsclient_contains_crlf(parsed->query))) goto invalid;
    if (parsed->path == NULL || ZSTR_LEN(parsed->path) == 0) target = zend_string_init("/", 1, 0); else target = zend_string_copy(parsed->path);
    if (parsed->query) target = zend_string_extend(target, ZSTR_LEN(target) + 1 + ZSTR_LEN(parsed->query), 0), ZSTR_VAL(target)[ZSTR_LEN(target) - ZSTR_LEN(parsed->query) - 1] = '?', memcpy(ZSTR_VAL(target) + ZSTR_LEN(target) - ZSTR_LEN(parsed->query), ZSTR_VAL(parsed->query), ZSTR_LEN(parsed->query));
    php_url_free(parsed); parsed = NULL;
    authority = (memchr(ZSTR_VAL(host), ':', ZSTR_LEN(host)) && port != (secure ? 443 : 80)) ? strpprintf(0, "[%s]:%u", ZSTR_VAL(host), (unsigned)port)
        : (memchr(ZSTR_VAL(host), ':', ZSTR_LEN(host)) ? strpprintf(0, "[%s]", ZSTR_VAL(host)) : (port == (secure ? 443 : 80) ? zend_string_copy(host) : strpprintf(0, "%s:%u", ZSTR_VAL(host), (unsigned)port)));
    stream = wsclient_open_stream(host, port, secure); if (!stream) goto done;
    { uint8_t bytes[16]; if (php_random_bytes_throw(bytes, sizeof(bytes)) == FAILURE) goto done; key = php_base64_encode(bytes, sizeof(bytes)); }
    expected = zend_string_alloc(WSCLIENT_ACCEPT_LENGTH, 0);
    if (key == NULL || !wsclient_compute_accept(ZSTR_VAL(key), ZSTR_LEN(key), ZSTR_VAL(expected))) {
        zend_throw_exception(wsclient_exception_ce, "Cannot prepare WebSocket handshake", 0);
        goto done;
    }
    request = wsclient_build_request(authority, target, key, headers, protocols); if (!request) { zend_throw_exception(wsclient_exception_ce, "Invalid WebSocket headers or subprotocols", 0); goto done; }
    { size_t sent = 0; while (sent < ZSTR_LEN(request)) { ssize_t n = php_stream_write(stream, ZSTR_VAL(request) + sent, ZSTR_LEN(request) - sent); if (n <= 0) { zend_throw_exception(wsclient_exception_ce, "Cannot write WebSocket handshake", 0); goto done; } sent += (size_t)n; } }
    while (!wsclient_header_end(response.s ? ZSTR_VAL(response.s) : "", response.s ? ZSTR_LEN(response.s) : 0, &header_end)) {
        char buffer[WSCLIENT_READ_CHUNK]; ssize_t n = php_stream_read(stream, buffer, sizeof(buffer));
        if (n <= 0) { zend_throw_exception(wsclient_exception_ce, "Connection closed during WebSocket handshake", 0); goto done; }
        smart_str_appendl(&response, buffer, (size_t)n);
        if (ZSTR_LEN(response.s) > WSCLIENT_MAX_HEADERS) { zend_throw_exception(wsclient_exception_ce, "WebSocket response headers exceed 16 KiB", 0); goto done; }
    }
    if (!wsclient_validate_response(ZSTR_VAL(response.s), header_end, expected)) { zend_throw_exception(wsclient_exception_ce, "Invalid WebSocket upgrade response", 0); goto done; }
    object = wsclient_connection_new(wsclient_connection_ce);
    { wsclient_connection_object *connection = wsclient_connection_from_obj(object); connection->stream = stream; stream = NULL; connection->session = wsclient_session_create(connection->stream); if (!connection->session) { zend_throw_exception(wsclient_exception_ce, "Cannot initialize WebSocket session", 0); OBJ_RELEASE(object); object = NULL; goto done; } if (header_end < ZSTR_LEN(response.s) && wsclient_session_feed(connection->session, (const uint8_t *)ZSTR_VAL(response.s) + header_end, ZSTR_LEN(response.s) - header_end) < 0) { zend_throw_exception(wsclient_exception_ce, "Invalid WebSocket frame after upgrade", 0); OBJ_RELEASE(object); object = NULL; } }
done:
    if (stream) php_stream_close(stream); if (host) zend_string_release(host); if (target) zend_string_release(target); if (authority) zend_string_release(authority); if (key) zend_string_release(key); if (expected) zend_string_release(expected); if (request) zend_string_release(request); smart_str_free(&response); if (parsed) php_url_free(parsed); return object;
invalid:
    php_url_free(parsed); zend_string_release(host); zend_argument_value_error(1, "must not contain CR or LF"); return NULL;
}
