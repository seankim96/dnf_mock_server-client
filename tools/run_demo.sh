#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(dirname -- "$SCRIPT_DIR")
STATE_DIR="$ROOT_DIR/.demo"
BUILD_DIR="$STATE_DIR/build"
DATABASE_PATH="$STATE_DIR/dnf_demo.db"
CERTIFICATE_PATH="$STATE_DIR/localhost-cert.pem"
PRIVATE_KEY_PATH="$STATE_DIR/localhost-key.pem"
GAME_LOG_PATH="$STATE_DIR/game-server.log"
AUTH_LOG_PATH="$STATE_DIR/auth-server.log"

AUTH_PORT=7443
GAME_PORT=7777
DEMO_LOGIN_ID="demo_player"
DEMO_PASSWORD="demo-password"
DEMO_CHARACTER_NAME="DemoFighter"

GAME_PID=""
AUTH_PID=""

cleanup()
{
    if [ -n "$AUTH_PID" ] && kill -0 "$AUTH_PID" 2>/dev/null; then
        kill "$AUTH_PID" 2>/dev/null || true
        wait "$AUTH_PID" 2>/dev/null || true
    fi

    if [ -n "$GAME_PID" ] && kill -0 "$GAME_PID" 2>/dev/null; then
        kill "$GAME_PID" 2>/dev/null || true
        wait "$GAME_PID" 2>/dev/null || true
    fi
}

wait_for_server()
{
    server_name=$1
    server_port=$2
    server_pid=$3
    attempt=0

    while [ "$attempt" -lt 50 ]; do
        if ! kill -0 "$server_pid" 2>/dev/null; then
            echo "$server_name stopped during startup." >&2
            return 1
        fi

        if lsof -nP -a -p "$server_pid" \
            -iTCP:"$server_port" -sTCP:LISTEN >/dev/null 2>&1; then
            return 0
        fi

        attempt=$((attempt + 1))
        sleep 0.1
    done

    echo "$server_name did not open port $server_port." >&2
    return 1
}

trap cleanup EXIT INT TERM

for required_command in cmake openssl lsof; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "Required command was not found: $required_command" >&2
        exit 1
    fi
done

mkdir -p "$STATE_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" -j4

if [ ! -f "$DATABASE_PATH" ]; then
    account_output=$(
        printf '%s\n' "$DEMO_PASSWORD" |
            "$BUILD_DIR/dnf_admin" create-account \
                "$DATABASE_PATH" "$DEMO_LOGIN_ID")
    account_id=${account_output##*accountId=}

    case "$account_id" in
        ''|*[!0-9]*)
            echo "Could not read the demo account ID." >&2
            exit 1
            ;;
    esac

    "$BUILD_DIR/dnf_admin" create-character \
        "$DATABASE_PATH" "$account_id" "$DEMO_CHARACTER_NAME"
fi

if [ ! -f "$CERTIFICATE_PATH" ] || [ ! -f "$PRIVATE_KEY_PATH" ]; then
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$PRIVATE_KEY_PATH" \
        -out "$CERTIFICATE_PATH" \
        -sha256 -days 3650 \
        -subj "/CN=localhost"
fi

if lsof -nP -iTCP:"$AUTH_PORT" -sTCP:LISTEN >/dev/null 2>&1 ||
   lsof -nP -iTCP:"$GAME_PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "Demo ports $AUTH_PORT or $GAME_PORT are already in use." >&2
    exit 1
fi

"$BUILD_DIR/dnf_mock_server" \
    "$GAME_PORT" "$DATABASE_PATH" "$ROOT_DIR/data" \
    >"$GAME_LOG_PATH" 2>&1 &
GAME_PID=$!

"$BUILD_DIR/dnf_auth_server" \
    "$AUTH_PORT" \
    "$DATABASE_PATH" \
    "$CERTIFICATE_PATH" \
    "$PRIVATE_KEY_PATH" \
    127.0.0.1 \
    "$GAME_PORT" >"$AUTH_LOG_PATH" 2>&1 &
AUTH_PID=$!

wait_for_server "Game server" "$GAME_PORT" "$GAME_PID"
wait_for_server "Authentication server" "$AUTH_PORT" "$AUTH_PID"

certificate_fingerprint=$(
    openssl x509 -in "$CERTIFICATE_PATH" \
        -noout -fingerprint -sha256 | cut -d= -f2)

export DNF_AUTH_HOST="localhost"
export DNF_AUTH_PORT="$AUTH_PORT"
export DNF_AUTH_LOGIN_ID="$DEMO_LOGIN_ID"
export DNF_AUTH_PASSWORD="$DEMO_PASSWORD"
export DNF_AUTH_CERT_FINGERPRINT="$certificate_fingerprint"

echo "DNF demo is ready."
echo "  Login: $DEMO_LOGIN_ID"
echo "  Character: $DEMO_CHARACTER_NAME"
echo "  Game log: $GAME_LOG_PATH"
echo "  Auth log: $AUTH_LOG_PATH"

"$SCRIPT_DIR/godot.sh" --path "$ROOT_DIR/client"
