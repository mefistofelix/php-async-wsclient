<?php

/**
 * @generate-class-entries
 * @strict-properties
 * @not-serializable
 */

namespace TrueAsync;

/**
 * Coroutine-aware WebSocket client.
 *
 * Connection setup, TLS negotiation, HTTP Upgrade, reads and writes suspend
 * only the current TrueAsync coroutine. The returned connection is iterable:
 * each iteration produces one complete text or binary message.
 */
final class WebSocketClient
{
    private function __construct() {}

    /**
     * Connect to a ws:// or wss:// endpoint and complete the RFC 6455 client
     * handshake.
     *
     * @param array<string, string|string[]> $headers
     * @param string[] $subprotocols
     */
    public static function connect(
        string $url,
        array $headers = [],
        array $subprotocols = [],
        ?WebSocketClientConfig $config = null,
    ): WebSocket {}
}

/**
 * Client-side transport and protocol options.
 */
final class WebSocketClientConfig
{
    public function __construct() {}
}
