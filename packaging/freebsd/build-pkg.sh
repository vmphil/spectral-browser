#!/bin/sh
# Build spectral-browser and pack it with pkg create.
# Run this on FreeBSD 14.x or 15.x. It is not a cross build: the
# package ABI comes from `pkg config abi` on this host (FreeBSD:15:amd64
# or FreeBSD:14:amd64). A 15 package will not install on 14, and vice versa.
#
#   pkg install gtk3 webkit2-gtk_41 pkgconf go
#   sh packaging/freebsd/build-pkg.sh
#
# Output: packaging/freebsd/out/spectral-browser-<version>.pkg
set -eu

cd "$(dirname "$0")/../.."
ROOT=$(pwd)

if [ "$(uname -s)" != "FreeBSD" ]; then
	echo "build-pkg.sh must run on FreeBSD (this host is $(uname -s))." >&2
	echo "See packaging/freebsd/README.md." >&2
	exit 1
fi

VER=$(awk -F'"' '/^const Version = / { print $2; exit }' main.go)
if [ -z "$VER" ]; then
	echo "could not read Version from main.go" >&2
	exit 1
fi
# "VERSION := 1.3.5" — the value is the last field on GNU and BSD make.
MKVER=$(awk '/^VERSION[[:space:]]*:?=/{ print $NF; exit }' Makefile | tr -d '[:space:]')
if [ -n "$MKVER" ] && [ "$MKVER" != "$VER" ]; then
	echo "version mismatch: main.go $VER, Makefile $MKVER" >&2
	exit 1
fi

need() {
	command -v "$1" >/dev/null 2>&1 || {
		echo "missing command: $1" >&2
		exit 1
	}
}
need go
need pkg
need pkg-config
need cc
need strip

go version | awk '
{
	for (i = 1; i <= NF; i++) {
		if ($i ~ /^go[0-9]/) {
			split(substr($i, 3), a, ".")
			if ((a[1] + 0) < 1 || ((a[1] + 0) == 1 && (a[2] + 0) < 26)) {
				print "Go 1.26 or newer is required, found " $i > "/dev/stderr"
				exit 1
			}
			found = 1
		}
	}
	if (!found) {
		print "could not parse: " $0 > "/dev/stderr"
		exit 1
	}
}'

if ! pkg-config --exists gtk+-3.0 webkit2gtk-4.1 webkit2gtk-web-extension-4.1; then
	echo "GTK 3 / WebKit2GTK 4.1 headers not found." >&2
	echo "Install them with: pkg install gtk3 webkit2-gtk_41 pkgconf go" >&2
	exit 1
fi

PREFIX=/usr/local
STAGE="$ROOT/packaging/freebsd/stage"
OUT="$ROOT/packaging/freebsd/out"
PKG=spectral-browser
rm -rf "$STAGE" "$OUT"
mkdir -p \
	"$STAGE$PREFIX/bin" \
	"$STAGE$PREFIX/lib/$PKG" \
	"$STAGE$PREFIX/share/applications" \
	"$STAGE$PREFIX/share/icons/hicolor/256x256/apps" \
	"$STAGE$PREFIX/share/icons/hicolor/512x512/apps" \
	"$STAGE$PREFIX/share/pixmaps" \
	"$STAGE$PREFIX/share/doc/$PKG" \
	"$OUT"

export CGO_ENABLED=1
export GOPROXY="${GOPROXY:-https://proxy.golang.org,direct}"
echo "building $PKG $VER"
go build -trimpath -ldflags "-s -w" -o "$STAGE$PREFIX/bin/$PKG" .
${CC:-cc} -shared -fPIC -O2 -Wall \
	-o "$STAGE$PREFIX/lib/$PKG/libspectral-ext.so" \
	ext/spectral-ext.c \
	$(pkg-config --cflags --libs webkit2gtk-web-extension-4.1)
chmod 755 "$STAGE$PREFIX/bin/$PKG"
strip --strip-unneeded "$STAGE$PREFIX/lib/$PKG/libspectral-ext.so" \
	|| strip -x "$STAGE$PREFIX/lib/$PKG/libspectral-ext.so"

cp packaging/freebsd/spectral-browser.desktop "$STAGE$PREFIX/share/applications/$PKG.desktop"
cp packaging/freebsd/icons/256x256/$PKG.png "$STAGE$PREFIX/share/icons/hicolor/256x256/apps/$PKG.png"
cp packaging/freebsd/icons/512x512/$PKG.png "$STAGE$PREFIX/share/icons/hicolor/512x512/apps/$PKG.png"
cp packaging/freebsd/icons/256x256/$PKG.png "$STAGE$PREFIX/share/pixmaps/$PKG.png"
cp Read-Me.md "$STAGE$PREFIX/share/doc/$PKG/README.md"
chmod 644 \
	"$STAGE$PREFIX/share/applications/$PKG.desktop" \
	"$STAGE$PREFIX/share/icons/hicolor/256x256/apps/$PKG.png" \
	"$STAGE$PREFIX/share/icons/hicolor/512x512/apps/$PKG.png" \
	"$STAGE$PREFIX/share/pixmaps/$PKG.png" \
	"$STAGE$PREFIX/share/doc/$PKG/README.md"

# Direct runtime dependencies. gtk3 and webkit2-gtk_41 pull in GLib,
# JavaScriptCore, and the rest of the WebKit stack. libsoup3 is linked
# directly. Versions and origins come from the local pkg database.
# (pkg which -q prints "name-version", which is not a package name, so
# shared-library owners are not copied in as extra deps.)
SEEN=" "
DEPS=""
add_dep() {
	name=$1
	[ -n "$name" ] || return 0
	case " $SEEN " in
	*" $name "*) return 0 ;;
	esac
	if ! ver=$(pkg query '%v' "$name" 2>/dev/null); then
		echo "required package is not installed: $name" >&2
		echo "Install build dependencies with: pkg install gtk3 webkit2-gtk_41 pkgconf go" >&2
		exit 1
	fi
	origin=$(pkg query '%o' "$name")
	SEEN="$SEEN$name "
	if [ -n "$DEPS" ]; then
		DEPS="$DEPS,"
	fi
	DEPS="$DEPS
    \"$name\": {\"origin\": \"$origin\", \"version\": \"$ver\"}"
}

add_dep gtk3
add_dep webkit2-gtk_41
if pkg query '%n' libsoup3 >/dev/null 2>&1; then
	add_dep libsoup3
fi

# abi is e.g. "FreeBSD:15:amd64" or "FreeBSD:14:amd64". The package arch
# field uses a different CPU token: amd64 is "x86:64" (see any official pkg).
ABI=$(pkg config abi)
case "$ABI" in
FreeBSD:*:amd64) arch_cpu="x86:64" ;;
FreeBSD:*:i386) arch_cpu="x86:32" ;;
*) arch_cpu=${ABI##*:} ;;
esac
rel=${ABI#*:}
rel=${rel%%:*}
ARCH="freebsd:${rel}:${arch_cpu}"

META=$(mktemp -d)
trap 'rm -rf "$META"' EXIT
cat >"$META/+MANIFEST" <<EOF
{
  "name": "$PKG",
  "version": "$VER",
  "origin": "www/spectral-browser",
  "comment": "Privacy-minded GTK/WebKit web browser",
  "desc": "Spectral Browser is a small tabbed web browser built on GTK 3 and WebKit2GTK 4.1.\\nIt is HTTPS-only, blocks third-party cookies and known trackers, keeps cookies only for sites you accept, and has a Ghost Panel (F8) showing live connections, outgoing requests and incoming responses.\\nOn FreeBSD the connection list is read with sockstat. The WebKit bubblewrap sandbox is Linux-only and is a no-op in the FreeBSD WebKit port.\\n",
  "maintainer": "ganymedepublishingillinois@gmail.com",
  "www": "https://github.com/vmphil/spectral-browser",
  "abi": "$ABI",
  "arch": "$ARCH",
  "prefix": "/usr/local",
  "categories": ["www", "net"],
  "licenselogic": "single",
  "licenses": ["GPLv3"],
  "deps": {$DEPS
  },
  "scripts": {
    "post-install": "#!/bin/sh\\nif command -v gtk-update-icon-cache >/dev/null 2>&1; then\\n  gtk-update-icon-cache -q -f -t /usr/local/share/icons/hicolor >/dev/null 2>&1 || true\\nfi\\nif command -v update-desktop-database >/dev/null 2>&1; then\\n  update-desktop-database -q /usr/local/share/applications >/dev/null 2>&1 || true\\nfi\\nexit 0\\n",
    "post-deinstall": "#!/bin/sh\\nif command -v gtk-update-icon-cache >/dev/null 2>&1; then\\n  gtk-update-icon-cache -q -f -t /usr/local/share/icons/hicolor >/dev/null 2>&1 || true\\nfi\\nif command -v update-desktop-database >/dev/null 2>&1; then\\n  update-desktop-database -q /usr/local/share/applications >/dev/null 2>&1 || true\\nfi\\nexit 0\\n"
  }
}
EOF
cp "$META/+MANIFEST" "$OUT/+MANIFEST"

# pkg create -M manifest packs only files already listed in that manifest,
# and those checksums are pkg's own format. A from-scratch package uses a
# metadata directory (-m, containing +MANIFEST) plus a plist (-p). Paths in
# the plist are relative to prefix (/usr/local); -r is the staged root.
pkg create -m "$META" -p "$ROOT/packaging/freebsd/pkg-plist" -r "$STAGE" -o "$OUT"
FINAL="$OUT/${PKG}-${VER}.pkg"
if [ ! -f "$FINAL" ]; then
	created=$(find "$OUT" -maxdepth 1 -name "${PKG}-*.pkg" | head -n 1)
	if [ -z "$created" ]; then
		echo "pkg create did not write a .pkg into $OUT" >&2
		ls -la "$OUT" >&2
		exit 1
	fi
	mv "$created" "$FINAL"
fi
echo "wrote $FINAL"
ls -l "$FINAL"
