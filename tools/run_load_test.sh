#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(dirname -- "$SCRIPT_DIR")
STATE_DIR="$ROOT_DIR/.demo/loadtest"
BUILD_DIR="$STATE_DIR/build"
DATABASE_PATH="$STATE_DIR/dnf_loadtest.db"
PROVISION_COUNT_PATH="$STATE_DIR/provisioned-count"
PROVISION_PREFIX_PATH="$STATE_DIR/account-prefix"
PROVISION_INCOMPLETE_PATH="$STATE_DIR/provisioning-incomplete"
CERTIFICATE_PATH="$STATE_DIR/localhost-cert.pem"
PRIVATE_KEY_PATH="$STATE_DIR/localhost-key.pem"
GAME_LOG_PATH="$STATE_DIR/game-server.log"
AUTH_LOG_PATH="$STATE_DIR/auth-server.log"
CLIENT_PROJECT="$ROOT_DIR/client/loadtest/DNFMockClient.LoadTest.csproj"

ACCOUNT_COUNT=${DNF_LOADTEST_ACCOUNTS:-100}
CONCURRENCY=${DNF_LOADTEST_CONCURRENCY:-20}
DURATION_SECONDS=${DNF_LOADTEST_DURATION_SECONDS:-10}
SESSION_TIMEOUT_SECONDS=${DNF_LOADTEST_SESSION_TIMEOUT_SECONDS:-30}
PROFILE=${DNF_LOADTEST_PROFILE:-lobby}
ACCOUNT_PREFIX=${DNF_LOADTEST_ACCOUNT_PREFIX:-load_user_}
LOADTEST_PASSWORD=${DNF_LOADTEST_PASSWORD:-loadtest-password}
AUTH_HOST=${DNF_AUTH_HOST:-localhost}
AUTH_PORT=${DNF_AUTH_PORT:-7443}
GAME_PORT=${DNF_GAME_PORT:-7777}

GAME_PID=""
AUTH_PID=""

usage()
{
    echo "Usage: $0 [options]"
    echo ""
    echo "  --profile lobby|dungeon"
    echo "  --accounts 1..1000"
    echo "  --concurrency 1..1000"
    echo "  --duration 1..3600"
    echo "  --timeout 1..300"
    echo "  --account-prefix PREFIX"
    echo "  --help"
    echo ""
    echo "The script provisions an isolated .demo/loadtest database, starts both"
    echo "servers, derives the development certificate fingerprint, and runs the"
    echo "load client. Passwords and certificate fingerprints are never printed."
}

require_value()
{
    option_name=$1
    option_count=$2
    if [ "$option_count" -lt 2 ]; then
        echo "Missing value for $option_name." >&2
        exit 2
    fi
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            require_value "$1" "$#"
            PROFILE=$2
            shift 2
            ;;
        --accounts)
            require_value "$1" "$#"
            ACCOUNT_COUNT=$2
            shift 2
            ;;
        --concurrency)
            require_value "$1" "$#"
            CONCURRENCY=$2
            shift 2
            ;;
        --duration)
            require_value "$1" "$#"
            DURATION_SECONDS=$2
            shift 2
            ;;
        --timeout)
            require_value "$1" "$#"
            SESSION_TIMEOUT_SECONDS=$2
            shift 2
            ;;
        --account-prefix)
            require_value "$1" "$#"
            ACCOUNT_PREFIX=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

validate_integer()
{
    value=$1
    name=$2
    minimum=$3
    maximum=$4

    case "$value" in
        ''|*[!0-9]*)
            echo "$name must be an integer between $minimum and $maximum." >&2
            exit 2
            ;;
    esac

    if [ "$value" -lt "$minimum" ] || [ "$value" -gt "$maximum" ]; then
        echo "$name must be between $minimum and $maximum." >&2
        exit 2
    fi
}

validate_integer "$ACCOUNT_COUNT" "accounts" 1 1000
validate_integer "$CONCURRENCY" "concurrency" 1 1000
validate_integer "$DURATION_SECONDS" "duration" 1 3600
validate_integer "$SESSION_TIMEOUT_SECONDS" "timeout" 1 300
validate_integer "$AUTH_PORT" "authentication port" 1 65535
validate_integer "$GAME_PORT" "game port" 1 65535

if [ "$CONCURRENCY" -gt "$ACCOUNT_COUNT" ]; then
    echo "concurrency cannot be greater than accounts." >&2
    exit 2
fi

if [ "$AUTH_PORT" -eq "$GAME_PORT" ]; then
    echo "authentication and game ports must be different." >&2
    exit 2
fi

case "$PROFILE" in
    lobby)
        ;;
    dungeon)
        if [ $((ACCOUNT_COUNT % 4)) -ne 0 ] || [ "$CONCURRENCY" -lt 4 ]; then
            echo "dungeon profile requires accounts divisible by 4 and concurrency >= 4." >&2
            exit 2
        fi
        ;;
    *)
        echo "profile must be lobby or dungeon." >&2
        exit 2
        ;;
esac

case "$ACCOUNT_PREFIX" in
    ''|*[!A-Za-z0-9_]*)
        echo "account-prefix must use ASCII letters, digits, or underscores." >&2
        exit 2
        ;;
esac

if [ "${#ACCOUNT_PREFIX}" -gt 28 ]; then
    echo "account-prefix cannot exceed 28 characters." >&2
    exit 2
fi

if [ -z "$LOADTEST_PASSWORD" ]; then
    echo "DNF_LOADTEST_PASSWORD cannot be empty." >&2
    exit 2
fi

if [ "${#LOADTEST_PASSWORD}" -gt 1024 ]; then
    echo "DNF_LOADTEST_PASSWORD cannot exceed 1024 characters." >&2
    exit 2
fi

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

handle_signal()
{
    exit 130
}

wait_for_server()
{
    server_name=$1
    server_port=$2
    server_pid=$3
    attempt=0

    while [ "$attempt" -lt 100 ]; do
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

provision_account()
{
    account_number=$1
    padded_number=$(printf '%04d' "$account_number")
    login_id="${ACCOUNT_PREFIX}${padded_number}"
    character_name="LoadPlayer${padded_number}"

    account_output=$(
        printf '%s\n' "$LOADTEST_PASSWORD" |
            "$BUILD_DIR/dnf_admin" create-account \
                "$DATABASE_PATH" "$login_id")
    account_id=${account_output##*accountId=}

    case "$account_id" in
        ''|*[!0-9]*)
            echo "Could not read account ID while provisioning account $account_number." >&2
            exit 1
            ;;
    esac

    "$BUILD_DIR/dnf_admin" create-character \
        "$DATABASE_PATH" "$account_id" "$character_name" >/dev/null
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

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j4
dotnet build "$CLIENT_PROJECT" -warnaserror

if [ -f "$PROVISION_INCOMPLETE_PATH" ]; then
    echo "A previous account provisioning run was interrupted." >&2
    echo "Remove only $STATE_DIR and run again." >&2
    exit 1
fi

if [ -f "$DATABASE_PATH" ] && \
   { [ ! -f "$PROVISION_COUNT_PATH" ] || [ ! -f "$PROVISION_PREFIX_PATH" ]; }; then
    echo "The isolated load-test database has no provisioning metadata." >&2
    echo "Remove only $STATE_DIR and run again." >&2
    exit 1
fi

if [ ! -f "$DATABASE_PATH" ] && \
   { [ -f "$PROVISION_COUNT_PATH" ] || [ -f "$PROVISION_PREFIX_PATH" ]; }; then
    echo "Provisioning metadata exists but the load-test database is missing." >&2
    echo "Remove only $STATE_DIR and run again." >&2
    exit 1
fi

provisioned_count=0
if [ -f "$PROVISION_COUNT_PATH" ]; then
    provisioned_count=$(sed -n '1p' "$PROVISION_COUNT_PATH")
    validate_integer "$provisioned_count" "stored provisioned count" 0 1000
    stored_prefix=$(sed -n '1p' "$PROVISION_PREFIX_PATH")
    if [ "$stored_prefix" != "$ACCOUNT_PREFIX" ]; then
        echo "The requested account prefix does not match the isolated database." >&2
        echo "Remove only $STATE_DIR before changing the prefix." >&2
        exit 1
    fi
fi

if [ "$provisioned_count" -lt "$ACCOUNT_COUNT" ]; then
    : >"$PROVISION_INCOMPLETE_PATH"
    account_number=$((provisioned_count + 1))
    while [ "$account_number" -le "$ACCOUNT_COUNT" ]; do
        provision_account "$account_number"
        if [ $((account_number % 25)) -eq 0 ] || \
           [ "$account_number" -eq "$ACCOUNT_COUNT" ]; then
            echo "Provisioned $account_number/$ACCOUNT_COUNT load-test accounts."
        fi
        account_number=$((account_number + 1))
    done

    count_temp="$PROVISION_COUNT_PATH.tmp"
    prefix_temp="$PROVISION_PREFIX_PATH.tmp"
    printf '%s\n' "$ACCOUNT_COUNT" >"$count_temp"
    printf '%s\n' "$ACCOUNT_PREFIX" >"$prefix_temp"
    mv "$count_temp" "$PROVISION_COUNT_PATH"
    mv "$prefix_temp" "$PROVISION_PREFIX_PATH"
    rm "$PROVISION_INCOMPLETE_PATH"
fi

if [ ! -f "$CERTIFICATE_PATH" ] || [ ! -f "$PRIVATE_KEY_PATH" ]; then
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$PRIVATE_KEY_PATH" \
        -out "$CERTIFICATE_PATH" \
        -sha256 -days 3650 \
        -subj "/CN=localhost" >/dev/null 2>&1
fi

if lsof -nP -iTCP:"$AUTH_PORT" -sTCP:LISTEN >/dev/null 2>&1 ||
   lsof -nP -iTCP:"$GAME_PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "Load-test ports $AUTH_PORT or $GAME_PORT are already in use." >&2
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

export DNF_LOADTEST_PASSWORD="$LOADTEST_PASSWORD"
export DNF_AUTH_CERT_FINGERPRINT="$certificate_fingerprint"

echo "Load-test servers are ready."
echo "  profile: $PROFILE"
echo "  accounts: $ACCOUNT_COUNT"
echo "  concurrency: $CONCURRENCY"
echo "  hold: $DURATION_SECONDS seconds per session"
echo "  game log: $GAME_LOG_PATH"
echo "  auth log: $AUTH_LOG_PATH"

dotnet run --project "$CLIENT_PROJECT" --no-build -- \
    --profile "$PROFILE" \
    --accounts "$ACCOUNT_COUNT" \
    --concurrency "$CONCURRENCY" \
    --duration "$DURATION_SECONDS" \
    --timeout "$SESSION_TIMEOUT_SECONDS" \
    --auth-host "$AUTH_HOST" \
    --auth-port "$AUTH_PORT" \
    --account-prefix "$ACCOUNT_PREFIX"
