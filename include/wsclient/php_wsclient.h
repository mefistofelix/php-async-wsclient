/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#ifndef TRUE_ASYNC_PHP_WSCLIENT_H
#define TRUE_ASYNC_PHP_WSCLIENT_H

#include "php.h"

extern zend_class_entry *wsclient_ce;
extern zend_class_entry *wsclient_connection_ce;
extern zend_class_entry *wsclient_message_ce;
extern zend_class_entry *wsclient_exception_ce;

void wsclient_register_classes(void);

#endif
