/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#ifndef TRUE_ASYNC_WSCLIENT_HANDSHAKE_H
#define TRUE_ASYNC_WSCLIENT_HANDSHAKE_H

#include "php.h"

#define WSCLIENT_KEY_LENGTH 24u
#define WSCLIENT_ACCEPT_LENGTH 28u

/* RFC 6455 client-only primitive. It intentionally has no HTTP server
 * request dependency, so the client extension can be loaded independently
 * from true_async_server. */
bool wsclient_compute_accept(const char *key, size_t key_len,
                             char out[WSCLIENT_ACCEPT_LENGTH]);

#endif
