--TEST--
WebSocketClient: coroutine-aware ws connect, custom headers, send and recv
--EXTENSIONS--
true_async_server
true_async
--FILE--
<?php
use TrueAsync\HttpServer;
use TrueAsync\HttpServerConfig;
use TrueAsync\WebSocket;
use TrueAsync\WebSocketClient;
use function Async\spawn;
use function Async\await;
use function Async\delay;

require_once __DIR__ . '/../server/_free_port.inc';

$port = tas_free_port();
$server = new HttpServer(
    (new HttpServerConfig())->addListener('127.0.0.1', $port)
        ->setReadTimeout(5)->setWriteTimeout(5)
);
$server->addWebSocketHandler(function (WebSocket $socket) {
    $message = $socket->recv();
    $socket->send('echo:' . $message->data);
});
$server->addHttpHandler(function ($request, $response) {
    $response->setStatusCode(404)->end();
});

$client = spawn(function () use ($port, $server) {
    delay(20);
    $socket = WebSocketClient::connect(
        "ws://127.0.0.1:$port/socket?trace=1",
        ['X-Client-Test' => 'enabled']
    );
    $socket->send('hello');
    $reply = $socket->recv();
    $socket->close();
    $server->stop();
    return $reply->data;
});

$server->start();
echo await($client), "\nDone\n";
--EXPECT--
echo:hello
Done
