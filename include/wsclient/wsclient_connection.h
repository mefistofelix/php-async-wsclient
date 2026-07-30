/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#ifndef TRUE_ASYNC_WSCLIENT_CONNECTION_H
#define TRUE_ASYNC_WSCLIENT_CONNECTION_H

#include "php.h"
#include "main/php_streams.h"
#include "wsclient/wsclient_session.h"

typedef struct wsclient_connection_object {
    php_stream *stream;
    wsclient_session_t *session;
    zend_string *subprotocol;
    bool closed;
    zend_object std;
} wsclient_connection_object;

typedef struct wsclient_message_object {
    zend_string *data;
    bool binary;
    zend_object std;
} wsclient_message_object;

zend_object *wsclient_connection_create(zend_string *url, HashTable *headers,
    HashTable *subprotocols);
zend_object *wsclient_message_create(zend_string *data, bool binary);
zend_object *wsclient_connection_new(zend_class_entry *ce);
zend_object *wsclient_message_new(zend_class_entry *ce);
wsclient_connection_object *wsclient_connection_from_obj(zend_object *object);
wsclient_message_object *wsclient_message_from_obj(zend_object *object);
void wsclient_connection_handlers_init(void);

#endif
