/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#ifndef TRUE_ASYNC_WSCLIENT_SESSION_H
#define TRUE_ASYNC_WSCLIENT_SESSION_H

#include "php.h"
#include <wslay/wslay.h>

typedef struct _php_stream php_stream;
typedef struct wsclient_message_node_t {
    struct wsclient_message_node_t *next;
    zend_string *data;
    bool binary;
} wsclient_message_node_t;

typedef struct wsclient_session_t {
    wslay_event_context_ptr context;
    php_stream *stream;
    const uint8_t *input;
    size_t input_len;
    size_t input_pos;
    wsclient_message_node_t *messages_head;
    wsclient_message_node_t *messages_tail;
    bool closed;
} wsclient_session_t;

wsclient_session_t *wsclient_session_create(php_stream *stream);
void wsclient_session_destroy(wsclient_session_t *session);
int wsclient_session_feed(wsclient_session_t *session, const uint8_t *data, size_t len);
wsclient_message_node_t *wsclient_session_pop(wsclient_session_t *session);
int wsclient_session_send(wsclient_session_t *session, uint8_t opcode,
                          const char *data, size_t len);

#endif
