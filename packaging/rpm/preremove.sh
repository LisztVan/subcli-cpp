#!/bin/sh
set -e
if [ "$1" = "0" ]; then
    if command -v systemctl >/dev/null 2>&1; then
        systemctl stop subcli-daemon.service >/dev/null 2>&1 || true
        systemctl disable subcli-daemon.service >/dev/null 2>&1 || true
    fi
fi
exit 0
