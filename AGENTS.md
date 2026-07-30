# TrueAsync WebSocket Client — Agent Guide

## Objective

Turn this TrueAsync Server-based repository into a sibling native PHP extension
that provides a high-level, coroutine-aware WebSocket **client**. The result
must be written in C and integrated with PHP TrueAsync. Do not implement a
userland-only wrapper and do not introduce a second event loop. Do not add or
depend on Symfony (including Symfony Convenience Server): this is a native C
module using the existing TrueAsync asynchronous runtime and its PHP-facing
async APIs. Do not introduce a dedicated synchronous transport path.

Target the current `main` development ABI: `true-async/php-async` `main` and
the matching `true-async/php-src` `true-async-main` branch. Use its
`zend_async_API.h` I/O primitives; do not target the older `true-async-api`
ABI surface.

The module must be source-complete before attempting a local build. Build and
runtime verification are a later phase unless explicitly requested.

## Product contract

The public API is message-oriented, comparable to modern Node.js WebSocket
clients, while retaining the naming and conventions of TrueAsync Server.

Suggested PHP surface:

```php
use TrueAsync\WebSocketClient;

$socket = WebSocketClient::connect('wss://example.test/socket');
$socket->send('hello');

foreach ($socket as $message) {
    // $message is a complete WebSocketMessage, never a raw frame.
    echo $message->data;
}
```

- `TrueAsync\WebSocketClient::connect()` is coroutine-aware. DNS, TCP connect,
  TLS (`wss://`), HTTP upgrade and response validation must suspend only the
  calling coroutine.
- The returned connection exposes `send()`, `sendBinary()`, `trySend()`,
  `trySendBinary()`, `recv()`, `ping()`, `close()`, `isClosed()`, and iteration.
- `recv()` and iteration yield fully reassembled `WebSocketMessage` objects;
  control frames and fragmented wire frames never leak to userland.
- A clean close ends `recv()`/iteration with `null`; abnormal close and protocol
  errors use the project WebSocket exception classes.
- Keep the single-reader invariant: multiple concurrent `recv()` calls on one
  connection are rejected. Sending may be performed by multiple coroutines with
  cooperative queueing and backpressure.

## Reuse from this repository

This project is intentionally based on `true-async/server`; preserve its
layout and coding conventions.

- `deps/wslay/` is the bundled RFC 6455 framing implementation. Use its **client
  context** for outgoing connections. The client must mask outbound frames;
  inbound server frames must not be masked.
- `src/websocket/ws_session.c` and `include/websocket/ws_session.h` are the
  reference for message FIFO ownership, fragmentation, ping/pong, close
  semantics, backpressure, iteration and coroutine wakeups. Extract or adapt
  shared logic rather than duplicating divergent frame handling.
- `src/websocket/ws_handshake.c` is server-side only. Client code must generate
  a random 16-byte `Sec-WebSocket-Key`, calculate and retain the expected
  `Sec-WebSocket-Accept`, build a HTTP/1.1 Upgrade request, require status 101,
  and validate Upgrade, Connection and Accept headers.
- `src/core/` and `Zend/zend_async_API.h` define the asynchronous patterns.
  Use existing TrueAsync primitives so socket I/O parks the current coroutine.
- Use PHP's existing `php_url_parse_ex2()` for URI decomposition rather than
  maintaining a second URL parser. Keep only the WebSocket-specific validation
  around its result (ws/wss scheme, no credentials/fragment, safe target).
- PHP stream TLS support provides `ssl://`/OpenSSL behavior. `wss://` must
  reuse it and respect certificate/hostname options; do not create a parallel
  OpenSSL transport unless an existing extension abstraction requires it. The
  public API accepts only `ws://` and `wss://`: internal PHP stream
  transport names are implementation details and must not leak into the API.

## Current client files

The first client foundation is already present:

- `include/websocket/ws_client.h`: handshake state and public C helpers.
- `src/websocket/ws_client.c`: HTTP Upgrade request rendering and response
  acceptance validation.
- `stubs/WebSocketClient.php`: intended PHP API shape.
- `tests/unit/websocket/test_ws_client_handshake.c`: initial handshake tests.

Complete these rather than replacing them with unrelated architecture. The
current code is a foundation, not a finished client.

## Required implementation work

1. Add a client connection object that owns the async stream/socket, wslay client
   context, receive buffer/FIFO, outbound frame queue, close state and waiter.
2. Parse `ws://` and `wss://` URLs safely, including host, port, target path and
   query. Reject unsupported schemes, malformed authority and unsafe embedded
   control bytes.
3. Establish the transport through TrueAsync-aware stream/socket APIs, then run
   the HTTP Upgrade state machine. Preserve bytes received after HTTP headers as
   the first WebSocket input bytes.
4. Drive `wslay_event_recv()` on async reads and enqueue complete messages.
   Use a bounded queue and close predictably on excessive peer input.
5. Drive `wslay_event_send()` for text, binary, PING, PONG and CLOSE. Client
   frames must be masked. Respect backpressure without blocking unrelated
   coroutines.
6. Implement generated class entries and registration for the client API. Keep
   names in the `TrueAsync` namespace and reuse existing `WebSocketMessage`,
   close-code and exception conventions where coexistence permits.
7. Do not make the client depend on server-only HTTP request routing, listener,
   topic, worker or HTTP/2/HTTP/3 server subsystems.

## Client session model

Model every connection as one owned, single-reactor `ws_client_session_t`.
It must not be shared between worker threads. Its lifecycle is:

```text
NEW -> CONNECTING -> TLS_HANDSHAKE (wss only) -> HTTP_UPGRADE
    -> OPEN -> CLOSING -> CLOSED
                 \-> FAILED
```

- `NEW`: URL/options have been validated; no transport is owned yet.
- `CONNECTING`: asynchronous DNS/TCP connect is in flight. Cancellation and
  timeout must release the pending connect resource.
- `TLS_HANDSHAKE`: only for `wss://`; all certificate and hostname failures are
  terminal and reported as connection failures.
- `HTTP_UPGRADE`: write the request, incrementally collect headers up to a
  strict cap, validate the server response, then preserve any trailing bytes.
- `OPEN`: read events feed Wslay; completed messages are appended to the FIFO;
  `recv()`/the iterator consumes the FIFO. A one-shot in-thread event wakes the
  suspended reader. Outbound producers enqueue frames and one cooperative
  flusher owns actual writes.
- `CLOSING`: after local or remote CLOSE, send/finish the close handshake once,
  wake a waiting reader and drain only already-complete queued messages.
- `CLOSED`: resources and Wslay context are released exactly once; clean close
  returns `null` from `recv()` after the FIFO is empty.
- `FAILED`: protocol, transport, TLS or write failure; wake waiters and expose
  the appropriate exception. Never leave a coroutine parked after failure.

### Ownership invariants

- The session owns its stream/socket, Wslay context, inbound staging buffer,
  pending outbound payloads, message FIFO and internal wait event.
- A `WebSocket` PHP object only borrows the session while it is live; object
  destruction must initiate the same idempotent close/dispose path.
- A single `recv_waiter` may be suspended. Reject a second reader instead of
  racing message delivery.
- `send()` producers may suspend for high-water backpressure; `trySend()` never
  queues when the transport is above its limit. Internal PONG/close writes must
  not recursively suspend from the read callback.
- Do not invoke user PHP callbacks from the transport read stack. Wake or queue
  the coroutine and let the scheduler resume it on a clean stack.

## Build-system requirements

Prepare all supported build paths; do not run the build unless asked.

- `config.m4`: include every client source and its bundled Wslay requirements
  in the PHP/TrueAsync extension build.
- `config.w32`: include the same client and Wslay source files, include paths
  and feature definitions for Windows builds.
- `CMakeLists.txt`: include all production client sources and dependencies.
- Add any generated stub headers or build generation inputs expected by the
  existing extension workflow.

The repository currently has client additions in all three build descriptors.
Keep them synchronized when new source files are created.

## Test plan

Prepare tests now; execute them only after a requested build.

- Unit-test request generation: path, host/port, required Upgrade headers,
  subprotocol list and correct CRLF termination.
- Unit-test handshake validation: accepted 101 response, wrong/missing Accept,
  missing Upgrade or Connection token, malformed/truncated headers and surplus
  bytes following the header terminator.
- Unit-test RFC 6455 framing through Wslay: client masking, text/binary,
  fragmentation, PING/PONG, normal close, protocol error and oversized input.
- PHP integration tests: `connect()`, `wss://`, `send()`/`recv()`, `foreach`,
  backpressure, cancellation/timeout and concurrent-reader rejection. Use a
  local deterministic echo peer rather than an internet service.

## C standards and safety

Follow `docs/CODING_STANDARDS.md` exactly:

- C11, portable across Linux, macOS and Windows.
- Descriptive lower_snake_case names; use `EXPECTED`/`UNEXPECTED` in hot paths.
- Use guard clauses, brace every control body and make ownership explicit.
- Do not introduce locks on a per-connection reactor path. Coroutine code is
  single-threaded per owning reactor; cross-thread interactions require the
  existing TrueAsync mechanisms.
- Keep network parsers capped and strict. Treat all URL, HTTP and WebSocket wire
  bytes as hostile input.
- New wire parser/validator code requires C unit coverage.

## Completion criteria

Do not call the module complete merely because stubs, headers or build entries
exist. Completion requires the C connection state machine, public PHP API,
message iterator, protocol/TLS behavior, synchronized build descriptors and
prepared unit/integration tests to all be present. A later build/test pass is
still required to claim runtime verification.
