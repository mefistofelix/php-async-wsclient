/* This is a generated file, edit WebSocketClient.php instead. */

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_TrueAsync_WebSocketClient___construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_TrueAsync_WebSocketClient_connect, 0, 1, TrueAsync\\WebSocket, 0)
	ZEND_ARG_TYPE_INFO(0, url, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, headers, IS_ARRAY, 0, "[]")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, subprotocols, IS_ARRAY, 0, "[]")
	ZEND_ARG_OBJ_TYPE_MASK(0, config, TrueAsync\\WebSocketClientConfig, MAY_BE_NULL, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_TrueAsync_WebSocketClientConfig___construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_METHOD(TrueAsync_WebSocketClient, __construct);
ZEND_METHOD(TrueAsync_WebSocketClient, connect);
ZEND_METHOD(TrueAsync_WebSocketClientConfig, __construct);

static const zend_function_entry class_TrueAsync_WebSocketClient_methods[] = {
	ZEND_ME(TrueAsync_WebSocketClient, __construct, arginfo_class_TrueAsync_WebSocketClient___construct, ZEND_ACC_PRIVATE)
	ZEND_ME(TrueAsync_WebSocketClient, connect, arginfo_class_TrueAsync_WebSocketClient_connect, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_FE_END
};

static const zend_function_entry class_TrueAsync_WebSocketClientConfig_methods[] = {
	ZEND_ME(TrueAsync_WebSocketClientConfig, __construct, arginfo_class_TrueAsync_WebSocketClientConfig___construct, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_TrueAsync_WebSocketClient(void)
{
	zend_class_entry ce;
	INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClient", class_TrueAsync_WebSocketClient_methods);
	return zend_register_internal_class_with_flags(&ce, NULL,
		ZEND_ACC_FINAL|ZEND_ACC_NO_DYNAMIC_PROPERTIES|ZEND_ACC_NOT_SERIALIZABLE);
}

static zend_class_entry *register_class_TrueAsync_WebSocketClientConfig(void)
{
	zend_class_entry ce;
	INIT_NS_CLASS_ENTRY(ce, "TrueAsync", "WebSocketClientConfig", class_TrueAsync_WebSocketClientConfig_methods);
	return zend_register_internal_class_with_flags(&ce, NULL,
		ZEND_ACC_FINAL|ZEND_ACC_NO_DYNAMIC_PROPERTIES|ZEND_ACC_NOT_SERIALIZABLE);
}
