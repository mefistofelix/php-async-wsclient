/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#include "php.h"
#include "zend_exceptions.h"

#include "wsclient/php_wsclient.h"

zend_class_entry *wsclient_ce = NULL;
zend_class_entry *wsclient_connection_ce = NULL;
zend_class_entry *wsclient_message_ce = NULL;
zend_class_entry *wsclient_exception_ce = NULL;

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

    (void) url;
    (void) headers;
    (void) subprotocols;
    zend_throw_exception(wsclient_exception_ce,
        "WebSocket client transport is not initialized", 0);
    RETURN_THROWS();
}

static const zend_function_entry wsclient_methods[] = {
    ZEND_ME(TrueAsync_WebSocketClient, __construct, NULL, ZEND_ACC_PRIVATE)
    ZEND_ME(TrueAsync_WebSocketClient, connect, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    ZEND_FE_END
};

static const zend_function_entry wsclient_connection_methods[] = {
    ZEND_ME(TrueAsync_WebSocketClientConnection, __construct, NULL, ZEND_ACC_PRIVATE)
    ZEND_FE_END
};

static const zend_function_entry wsclient_message_methods[] = {
    ZEND_ME(TrueAsync_WebSocketClientMessage, __construct, NULL, ZEND_ACC_PRIVATE)
    ZEND_FE_END
};

void wsclient_register_classes(void)
{
    zend_class_entry ce;

    INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClientException", NULL);
    wsclient_exception_ce = zend_register_internal_class_ex(&ce, zend_ce_exception);

    INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClient", wsclient_methods);
    wsclient_ce = zend_register_internal_class_with_flags(&ce, NULL,
        ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE);

    INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClientConnection",
                        wsclient_connection_methods);
    wsclient_connection_ce = zend_register_internal_class_with_flags(&ce, NULL,
        ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE);

    INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClientMessage", wsclient_message_methods);
    wsclient_message_ce = zend_register_internal_class_with_flags(&ce, NULL,
        ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE);
}
