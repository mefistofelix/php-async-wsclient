--TEST--
Rooms reliable-send: HttpServerConfig retry knobs — defaults, round-trip, validation
--EXTENSIONS--
true_async_server
--FILE--
<?php
use TrueAsync\HttpServerConfig;

/* The three reliable-send knobs the hub reads at call time: drainer cadence,
 * default deadline, and the per-worker outbound-queue cap. Documented defaults
 * 50 / 5000 / 4096; each rejects a non-positive value the way the other WS
 * knobs do. */
$c = new HttpServerConfig();

echo "default interval: ", $c->getWsPublishRetryIntervalMs(), "\n";
echo "default timeout:  ", $c->getWsPublishRetryTimeoutMs(), "\n";
echo "default queuemax: ", $c->getWsPublishRetryQueueMax(), "\n";

$c->setWsPublishRetryIntervalMs(25)
  ->setWsPublishRetryTimeoutMs(1000)
  ->setWsPublishRetryQueueMax(64);

echo "after interval: ", $c->getWsPublishRetryIntervalMs(), "\n";
echo "after timeout:  ", $c->getWsPublishRetryTimeoutMs(), "\n";
echo "after queuemax: ", $c->getWsPublishRetryQueueMax(), "\n";

foreach (['setWsPublishRetryIntervalMs', 'setWsPublishRetryTimeoutMs', 'setWsPublishRetryQueueMax'] as $m) {
    try { $c->$m(0);  echo "$m(0) NOT REJECTED\n"; }
    catch (\Throwable $e) { echo "$m(0) rejected: ", $e::class, "\n"; }

    try { $c->$m(-5); echo "$m(-5) NOT REJECTED\n"; }
    catch (\Throwable $e) { echo "$m(-5) rejected: ", $e::class, "\n"; }
}

echo "Done\n";
--EXPECT--
default interval: 50
default timeout:  5000
default queuemax: 4096
after interval: 25
after timeout:  1000
after queuemax: 64
setWsPublishRetryIntervalMs(0) rejected: TrueAsync\HttpServerInvalidArgumentException
setWsPublishRetryIntervalMs(-5) rejected: TrueAsync\HttpServerInvalidArgumentException
setWsPublishRetryTimeoutMs(0) rejected: TrueAsync\HttpServerInvalidArgumentException
setWsPublishRetryTimeoutMs(-5) rejected: TrueAsync\HttpServerInvalidArgumentException
setWsPublishRetryQueueMax(0) rejected: TrueAsync\HttpServerInvalidArgumentException
setWsPublishRetryQueueMax(-5) rejected: TrueAsync\HttpServerInvalidArgumentException
Done
