/*
  +----------------------------------------------------------------------+
  | Copyright (c) TrueAsync                                              |
  +----------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0                       |
  +----------------------------------------------------------------------+
*/

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include "php.h"
#include "websocket/ws_session.h"

typedef struct ws_client_connection_t ws_client_connection_t;

/* RFC 6455 uses the same accept-value calculation on both ends of the
 * handshake.  Keep this small client helper independent from the transport:
 * callers may use it with plain TCP, TLS, or a test socket. */

typedef struct ws_client_handshake_t {
    zend_string *host;
    zend_string *target;
    zend_string *key;
    zend_string *expected_accept;
} ws_client_handshake_t;

typedef struct ws_client_url_t {
    zend_string *host;
    zend_string *target;
    uint16_t port;
    bool secure;
} ws_client_url_t;

bool ws_client_url_parse(const zend_string *url, ws_client_url_t *out);
void ws_client_url_release(ws_client_url_t *url);

/* Initialise an owned handshake from the HTTP authority, request target and
 * already-base64-encoded Sec-WebSocket-Key.  The key must represent 16 random
 * bytes (24 characters on the wire).  Returns false for invalid or unsafe
 * request components.  Call ws_client_handshake_release() on success. */
bool ws_client_handshake_init(ws_client_handshake_t *handshake,
                              const zend_string *host,
                              const zend_string *target,
                              const zend_string *key);

zend_string *ws_client_handshake_build_request(const ws_client_handshake_t *handshake,
                                               const zend_string *const *subprotocols,
                                               size_t subprotocol_count);
zend_string *ws_client_handshake_build_request_ex(const ws_client_handshake_t *handshake,
                                                  HashTable *headers,
                                                  const zend_string *const *subprotocols,
                                                  size_t subprotocol_count);

bool ws_client_handshake_validate_response(const char *response,
                                           size_t response_len,
                                           const zend_string *expected_accept);

void ws_client_handshake_release(ws_client_handshake_t *handshake);

ws_client_connection_t *ws_client_connection_open(const ws_client_url_t *url,
                                                   HashTable *headers,
                                                   const zend_string *const *subprotocols,
                                                   size_t subprotocol_count,
                                                   zend_string **selected_subprotocol);
void ws_client_connection_close(ws_client_connection_t *connection);
bool ws_client_connection_recv(ws_client_connection_t *connection);
bool ws_client_connection_is_closed(const ws_client_connection_t *connection);
ws_session_t *ws_client_connection_session(ws_client_connection_t *connection);
void ws_client_php_classes_register(void);

#endif /* WS_CLIENT_H */
