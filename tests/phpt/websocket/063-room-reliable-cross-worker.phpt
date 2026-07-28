--TEST--
Rooms reliable-send cross-worker: publish()/send()/trySend() reach a subscriber on another worker; posted/delivered count the remote mailbox
--EXTENSIONS--
true_async_server
true_async
--FILE--
<?php
/* Two workers. Six clients subscribe to one topic and are spread across both
 * workers. A coroutine on the pool PARENT (attached to no worker of its own) drives
 * the three server-side calls; every one must cross a worker boundary to reach the
 * subscribers, and the breakdown/return value must show the remote mailbox was
 * used, not just a local tree:
 *   - publish() returns posted >= 1 (a remote mailbox accepted the copy) and
 *     served == 0 (the parent owns no subscribers), and every client receives it;
 *   - send() returns a delivered count >= 1 (the remote targets it reached),
 *     throws nothing, and every client receives it;
 *   - trySend() returns true and every client receives it;
 *   - getRuntimeStats().ws_topic_posted advances across the run.
 *
 * Normal cross-worker delivery does NOT park: the target mailboxes are nowhere
 * near full, so retry_queued/retry_delivered stay 0 here. Forcing the park→retry
 * path needs a wedged consumer backed up past TOPIC_HUB_MAILBOX_CAPACITY (4096)
 * messages, which a .phpt cannot provoke without a test-only knob — see report. */
require_once __DIR__ . '/../server/_free_port.inc';
require_once __DIR__ . '/_ws_client.inc';

use TrueAsync\HttpServer;
use TrueAsync\HttpServerConfig;
use TrueAsync\WebSocket;
use TrueAsync\HttpRequest;
use function Async\spawn;
use function Async\delay;

const CLIENTS = 6;

$port = tas_free_port();
$config = (new HttpServerConfig())
    ->addListener('127.0.0.1', $port)
    ->setWorkers(2)
    ->setReadTimeout(10)
    ->setWriteTimeout(10)
    ->setWsPingIntervalMs(0);

$server = new HttpServer($config);
$server->enableRooms();

$server->addWebSocketHandler(function (WebSocket $ws, HttpRequest $req) {
    $ws->subscribe('chat');
    foreach ($ws as $msg) {
        /* read-only subscribers */
    }
});

$server->addHttpHandler(function ($req, $res) { $res->setStatusCode(404)->end(); });

spawn(function () use ($port, $server) {
    delay(4000);   // both workers have to bind

    $conns = [];
    for ($i = 0; $i < CLIENTS; $i++) {
        $fp = ws_open($port);
        if ($fp === null) { echo "handshake failed\n"; $server->stop(); return; }
        $conns[] = $fp;
    }

    delay(800);    // let every subscribe land in its worker's tree

    $reached = function (string $expect) use ($conns) {
        $got = 0;
        foreach ($conns as $fp) {
            if (ws_read_pending($fp) === $expect) { $got++; }
        }
        return $got;
    };

    $before = $server->getRuntimeStats()['ws_topic_posted'];

    /* publish — best-effort, but it still crosses to the remote worker(s). */
    $p = $server->publish('chat', 'p');
    echo 'publish served (parent owns none): ', $p['served'] === 0 ? 'yes' : "no ({$p['served']})", "\n";
    echo 'publish posted a remote mailbox: ', $p['posted'] >= 1 ? 'yes' : "no ({$p['posted']})", "\n";
    delay(800);
    echo 'publish reached all: ', $reached('p') === CLIENTS ? 'yes' : 'no', "\n";

    /* trySend — reliable, non-blocking; nothing to park, delivered outright. */
    $t = $server->trySend('chat', 't');
    echo 'trySend returned: ', var_export($t, true), "\n";
    delay(800);
    echo 'trySend reached all: ', $reached('t') === CLIENTS ? 'yes' : 'no', "\n";

    /* send — reliable, blocking; delivered count includes the remote targets. */
    $threw = false;
    $r = null;
    try { $r = $server->send('chat', 's'); }
    catch (\Throwable $e) { $threw = true; }
    echo 'send threw: ', $threw ? 'yes' : 'no', "\n";
    echo 'send delivered a remote target: ', (is_int($r) && $r >= 1) ? 'yes' : "no (" . var_export($r, true) . ")", "\n";
    delay(800);
    echo 'send reached all: ', $reached('s') === CLIENTS ? 'yes' : 'no', "\n";

    $after = $server->getRuntimeStats()['ws_topic_posted'];
    echo 'ws_topic_posted advanced: ', ($after - $before) >= 1 ? 'yes' : 'no', "\n";

    foreach ($conns as $fp) { fclose($fp); }
    $server->stop();
});

$server->start();
?>
--EXPECTF--
publish served (parent owns none): yes
publish posted a remote mailbox: yes
publish reached all: yes
trySend returned: true
trySend reached all: yes
send threw: no
send delivered a remote target: yes
send reached all: yes
ws_topic_posted advanced: yes%A
