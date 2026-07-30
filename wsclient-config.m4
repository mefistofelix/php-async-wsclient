dnl Standalone TrueAsync WebSocket client extension.
PHP_ARG_ENABLE([true-async-wsclient],
  [whether to enable the TrueAsync WebSocket client],
  [AS_HELP_STRING([--enable-true-async-wsclient],
    [Enable native TrueAsync WebSocket client support])],
  [yes])

if test "$PHP_TRUE_ASYNC_WSCLIENT" != "no"; then
  PHP_NEW_EXTENSION([true_async_wsclient], [
    src/wsclient/wsclient_module.c
    src/wsclient/wsclient_classes.c
    src/wsclient/wsclient_handshake.c
    src/wsclient/wsclient_session.c
    src/wsclient/wsclient_connection.c
    deps/wslay/lib/wslay_event.c
    deps/wslay/lib/wslay_queue.c
    deps/wslay/lib/wslay_stack.c
  ], [$ext_shared])
  PHP_ADD_INCLUDE([$ext_srcdir])
  PHP_ADD_INCLUDE([$ext_srcdir/include])
  PHP_ADD_INCLUDE([$ext_srcdir/deps/wslay/includes])
fi
