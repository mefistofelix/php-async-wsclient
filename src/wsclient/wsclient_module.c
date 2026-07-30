/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "php.h"
#include "ext/standard/info.h"

#include "php_true_async_wsclient.h"
#include "wsclient/php_wsclient.h"

#define PHP_TRUE_ASYNC_WSCLIENT_VERSION "0.1.0-dev"

PHP_MINIT_FUNCTION(true_async_wsclient)
{
    wsclient_register_classes();
    return SUCCESS;
}

PHP_MINFO_FUNCTION(true_async_wsclient)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "true_async_wsclient support", "enabled");
    php_info_print_table_row(2, "Version", PHP_TRUE_ASYNC_WSCLIENT_VERSION);
    php_info_print_table_end();
}

zend_module_entry true_async_wsclient_module_entry = {
    STANDARD_MODULE_HEADER,
    "true_async_wsclient",
    NULL,
    PHP_MINIT(true_async_wsclient),
    NULL,
    NULL,
    NULL,
    PHP_MINFO(true_async_wsclient),
    PHP_TRUE_ASYNC_WSCLIENT_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_TRUE_ASYNC_WSCLIENT
ZEND_GET_MODULE(true_async_wsclient)
#endif
