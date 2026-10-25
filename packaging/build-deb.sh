#!/usr/bin/env bash
# Build spectral-browser .deb without root.
set -euo pipefail
cd "$(dirname "$0")/.."
VER="${VERSION:-1.3.3}"; PKG=spectral-browser; ARCH="${ARCH:-$(dpkg --print-architecture)}"
ICON_SRC="${ICON_SRC:-/home/philip/Pictures/SpecB.png}"
STAGE="dist/stage/${PKG}_${VER}_${ARCH}"
rm -rf "$STAGE"; mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/bin" "$STAGE/usr/share/applications" \
  "$STAGE/usr/share/icons/hicolor/256x256/apps" "$STAGE/usr/share/icons/hicolor/512x512/apps" "$STAGE/usr/share/pixmaps" \
  "$STAGE/usr/lib/spectral-browser" "$STAGE/usr/share/doc/$PKG"
go build -trimpath -ldflags "-s -w" -o "$STAGE/usr/bin/$PKG" .
chmod 755 "$STAGE/usr/bin/$PKG"
make ext >/dev/null
install -m 644 bin/ext/libspectral-ext.so "$STAGE/usr/lib/spectral-browser/"
strip --strip-unneeded "$STAGE/usr/lib/spectral-browser/libspectral-ext.so"
install -m 644 Read-Me.md "$STAGE/usr/share/doc/$PKG/README.md"
convert "$ICON_SRC" -resize 256x256 "$STAGE/usr/share/icons/hicolor/256x256/apps/$PKG.png"
cp "$ICON_SRC" "$STAGE/usr/share/icons/hicolor/512x512/apps/$PKG.png"
cp "$STAGE/usr/share/icons/hicolor/256x256/apps/$PKG.png" "$STAGE/usr/share/pixmaps/$PKG.png"
chmod 644 "$STAGE"/usr/share/icons/hicolor/*/apps/*.png "$STAGE/usr/share/pixmaps/$PKG.png"
cat > "$STAGE/usr/share/applications/$PKG.desktop" <<D
[Desktop Entry]
Type=Application
Name=Spectral Browser
GenericName=Web Browser
Comment=Privacy-minded GTK/WebKit web browser
Exec=/usr/bin/spectral-browser %u
Icon=spectral-browser
Terminal=false
Categories=Network;WebBrowser;
MimeType=x-scheme-handler/http;x-scheme-handler/https;text/html;
StartupNotify=true
D
chmod 644 "$STAGE/usr/share/applications/$PKG.desktop"
# Depends via dpkg-shlibdeps (needs a debian/control stub)
TMP=$(mktemp -d); mkdir -p "$TMP/debian"; printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' $PKG $PKG > "$TMP/debian/control"
DEPS=$(cd "$TMP" && dpkg-shlibdeps -O "$OLDPWD/$STAGE/usr/bin/$PKG" "$OLDPWD/$STAGE/usr/lib/spectral-browser/libspectral-ext.so" 2>/dev/null | sed 's/^shlibs:Depends=//')
rm -rf "$TMP"
SIZE=$(du -sk "$STAGE/usr" | cut -f1)
cat > "$STAGE/DEBIAN/control" <<C
Package: $PKG
Version: $VER
Architecture: $ARCH
Maintainer: Philip Orona <philip@localhost>
Section: web
Priority: optional
Installed-Size: $SIZE
Depends: $DEPS, bubblewrap, xdg-dbus-proxy
Description: Lightweight GTK/WebKit web browser written in Go
 Spectral Browser is a small tabbed web browser built on GTK 3 and WebKit2GTK 4.1.
 It is HTTPS-only, blocks third-party cookies and known trackers, keeps
 cookies only for sites you accept, and has a Ghost Panel (F8) showing
 live connections, outgoing requests and incoming responses.
C
for s in postinst postrm; do cat > "$STAGE/DEBIAN/$s" <<S
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t /usr/share/icons/hicolor || true
exit 0
S
chmod 755 "$STAGE/DEBIAN/$s"; done
mkdir -p dist
dpkg-deb --root-owner-group --build "$STAGE" "dist/${PKG}_${VER}_${ARCH}.deb"
