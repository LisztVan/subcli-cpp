#!/bin/sh
set -e
if [ "$1" = "0" ]; then
    rm -rf /var/lib/subcli
    rm -rf /var/cache/subcli
    rm -rf /var/log/subcli
    if command -v systemctl >/dev/null 2>&1; then
        systemctl daemon-reload >/dev/null 2>&1 || true
    fi
fi
exit 0
