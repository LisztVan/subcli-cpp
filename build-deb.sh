#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"
PKG_NAME="subcli"
PKG_VERSION="0.2.7-1"
ARCH="amd64"
PKG_DIR="debian/${PKG_NAME}"
OUTPUT_FILE="${PKG_NAME}_${PKG_VERSION}_${ARCH}.deb"

# Ensure binary is built
if [ ! -f build/subcli ]; then
    echo "Building project first..."
    cmake -S . -B build
    cmake --build build -j
fi

# Clean package directory
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/DEBIAN"
mkdir -p "${PKG_DIR}/etc/subcli"
mkdir -p "${PKG_DIR}/lib/systemd/system"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/subcli/templates"
mkdir -p "${PKG_DIR}/usr/share/subcli/profiles"
mkdir -p "${PKG_DIR}/usr/share/doc/subcli"

# Copy binary
cp build/subcli "${PKG_DIR}/usr/bin/"

# Copy templates
cp templates/* "${PKG_DIR}/usr/share/subcli/templates/"

# Copy profiles
cp profiles/* "${PKG_DIR}/usr/share/subcli/profiles/"

# Copy systemd service
cp packaging/systemd/subcli-daemon.service "${PKG_DIR}/lib/systemd/system/subcli-daemon.service"

# Copy docs
cp README.subcli.md "${PKG_DIR}/usr/share/doc/subcli/"
gzip -9 -n -f "${PKG_DIR}/usr/share/doc/subcli/README.subcli.md"

# Copy copyright
cp debian/copyright "${PKG_DIR}/usr/share/doc/subcli/"

# Create default /etc config (marked as conffile)
cat > "${PKG_DIR}/etc/subcli/config.yaml" << 'YAML'
# subcli - system-wide default configuration
#
# This file is managed by apt/dpkg. To override for your user:
#   subcli config set <key> <value>
#
# User settings are stored in your workspace (~/.local/share/subcli/config.yaml
# or the workspace root from subcli init).
#
# See: subcli config list
#      subcli config get <key>
#      docs: /usr/share/doc/subcli/README.subcli.md

# Default export profile
profile: bypass-cn

# Default output directory (relative paths are resolved against workspace)
output_dir: outputs

# Network limits
parallelism: 3
timeout: 15
retry: 2
fetch_max_bytes: 5242880

# Logging
log_level: info

# TUN mode (requires root / appropriate capabilities)
tun: false
YAML

# Create DEBIAN/control
cat > "${PKG_DIR}/DEBIAN/control" << 'CONTROL'
Package: subcli
Version: 0.2.7-1
Section: net
Priority: optional
Architecture: amd64
Depends: libcurl4, libc6 (>= 2.17), libgcc-s1, libstdc++6 (>= 8)
Recommends: curl
Suggests: mihomo, sing-box, xray
Maintainer: subcli developers <subcli@lists.sr.ht>
Description: Subscription to proxy client config tool
 Generate Mihomo, sing-box, and Xray configuration files
 from subscription URLs, profiles, templates, and workspace
 settings. subcli does not replace proxy clients and does
 not enable the system proxy by itself.
Homepage: https://git.sr.ht/~earendil-works/subcli-cpp
CONTROL

# Create conffiles
echo "/etc/subcli/config.yaml" > "${PKG_DIR}/DEBIAN/conffiles"

# Create postinst
cat > "${PKG_DIR}/DEBIAN/postinst" << 'POSTINST'
#!/bin/sh
set -e

case "$1" in
    configure)
        if [ "$2" = "" ] && [ -d /run/systemd/system ]; then
            if command -v deb-systemd-helper >/dev/null 2>&1; then
                deb-systemd-helper enable subcli-daemon.service >/dev/null 2>&1 || true
            fi
        fi
        ;;
    abort-upgrade|abort-remove|abort-deconfigure)
        ;;
    *)
        ;;
esac
POSTINST

# Create prerm
cat > "${PKG_DIR}/DEBIAN/prerm" << 'PRERM'
#!/bin/sh
set -e

case "$1" in
    remove|upgrade|deconfigure)
        if [ -d /run/systemd/system ]; then
            if command -v deb-systemd-helper >/dev/null 2>&1; then
                deb-systemd-helper stop subcli-daemon.service >/dev/null 2>&1 || true
            fi
        fi
        ;;
    failed-upgrade)
        ;;
    *)
        ;;
esac
PRERM

# Create postrm
cat > "${PKG_DIR}/DEBIAN/postrm" << 'POSTRM'
#!/bin/sh
set -e

case "$1" in
    remove|purge)
        if [ -d /run/systemd/system ]; then
            if command -v deb-systemd-helper >/dev/null 2>&1; then
                deb-systemd-helper disable subcli-daemon.service >/dev/null 2>&1 || true
                deb-systemd-helper purge subcli-daemon.service >/dev/null 2>&1 || true
            fi
        fi
        ;;
    upgrade|failed-upgrade|abort-install|abort-upgrade|disappear)
        ;;
    *)
        ;;
esac
POSTRM

chmod 755 "${PKG_DIR}/DEBIAN/postinst"
chmod 755 "${PKG_DIR}/DEBIAN/prerm"
chmod 755 "${PKG_DIR}/DEBIAN/postrm"

# Generate md5sums
cd "${PKG_DIR}"
find . -type f ! -path './DEBIAN/*' -exec md5sum {} \; > DEBIAN/md5sums
cd - > /dev/null

# Build the package
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${OUTPUT_FILE}"

echo ""
echo "=== Package created: ${OUTPUT_FILE} ==="
echo ""
dpkg-deb --info "${OUTPUT_FILE}"
echo ""
dpkg-deb --contents "${OUTPUT_FILE}" | head -40
