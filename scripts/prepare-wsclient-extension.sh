#!/usr/bin/env sh
set -eu

# The repository still retains the historical server tree while the client is
# being extracted. phpize always reads config.m4/config.w32 from its working
# directory, so materialize the standalone descriptors immediately before the
# client-only build. This script is used by the release workflow.
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cp "$root/wsclient-config.m4" "$root/config.m4"
cp "$root/wsclient-config.w32" "$root/config.w32"
