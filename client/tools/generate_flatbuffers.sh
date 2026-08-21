#!/bin/sh

set -eu

FLATC_COMMAND="${FLATC_COMMAND:-flatc}"
EXPECTED_VERSION="flatc version 25.2.10"
ACTUAL_VERSION="$($FLATC_COMMAND --version)"

if [ "$ACTUAL_VERSION" != "$EXPECTED_VERSION" ]; then
    echo "Expected $EXPECTED_VERSION but found $ACTUAL_VERSION" >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CLIENT_DIR=$(dirname -- "$SCRIPT_DIR")
ROOT_DIR=$(dirname -- "$CLIENT_DIR")

"$FLATC_COMMAND" \
    --csharp \
    --gen-onefile \
    -o "$CLIENT_DIR/scripts/Generated" \
    "$ROOT_DIR/schemas/AuthMessage.fbs" \
    "$ROOT_DIR/schemas/DungeonMessage.fbs" \
    "$ROOT_DIR/schemas/TcpMessage.fbs"
