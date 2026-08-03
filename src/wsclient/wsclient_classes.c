/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#include "php.h"
#include "zend_exceptions.h"

#include "wsclient/php_wsclient.h"
#include "wsclient/wsclient_connection.h"
#include "wsclient/wsclient_session.h"

zend_class_entry *wsclient_ce = NULL;
zend_class_entry *wsclient_connection_ce = NULL;
zend_class_entry *wsclient_message_ce = NULL;
zend_class_entry *wsclient_exception_ce = NULL;

ZEND_BEGIN_ARG_INFO_EX(arginfo_wsclient_none, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_wsclient_connect, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, url, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, headers, IS_ARRAY, 0, "[]")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, subprotocols, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_wsclient_send, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, binary, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

ZEND_METHOD(TrueAsync_WebSocketClient, __construct)
{
    ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(TrueAsync_WebSocketClientConnection, __construct)
{
    ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(TrueAsync_WebSocketClientMessage, __construct)
{
    ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(TrueAsync_WebSocketClient, connect)
{
    zend_string *url;
    HashTable *headers = NULL;
    HashTable *subprotocols = NULL;

    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STR(url)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_HT(headers)
        Z_PARAM_ARRAY_HT(subprotocols)
    ZEND_PARSE_PARAMETERS_END();

    zend_object *connection = wsclient_connection_create(url, headers, subprotocols);
    if (connection == NULL) {
        if (!EG(exception)) {
            zend_throw_exception(wsclient_exception_ce, "Cannot establish WebSocket connection", 0);
        }
        RETURN_THROWS();
    }
    RETURN_OBJ(connection);
}

ZEND_METHOD(TrueAsync_WebSocketClientConnection, send)
{
    zend_string *data;
    zend_bool binary = 0;
    wsclient_connection_object *connection = wsclient_connection_from_obj(Z_OBJ_P(ZEND_THIS));
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STR(data)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(binary)
    ZEND_PARSE_PARAMETERS_END();
    if (connection->closed || connection->session == NULL ||
        wsclient_session_send(connection->session, binary ? WSLAY_BINARY_FRAME : WSLAY_TEXT_FRAME,
                              ZSTR_VAL(data), ZSTR_LEN(data)) != 0) {
        connection->closed = true;
        zend_throw_exception(wsclient_exception_ce, "Cannot send WebSocket message", 0);
        RETURN_THROWS();
    }
    RETURN_NULL();
}

ZEND_METHOD(TrueAsync_WebSocketClientConnection, receive)
{
    wsclient_connection_object *connection = wsclient_connection_from_obj(Z_OBJ_P(ZEND_THIS));
    wsclient_message_node_t *node;
    ZEND_PARSE_PARAMETERS_NONE();
    if (connection->session == NULL || connection->closed) {
        RETURN_NULL();
    }
    node = wsclient_session_pop(connection->session);
    while (node == NULL) {
        char buffer[8192];
        ssize_t read = php_stream_read(connection->stream, buffer, sizeof(buffer));
        if (read <= 0 || wsclient_session_feed(connection->session, (const uint8_t *) buffer,
                                                (size_t) read) < 0) {
            connection->closed = true;
            RETURN_NULL();
        }
        node = wsclient_session_pop(connection->session);
    }
    {
        zend_object *message = wsclient_message_create(node->data, node->binary);
        efree(node);
        RETURN_OBJ(message);
    }
}

ZEND_METHOD(TrueAsync_WebSocketClientConnection, close)
{
    wsclient_connection_object *connection = wsclient_connection_from_obj(Z_OBJ_P(ZEND_THIS));
    ZEND_PARSE_PARAMETERS_NONE();
    if (!connection->closed && connection->session != NULL) {
        (void) wsclient_session_send(connection->session, WSLAY_CONNECTION_CLOSE, NULL, 0);
    }
    connection->closed = true;
    if (connection->stream != NULL) {
        php_stream_close(connection->stream);
        connection->stream = NULL;
    }
    RETURN_NULL();
}

ZEND_METHOD(TrueAsync_WebSocketClientConnection, isClosed)
{
    wsclient_connection_object *connection = wsclient_connection_from_obj(Z_OBJ_P(ZEND_THIS));
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(connection->closed);
}

ZEND_METHOD(TrueAsync_WebSocketClientConnection, getSubprotocol)
{
    wsclient_connection_object *connection = wsclient_connection_from_obj(Z_OBJ_P(ZEND_THIS));
    ZEND_PARSE_PARAMETERS_NONE();
    if (connection->subprotocol == NULL) RETURN_NULL();
    RETURN_STR_COPY(connection->subprotocol);
}

ZEND_METHOD(TrueAsync_WebSocketClientMessage, data)
{
    wsclient_message_object *message = wsclient_message_from_obj(Z_OBJ_P(ZEND_THIS));
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_STR_COPY(message->data);
}

ZEND_METHOD(TrueAsync_WebSocketClientMessage, isBinary)
{
    wsclient_message_object *message = wsclient_message_from_obj(Z_OBJ_P(ZEND_THIS));
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(message->binary);
}

static const zend_function_entry wsclient_methods[] = {
    ZEND_ME(TrueAsync_WebSocketClient, __construct, arginfo_wsclient_none, ZEND_ACC_PRIVATE)
    ZEND_ME(TrueAsync_WebSocketClient, connect, arginfo_wsclient_connect, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    ZEND_FE_END
};

static const zend_function_entry wsclient_connection_methods[] = {
    ZEND_ME(TrueAsync_WebSocketClientConnection, __construct, arginfo_wsclient_none, ZEND_ACC_PRIVATE)
    ZEND_ME(TrueAsync_WebSocketClientConnection, send, arginfo_wsclient_send, ZEND_ACC_PUBLIC)
    ZEND_ME(TrueAsync_WebSocketClientConnection, receive, arginfo_wsclient_none, ZEND_ACC_PUBLIC)
    ZEND_ME(TrueAsync_WebSocketClientConnection, close, arginfo_wsclient_none, ZEND_ACC_PUBLIC)
    ZEND_ME(TrueAsync_WebSocketClientConnection, isClosed, arginfo_wsclient_none, ZEND_ACC_PUBLIC)
    ZEND_ME(TrueAsync_WebSocketClientConnection, getSubprotocol, arginfo_wsclient_none, ZEND_ACC_PUBLIC)
    ZEND_FE_END
};

static const zend_function_entry wsclient_message_methods[] = {
    ZEND_ME(TrueAsync_WebSocketClientMessage, __construct, arginfo_wsclient_none, ZEND_ACC_PRIVATE)
    ZEND_ME(TrueAsync_WebSocketClientMessage, data, arginfo_wsclient_none, ZEND_ACC_PUBLIC)
    ZEND_ME(TrueAsync_WebSocketClientMessage, isBinary, arginfo_wsclient_none, ZEND_ACC_PUBLIC)
    ZEND_FE_END
};

void wsclient_register_classes(void)
{
    zend_class_entry ce;

    INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClientException", NULL);
    wsclient_exception_ce = zend_register_internal_class_ex(&ce, zend_ce_exception);

    INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClient", wsclient_methods);
    wsclient_ce = zend_register_internal_class_with_flags(&ce, NULL,
        ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES);

    INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClientConnection",
                        wsclient_connection_methods);
    wsclient_connection_ce = zend_register_internal_class_with_flags(&ce, NULL,
        ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES);
    wsclient_connection_ce->create_object = wsclient_connection_new;

    INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClientMessage", wsclient_message_methods);
    wsclient_message_ce = zend_register_internal_class_with_flags(&ce, NULL,
        ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES);
    wsclient_message_ce->create_object = wsclient_message_new;

}
