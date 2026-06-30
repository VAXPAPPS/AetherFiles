#!/bin/bash
set -e

# App information
APP_NAME="aetherfiles"
PKG_NAME="aetherfiles"
VERSION="0.1.0"
ARCH="amd64"
DEB_DIR="${PKG_NAME}_${VERSION}_${ARCH}"

echo "Compiling the application..."
if [ ! -d "build" ]; then
    meson setup build
fi
meson compile -C build

echo "Creating package directory structure..."
rm -rf "$DEB_DIR"
mkdir -p "$DEB_DIR/DEBIAN"
mkdir -p "$DEB_DIR/usr/bin"
mkdir -p "$DEB_DIR/usr/share/applications"
mkdir -p "$DEB_DIR/usr/share/icons/hicolor/scalable/apps"

echo "Copying binary..."
cp build/src/$APP_NAME "$DEB_DIR/usr/bin/"
chmod 755 "$DEB_DIR/usr/bin/$APP_NAME"

echo "Copying icon..."
cp data/AetherFiles.svg "$DEB_DIR/usr/share/icons/hicolor/scalable/apps/$APP_NAME.svg"
chmod 644 "$DEB_DIR/usr/share/icons/hicolor/scalable/apps/$APP_NAME.svg"

echo "Creating desktop entry..."
cat <<EOF > "$DEB_DIR/usr/share/applications/$APP_NAME.desktop"
[Desktop Entry]
Name=AetherFiles
Comment=A modern and fast file manager
Exec=$APP_NAME %U
Icon=$APP_NAME
Terminal=false
Type=Application
DBusActivatable=true
StartupNotify=true
Categories=System;FileTools;FileManager;Utility;GTK;Core;FileManager;
Keywords=folder;manager;explore;disk;file;system;browser;folder;manager;explore;disk;filesystem;
MimeType=inode/directory;application/x-gnome-saved-search;inode/directory;application/x-7z-compressed;application/x-7z-compressed-tar;application/x-bzip;application/x-bzip-compressed-tar;application/x-compress;application/x-compressed-tar;application/x-cpio;application/x-gzip;application/x-lha;application/x-lzip;application/x-lzip-compressed-tar;application/x-lzma;application/x-lzma-compressed-tar;application/x-tar;application/x-tarz;application/x-xar;application/x-xz;application/x-xz-compressed-tar;application/zip;application/gzip;application/bzip2;application/x-bzip2-compressed-tar;application/vnd.rar;application/zstd;application/x-zstd-compressed-tar
StartupNotify=true
Actions=new-window;
X-Purism-FormFactor=Workstation;Mobile;


[Desktop Action new-window]
Name=New Window
Exec=aetherfiles --new-window
EOF
chmod 644 "$DEB_DIR/usr/share/applications/$APP_NAME.desktop"

echo "Creating DEBIAN/control..."
cat <<EOF > "$DEB_DIR/DEBIAN/control"
Package: $PKG_NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: VAXPOS <support@vaxp.org>
Description: AetherFiles  Manager
 A modern, fast, and elegant file manager for AetherOS.
EOF

echo "Building Debian package..."
dpkg-deb --build "$DEB_DIR"

echo "Done! The package is ready: ${DEB_DIR}.deb"
