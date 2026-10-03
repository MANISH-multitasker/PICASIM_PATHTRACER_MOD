#!/bin/bash
# Create a PicaSim AppImage
# Usage: ./linux_create_appimage.sh <build_dir> [output_dir]
#
# Example:
#   cmake --build --preset linux-arm64-release
#   ./linux_create_appimage.sh build/linux-arm64 dist
#
# Requirements:
#   - appimagetool (downloaded automatically if not found)
#   - Built PicaSim binary in <build_dir>/Release/PicaSim
#   - data/ directory in project root

set -e

BUILD_DIR="${1:?Usage: $0 <build_dir> [output_dir]}"
OUTPUT_DIR="${2:-.}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARCH="$(uname -m)"

# Map arch names for AppImage
case "$ARCH" in
    x86_64)  APPIMAGE_ARCH="x86_64" ;;
    aarch64) APPIMAGE_ARCH="aarch64" ;;
    *)       echo "Unsupported architecture: $ARCH"; exit 1 ;;
esac

# Read version
VERSION="1.0.0"
if [ -f "$SCRIPT_DIR/VERSIONS.txt" ]; then
    VERSION=$(head -1 "$SCRIPT_DIR/VERSIONS.txt" | grep -oP '[0-9]+\.[0-9]+\.[0-9]+' || echo "1.0.0")
fi
echo "PicaSim version: $VERSION"
echo "Architecture: $ARCH ($APPIMAGE_ARCH)"

# Verify binary exists
BINARY="$BUILD_DIR/Release/PicaSim"
if [ ! -f "$BINARY" ]; then
    echo "Error: Binary not found at $BINARY"
    echo "Build with: cmake --build --preset linux-${APPIMAGE_ARCH/x86_64/x64}-release"
    exit 1
fi

# Find or download appimagetool
APPIMAGETOOL=""
if command -v appimagetool &>/dev/null; then
    APPIMAGETOOL="appimagetool"
elif [ -f "$BUILD_DIR/appimagetool" ]; then
    APPIMAGETOOL="$BUILD_DIR/appimagetool"
else
    echo "Downloading appimagetool for $APPIMAGE_ARCH..."
    curl -L -o "$BUILD_DIR/appimagetool" \
        "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${APPIMAGE_ARCH}.AppImage"
    chmod +x "$BUILD_DIR/appimagetool"
    APPIMAGETOOL="$BUILD_DIR/appimagetool"
fi

# Clean and create AppDir
APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/picasim"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# Copy binary
cp "$BINARY" "$APPDIR/usr/bin/PicaSim"
chmod +x "$APPDIR/usr/bin/PicaSim"

# Copy data directory (excluding user data and large unnecessary files)
echo "Copying data directory..."
rsync -a --exclude='UserData/' --exclude='UserSettings/' \
    "$SCRIPT_DIR/data/" "$APPDIR/usr/share/picasim/data/"

# Copy icon
if [ -f "$SCRIPT_DIR/resources/IconFull.png" ]; then
    cp "$SCRIPT_DIR/resources/IconFull.png" "$APPDIR/PicaSim.png"
    cp "$SCRIPT_DIR/resources/IconFull.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/PicaSim.png"
fi

# Create .desktop file
cat > "$APPDIR/PicaSim.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=PicaSim
Comment=R/C Flight Simulator
Exec=AppRun
Icon=PicaSim
Categories=Game;Simulation;
Terminal=false
EOF

# Create AppRun script
cat > "$APPDIR/AppRun" << 'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
cd "$HERE/usr/share/picasim/data"
exec "$HERE/usr/bin/PicaSim" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# Copy VERSION info
cp "$SCRIPT_DIR/VERSIONS.txt" "$APPDIR/usr/share/picasim/data/VERSIONS.txt" 2>/dev/null || true
cp "$SCRIPT_DIR/LICENSE.txt" "$APPDIR/usr/share/picasim/" 2>/dev/null || true

# Create AppImage
mkdir -p "$OUTPUT_DIR"
APPIMAGE_NAME="PicaSim-${VERSION}-${APPIMAGE_ARCH}.AppImage"
echo "Creating AppImage: $APPIMAGE_NAME"

export ARCH="$APPIMAGE_ARCH"
# Use --appimage-extract-and-run to work without FUSE (e.g. in Docker/CI)
"$APPIMAGETOOL" --appimage-extract-and-run "$APPDIR" "$OUTPUT_DIR/$APPIMAGE_NAME" || \
    "$APPIMAGETOOL" "$APPDIR" "$OUTPUT_DIR/$APPIMAGE_NAME"

echo ""
echo "AppImage created: $OUTPUT_DIR/$APPIMAGE_NAME"
ls -lh "$OUTPUT_DIR/$APPIMAGE_NAME"
