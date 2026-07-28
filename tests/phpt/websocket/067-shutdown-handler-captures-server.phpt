--TEST--
Rooms: shutdown with a worker handler that captures the server no longer double-frees
--SKIPIF--
<?php
if (!function_exists('TrueAsync\\__test_force_topic_post_full')) {
    echo "skip requires --enable-tas-test-hooks (fault-injection hook absent)";
}
?>
--EXTENSIONS--
true_async_server
true_async
--FILE--
<?php
/* Regression for the worker-shell release double-free. A WebSocket handler that
 * CAPTURES the server object is transferred per worker under one transfer ctx, so
 * the captured $server and the transit shell are xlat-deduped to the SAME object.
 * On shutdown http_server_release_worker_shell released the closure and the transit
 * in separate calls, each with its own visited set, and freed that shared object
 * twice.
 *
 * The park is load-bearing: only a shell whose closure never LOADed still owns the
 * snapshot at release, so we force a send to PARK a coroutine, then stop() while it
 * is parked. Without the fix this aborts (double free / rc 134 under valgrind);
 * with it the process exits cleanly. */
require_once __DIR__ . '/../server/_free_port.inc';
require_once __DIR__ . '/_ws_client.inc';

use TrueAsync\HttpServer;
use TrueAsync\HttpServerConfig;
use TrueAsync\WebSocket;
use TrueAsync\HttpRequest;
use function Async\spawn;
use function Async\delay;

$port = tas_free_port();
$server = new HttpServer(
    (new HttpServerConfig())
        ->addListener('127.0.0.1', $port)
        ->setWorkers(2)
        ->setWsPingIntervalMs(0)
);
$server->enableRooms();

/* The handler captures $server — the essential ingredient. */
$server->addWebSocketHandler(function (WebSocket $ws, HttpRequest $rq) use ($server) {
    if ($rq->getPath() === '/ctl') {
        foreach ($ws as $m) {
            if ($m->data !== 'go') { continue; }
            \TrueAsync\__test_force_topic_post_full(1 << 28);   // never delivers -> parks
            try { $server->send('t', 'm', 5000); } catch (\Throwable $e) { /* SHUTDOWN */ }
        }
        return;
    }
    $ws->subscribe('t');
    foreach ($ws as $m) { /* receive only */ }
});
$server->addHttpHandler(function ($rq, $rs) { $rs->setStatusCode(404)->end(); });

spawn(function () use ($server, $port) {
    delay(3500);
    $sub = ws_open($port, '/');
    $ctl = ws_open($port, '/ctl');
    if ($sub === null || $ctl === null) { echo "handshake failed\n"; $server->stop(); return; }
    delay(600);
    ws_write($ctl, 'go');   // parks a blocking send on its worker
    delay(600);
    @fclose($sub); @fclose($ctl);
    $server->stop();        // release worker shells while one closure is parked
    echo "done\n";
});

$server->start();
?>
--EXPECTF--
%Adone
