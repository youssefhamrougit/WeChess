#!/usr/bin/env bash
# Packages the whole project into a source ZIP that can be extracted anywhere
# and built with the README instructions.
#
#   ./scripts/package_zip.sh            -> dist/wechess-<version>-source.zip
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n 's/^project(WeChess VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -n1)"
[ -n "$VERSION" ] || VERSION="1.0.0"
NAME="wechess-$VERSION"

mkdir -p dist
ARCHIVE="dist/$NAME-source.zip"
rm -f "$ARCHIVE"

find . -type f \
  -not -path "./.git/*" \
  -not -path "./.tools/*" \
  -not -path "./build/*" \
  -not -path "./dist/*" \
  -not -path "./wechess-data/*" \
  -not -path "./android/.gradle/*" \
  -not -path "./android/.idea/*" \
  -not -path "./android/app/build/*" \
  -not -path "./android/app/.cxx/*" \
  -not -name "local.properties" \
  -not -name "*.apk" \
  -not -name "*.aab" \
  -not -name "*.jks" \
  -not -name "*.keystore" \
  | sort | zip -q -@ "$ARCHIVE"

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"
  echo "sha256: $(cut -d' ' -f1 "$ARCHIVE.sha256")"
fi

echo "wrote $ARCHIVE"
echo "files:  $(unzip -l "$ARCHIVE" | tail -n1 | awk '{print $2}')"
echo "size:   $(du -h "$ARCHIVE" | cut -f1)"
