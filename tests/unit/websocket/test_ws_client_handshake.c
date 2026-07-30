/* Client handshake regression coverage is compiled by the extension test build. */

#include <assert.h>
#include <string.h>

#include "php.h"
#include "common/php_sapi_test.h"
#include "websocket/ws_client.h"

static void test_client_request_contains_required_headers(void)
{
    ws_client_handshake_t handshake = {0};
    zend_string *const host = zend_string_init("example.test", sizeof("example.test") - 1, 0);
    zend_string *const target = zend_string_init("/socket", sizeof("/socket") - 1, 0);
    zend_string *const key = zend_string_init("dGhlIHNhbXBsZSBub25jZQ==", 24, 0);
    assert(ws_client_handshake_init(&handshake, host, target, key));
    zend_string_release(host);
    zend_string_release(target);
    zend_string_release(key);

    zend_string *const request = ws_client_handshake_build_request(&handshake, NULL, 0);

    assert(request != NULL);
    assert(strstr(ZSTR_VAL(request), "Upgrade: websocket\r\n") != NULL);
    assert(strstr(ZSTR_VAL(request), "Connection: Upgrade\r\n") != NULL);
    assert(strstr(ZSTR_VAL(request), "Sec-WebSocket-Version: 13\r\n") != NULL);

    zend_string_release(request);
    ws_client_handshake_release(&handshake);
}

static void test_client_rejects_wrong_accept_value(void)
{
    const char response[] = "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: wrong\r\n\r\n";
    zend_string *const expected = zend_string_init("correct", sizeof("correct") - 1, 0);

    assert(!ws_client_handshake_validate_response(response, sizeof(response) - 1, expected));

    zend_string_release(expected);
}

static void test_client_accepts_complete_valid_response(void)
{
    const char response[] = "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: WebSocket\r\n"
                            "Connection: keep-alive, Upgrade\r\n"
                            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    zend_string *const expected = zend_string_init("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
                                                   sizeof("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") - 1, 0);

    assert(ws_client_handshake_validate_response(response, sizeof(response) - 1, expected));
    zend_string_release(expected);
}

static void test_client_requires_all_upgrade_headers(void)
{
    const char response[] = "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Sec-WebSocket-Accept: correct\r\n\r\n";
    zend_string *const expected = zend_string_init("correct", sizeof("correct") - 1, 0);

    assert(!ws_client_handshake_validate_response(response, sizeof(response) - 1, expected));
    zend_string_release(expected);
}

static void test_client_rejects_header_injection(void)
{
    ws_client_handshake_t handshake = {0};
    zend_string *const host = zend_string_init("example.test\r\nX-Injected: 1",
                                               sizeof("example.test\r\nX-Injected: 1") - 1, 0);
    zend_string *const target = zend_string_init("/socket", sizeof("/socket") - 1, 0);
    zend_string *const key = zend_string_init("dGhlIHNhbXBsZSBub25jZQ==", 24, 0);

    assert(!ws_client_handshake_init(&handshake, host, target, key));
    zend_string_release(host);
    zend_string_release(target);
    zend_string_release(key);
}

static void test_client_renders_safe_custom_headers(void)
{
    ws_client_handshake_t handshake = {0};
    zend_string *const host = zend_string_init("example.test", sizeof("example.test") - 1, 0);
    zend_string *const target = zend_string_init("/socket", sizeof("/socket") - 1, 0);
    zend_string *const key = zend_string_init("dGhlIHNhbXBsZSBub25jZQ==", 24, 0);
    HashTable headers;
    zval value;

    assert(ws_client_handshake_init(&handshake, host, target, key));
    zend_string_release(host);
    zend_string_release(target);
    zend_string_release(key);
    zend_hash_init(&headers, 2, NULL, ZVAL_PTR_DTOR, 0);
    ZVAL_STRING(&value, "trace-123");
    zend_hash_str_update(&headers, ZEND_STRL("X-Trace-Id"), &value);

    zend_string *const request =
        ws_client_handshake_build_request_ex(&handshake, &headers, NULL, 0);
    assert(request != NULL);
    assert(strstr(ZSTR_VAL(request), "X-Trace-Id: trace-123\r\n") != NULL);
    zend_string_release(request);
    zend_hash_destroy(&headers);

    zend_hash_init(&headers, 2, NULL, ZVAL_PTR_DTOR, 0);
    ZVAL_STRING(&value, "attacker.example");
    zend_hash_str_update(&headers, ZEND_STRL("Host"), &value);
    assert(ws_client_handshake_build_request_ex(&handshake, &headers, NULL, 0) == NULL);
    zend_hash_destroy(&headers);
    ws_client_handshake_release(&handshake);
}

static void test_client_parses_secure_ipv6_url(void)
{
    zend_string *const input = zend_string_init("wss://[2001:db8::1]:9443/socket?q=1",
                                                sizeof("wss://[2001:db8::1]:9443/socket?q=1") - 1, 0);
    ws_client_url_t parsed = {0};

    assert(ws_client_url_parse(input, &parsed));
    assert(parsed.secure && parsed.port == 9443);
    assert(strcmp(ZSTR_VAL(parsed.host), "2001:db8::1") == 0);
    assert(strcmp(ZSTR_VAL(parsed.target), "/socket?q=1") == 0);
    ws_client_url_release(&parsed);
    zend_string_release(input);
}

int main(void)
{
    if (php_test_runtime_init() != SUCCESS) {
        return 1;
    }

    test_client_request_contains_required_headers();
    test_client_rejects_wrong_accept_value();
    test_client_accepts_complete_valid_response();
    test_client_requires_all_upgrade_headers();
    test_client_rejects_header_injection();
    test_client_renders_safe_custom_headers();
    test_client_parses_secure_ipv6_url();

    php_test_runtime_shutdown();
    return 0;
}
