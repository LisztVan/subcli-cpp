#!/bin/sh
set -e
install -d -m 755 /var/lib/subcli/assets
install -d -m 755 /var/lib/subcli/outputs
install -d -m 755 /var/lib/subcli/state
install -d -m 755 /var/cache/subcli
install -d -m 755 /var/log/subcli
if [ ! -f /var/lib/subcli/sub.yaml ]; then
    printf 'version: 1\nsubscriptions: []\n' > /var/lib/subcli/sub.yaml
fi
if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload >/dev/null 2>&1 || true
fi
exit 0
