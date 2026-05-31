#!/usr/bin/env bash
# Build a clean DEB package without openssl artifacts
set -euo pipefail

cd "$(dirname "$0")"

echo "=== Building subcli (cmake) ==="
cmake -S . -B build 2>&1 | tail -3
cmake --build build -j 2>&1 | tail -3

echo "=== Running tests ==="
ctest --test-dir build --output-on-failure 2>&1 | tail -6

echo "=== Generating DEB with cpack ==="
rm -f subcli-*.deb
rm -rf _CPack_Packages
cpack --config build/CPackConfig.cmake -G DEB 2>&1 | tail -3

deb="$(ls subcli-*.deb | head -1)"
echo "Raw DEB: $deb"

echo "=== Cleaning openssl artifacts from DEB ==="
tmpdir="$(mktemp -d)"
dpkg-deb -R "$deb" "$tmpdir"
# Remove openssl artifacts
rm -rf "$tmpdir/usr/include/openssl" 2>/dev/null
rm -f "$tmpdir/usr/lib/libcrypto.a" "$tmpdir/usr/lib/libssl.a" 2>/dev/null
rm -f "$tmpdir/usr/lib/pkgconfig/openssl.pc" 2>/dev/null
rm -rf "$tmpdir/usr/share/openssl" 2>/dev/null
rm -f "$tmpdir/usr/share/doc/man1/openssl.pod" 2>/dev/null
rm -f "$tmpdir/usr/share/doc/openssl-c-indent.el" 2>/dev/null
rm -f "$tmpdir/usr/share/doc/man3/OPENSSL_*.pod" 2>/dev/null
rm -rf "$tmpdir/usr/share/doc/HOWTO" 2>/dev/null
# Repack
clean_deb="${deb%.deb}-clean.deb"
dpkg-deb -b "$tmpdir" "$clean_deb"
rm -rf "$tmpdir"

echo "=== Clean DEB: $clean_deb ==="
echo "Size: $(du -h "$clean_deb" | cut -f1)"
echo "Artifacts: $(dpkg --contents "$clean_deb" | grep -ci 'openssl\|libssl\|libcrypto' || echo '0')"
mv "$clean_deb" build/
echo "Saved to: build/$(basename "$clean_deb")"
