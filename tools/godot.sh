#!/bin/sh

set -eu

if [ -n "${GODOT_COMMAND:-}" ]; then
    exec "$GODOT_COMMAND" "$@"
fi

if command -v godot >/dev/null 2>&1; then
    exec godot "$@"
fi

if command -v godot4 >/dev/null 2>&1; then
    exec godot4 "$@"
fi

MACOS_GODOT="/Applications/Godot_mono.app/Contents/MacOS/Godot"
if [ -x "$MACOS_GODOT" ]; then
    exec "$MACOS_GODOT" "$@"
fi

echo "Godot .NET executable was not found." >&2
echo "Set GODOT_COMMAND to the Godot 4.6 .NET executable." >&2
exit 1
