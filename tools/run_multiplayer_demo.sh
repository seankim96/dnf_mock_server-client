#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(dirname -- "$SCRIPT_DIR")
STATE_DIR="$ROOT_DIR/.demo/multiplayer"
BUILD_DIR="$ROOT_DIR/.demo/build"
DATABASE_PATH="$STATE_DIR/dnf_multiplayer_demo.db"
DATABASE_MARKER="$STATE_DIR/accounts-ready"
CERTIFICATE_PATH="$STATE_DIR/localhost-cert.pem"
PRIVATE_KEY_PATH="$STATE_DIR/localhost-key.pem"
GAME_LOG_PATH="$STATE_DIR/game-server.log"
AUTH_LOG_PATH="$STATE_DIR/auth-server.log"
CLIENT_PROJECT="$ROOT_DIR/client/DNFMockClient.csproj"

AUTH_PORT=7443
GAME_PORT=7777
ACCOUNT_PREFIX="demo_party_"
DEMO_PASSWORD="demo-password"

GAME_PID=""
AUTH_PID=""
CLIENT_PID_1=""
CLIENT_PID_2=""
CLIENT_PID_3=""
CLIENT_PID_4=""
STARTED_CLIENT_PID=""

if [ "$#" -ne 0 ]; then
    echo "Usage: $0" >&2
    exit 1
fi

cleanup()
{
    for client_pid in \
        "$CLIENT_PID_1" "$CLIENT_PID_2" \
        "$CLIENT_PID_3" "$CLIENT_PID_4"; do
        if [ -n "$client_pid" ] && kill -0 "$client_pid" 2>/dev/null; then
            kill "$client_pid" 2>/dev/null || true
            wait "$client_pid" 2>/dev/null || true
        fi
    done

    if [ -n "$AUTH_PID" ] && kill -0 "$AUTH_PID" 2>/dev/null; then
        kill "$AUTH_PID" 2>/dev/null || true
        wait "$AUTH_PID" 2>/dev/null || true
    fi

    if [ -n "$GAME_PID" ] && kill -0 "$GAME_PID" 2>/dev/null; then
        kill "$GAME_PID" 2>/dev/null || true
        wait "$GAME_PID" 2>/dev/null || true
    fi
}

handle_signal()
{
    exit 130
}

start_demo_client()
{
    client_number=$1
    window_x=$2
    window_y=$3
    client_log="$STATE_DIR/client-${client_number}.log"
    client_stdout="$STATE_DIR/client-${client_number}.stdout.log"

    env DNF_AUTH_HOST="" \
        DNF_AUTH_PORT="" \
        DNF_AUTH_LOGIN_ID="" \
        DNF_AUTH_PASSWORD="" \
        "$SCRIPT_DIR/godot.sh" \
        --path "$ROOT_DIR/client" \
        --resolution 700x430 \
        --position "$window_x,$window_y" \
        --log-file "$client_log" >"$client_stdout" 2>&1 &
    STARTED_CLIENT_PID=$!
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

create_demo_account()
{
    account_number=$1
    login_id="${ACCOUNT_PREFIX}${account_number}"
    character_name="PartyFighter${account_number}"
    account_output=$(
        printf '%s\n' "$DEMO_PASSWORD" |
            "$BUILD_DIR/dnf_admin" create-account \
                "$DATABASE_PATH" "$login_id")
    account_id=${account_output##*accountId=}

    case "$account_id" in
        ''|*[!0-9]*)
            echo "Could not read account ID for $login_id." >&2
            exit 1
            ;;
    esac

    "$BUILD_DIR/dnf_admin" create-character \
        "$DATABASE_PATH" "$account_id" "$character_name"
}

trap cleanup EXIT
trap handle_signal INT TERM

for required_command in cmake dotnet openssl lsof; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "Required command was not found: $required_command" >&2
        exit 1
    fi
done

mkdir -p "$STATE_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" -j4
dotnet build "$CLIENT_PROJECT" -warnaserror

if [ -f "$DATABASE_MARKER" ] && [ ! -f "$DATABASE_PATH" ]; then
    echo "Multiplayer demo database is missing: $DATABASE_PATH" >&2
    echo "Remove only .demo/multiplayer and run again." >&2
    exit 1
fi

if [ ! -f "$DATABASE_MARKER" ]; then
    if [ -e "$DATABASE_PATH" ]; then
        echo "Multiplayer demo database is incomplete: $DATABASE_PATH" >&2
        echo "Remove only .demo/multiplayer and run again." >&2
        exit 1
    fi

    for account_number in 1 2 3 4; do
        create_demo_account "$account_number"
    done

    touch "$DATABASE_MARKER"
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
    "$GAME_PORT" "$DATABASE_PATH" >"$GAME_LOG_PATH" 2>&1 &
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

export DNF_AUTH_CERT_FINGERPRINT="$certificate_fingerprint"

echo "Four-account multiplayer demo is ready."
echo "  ${ACCOUNT_PREFIX}1 / $DEMO_PASSWORD"
echo "  ${ACCOUNT_PREFIX}2 / $DEMO_PASSWORD"
echo "  ${ACCOUNT_PREFIX}3 / $DEMO_PASSWORD"
echo "  ${ACCOUNT_PREFIX}4 / $DEMO_PASSWORD"
echo "  Game log: $GAME_LOG_PATH"
echo "  Auth log: $AUTH_LOG_PATH"

start_demo_client 1 20 40
CLIENT_PID_1=$STARTED_CLIENT_PID
start_demo_client 2 740 40
CLIENT_PID_2=$STARTED_CLIENT_PID
start_demo_client 3 20 490
CLIENT_PID_3=$STARTED_CLIENT_PID
start_demo_client 4 740 490
CLIENT_PID_4=$STARTED_CLIENT_PID

echo "Four Godot game windows were launched in a 2x2 layout."
echo "Close all four windows or press Ctrl-C here to stop the demo."

wait "$CLIENT_PID_1" || true
wait "$CLIENT_PID_2" || true
wait "$CLIENT_PID_3" || true
wait "$CLIENT_PID_4" || true
