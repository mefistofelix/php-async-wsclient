# TrueAsync WebSocket Client — Agent Guide

## Product boundary

This repository produces the standalone native PHP extension
`true_async_wsclient`, with artifact `php_true_async_wsclient.so` or
`php_true_async_wsclient.dll`.

- Public classes are `TrueAsync\\WebSocketClient`,
  `TrueAsync\\WebSocketClientConnection`, and
  `TrueAsync\\WebSocketClientMessage`.
- It is a C WebSocket **client** only. It has no Symfony, HTTP-server,
  TrueAsync Server, ThreadSync, listener, router, worker, HTTP/2 or HTTP/3
  runtime dependency.
- `true_async_server` remains a separate extension and is never renamed,
  replaced, or included as a dependency.
- The public URL schemes are only `ws://` and `wss://`. Internal PHP stream
  provider names are implementation details.

## Runtime and dependencies

Use the current matching development pair:

- `true-async/php-src` branch `true-async`
- `true-async/php-async` branch `main`

Use PHP's TrueAsync-aware stream transport APIs for DNS/connect/TLS/read/write
so the calling coroutine suspends on the existing runtime. Do not add a second
event loop or a synchronous socket implementation. `wss://` uses the PHP TLS
stream layer rather than a private OpenSSL transport.

The extension may bundle a private copy of small C dependencies, exactly as
the server project bundles Wslay. It must not require the server extension at
link or runtime.

## Implementation requirements

- Use bundled Wslay in client mode. Outbound frames are masked; inbound masked
  frames are rejected by the protocol implementation.
- Parse URLs with `php_url_parse_ex2()`; reject credentials, fragments,
  unsupported schemes, and CR/LF injection.
- Generate a random 16-byte `Sec-WebSocket-Key`; validate the HTTP 101
  upgrade, `Connection`, `Upgrade`, and `Sec-WebSocket-Accept` response
  headers; retain bytes that follow the HTTP header terminator.
- `WebSocketClient::connect(string $url, array $headers = [], array
  $subprotocols = [])` accepts safe custom request headers. Reserved WebSocket
  upgrade headers stay owned by the extension.
- Connection methods expose complete messages, never raw frames. Clean remote
  close returns `null` after queued messages are drained; protocol and
  transport errors raise `WebSocketClientException`.
- Keep parser sizes bounded and treat all wire input as hostile.

## Build and release integration

- `wsclient-config.m4` and `wsclient-config.w32` are the standalone build
  descriptors and must list every source and bundled dependency.
- `scripts/prepare-wsclient-extension.sh` materializes those descriptors
  immediately before `phpize` while legacy server-template files still exist
  in this repository.
- The release repository is `mefistofelix/releases`. Release artifacts retain
  every original binary and `php_true_async_server`; add the separate
  `php_true_async_wsclient` artifact alongside them.
- GitHub Actions must build and package against the matching development pair.
  Do not run smoke, unit, or end-to-end tests in GitHub Actions; run them only
  on a locally downloaded package.

## Completion

Do not call the work complete until the client is self-contained, has no
server runtime/class/build coupling, is integrated as an additional release
artifact, the GitHub build is green, and local smoke and real WebSocket
connect/send/receive tests pass against the downloaded package.
