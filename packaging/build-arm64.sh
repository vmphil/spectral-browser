#!/usr/bin/env bash
# build-arm64.sh - build spectral-browser_<VER>_arm64.deb natively inside a
# Debian forky arm64 chroot run through qemu-aarch64 (binfmt).
#
#   sudo ./packaging/build-arm64.sh            # version from the Makefile
#   sudo VERSION=1.3.3 ./packaging/build-arm64.sh
#
# Needs on the host (once): sudo apt install debootstrap qemu-user-static
# and a registered qemu-aarch64 binfmt (/proc/sys/fs/binfmt_misc/qemu-aarch64).
#
# The build chroot lives at $BUILDROOT (default /var/tmp/spectral-arm64-forky).
# It is created on the first run (~10 min under qemu) and reused after that.
# It is a throwaway build environment: it never touches the Arrow Silver image
# chroots (live-arm64/chroot). Output: dist/spectral-browser_<VER>_arm64.deb,
# owned by the user who ran sudo.
#
# 2026-10-18: first used for 1.3.2 (built on Grok Bot's box the same way).
set -euo pipefail

SRC=$(cd "$(dirname "$0")/.." && pwd)
VER=${VERSION:-$(sed -n 's/^VERSION := //p' "$SRC/Makefile")}
BUILDROOT=${BUILDROOT:-/var/tmp/spectral-arm64-forky}
MIRROR=${MIRROR:-http://deb.debian.org/debian}
ICON_SRC=${ICON_SRC:-/home/${SUDO_USER:-philip}/Pictures/SpecB.png}
OWNER=${SUDO_USER:-root}

die() { echo "ERROR: $*" >&2; exit 1; }
[[ $EUID -eq 0 ]] || die "run with sudo: sudo $0"
[[ -n $VER ]] || die "could not read VERSION from $SRC/Makefile"
[[ -f $ICON_SRC ]] || die "icon not found: $ICON_SRC (set ICON_SRC=...)"
for t in debootstrap rsync chroot; do command -v $t >/dev/null || die "$t not installed"; done
BINFMT=/proc/sys/fs/binfmt_misc/qemu-aarch64
grep -qx enabled "$BINFMT" 2>/dev/null || die "qemu-aarch64 binfmt not enabled (sudo apt install qemu-user-static)"

MOUNTED=()
cleanup() {
	local i
	for ((i=${#MOUNTED[@]}-1; i>=0; i--)); do umount -l "${MOUNTED[i]}" 2>/dev/null || true; done
}
trap cleanup EXIT

if [[ ! -x $BUILDROOT/usr/bin/apt-get ]]; then
	echo "==> Creating forky arm64 build chroot at $BUILDROOT (slow under qemu)"
	rm -rf "$BUILDROOT"
	debootstrap --arch=arm64 --variant=minbase --include=ca-certificates forky "$BUILDROOT" "$MIRROR"
fi
for m in proc sys dev dev/pts; do
	mount --bind "/$m" "$BUILDROOT/$m"; MOUNTED+=("$BUILDROOT/$m")
done
cp -L /etc/resolv.conf "$BUILDROOT/etc/resolv.conf"
chroot "$BUILDROOT" /usr/bin/uname -m | grep -qx aarch64 || die "chroot does not run arm64 code"

echo "==> Installing build dependencies in the chroot"
chroot "$BUILDROOT" env DEBIAN_FRONTEND=noninteractive bash -c '
	apt-get update -qq &&
	apt-get install -y -qq --no-install-recommends golang-go build-essential pkg-config \
		libgtk-3-dev libwebkit2gtk-4.1-dev imagemagick dpkg-dev file'

echo "==> Copying source ($SRC) into the chroot"
rm -rf "$BUILDROOT/build"; mkdir -p "$BUILDROOT/build"
rsync -a --exclude=/bin --exclude=/dist --exclude=/old-c --exclude='/.bak-*' --exclude='*.deb' \
	"$SRC/" "$BUILDROOT/build/spectral-browser/"
install -m 644 "$ICON_SRC" "$BUILDROOT/build/icon.png"

echo "==> Building spectral-browser $VER for arm64"
chroot "$BUILDROOT" env HOME=/root GOFLAGS=-buildvcs=false CGO_ENABLED=1 GOARCH=arm64 \
	ARCH=arm64 VERSION="$VER" ICON_SRC=/build/icon.png \
	bash -c 'cd /build/spectral-browser && ./packaging/build-deb.sh'

DEB=spectral-browser_${VER}_arm64.deb
OUT=$BUILDROOT/build/spectral-browser/dist/$DEB
[[ -f $OUT ]] || die "no $DEB produced"
STAGE=$BUILDROOT/build/spectral-browser/dist/stage/spectral-browser_${VER}_arm64
chroot "$BUILDROOT" file /build/spectral-browser/dist/stage/spectral-browser_${VER}_arm64/usr/bin/spectral-browser \
	| grep -q 'ARM aarch64' || die "binary is not aarch64"
if chroot "$BUILDROOT" ldd /build/spectral-browser/dist/stage/spectral-browser_${VER}_arm64/usr/bin/spectral-browser | grep -q 'not found'; then
	die "binary has missing libraries (ldd)"
fi
mkdir -p "$SRC/dist"
install -m 644 -o "$OWNER" -g "$(id -gn "$OWNER")" "$OUT" "$SRC/dist/$DEB"
echo
dpkg-deb -I "$SRC/dist/$DEB" | sed -n 's/^ \(Version\|Architecture\|Depends\):/  \1:/p'
echo "  File:   $SRC/dist/$DEB"
echo "  SHA256: $(sha256sum "$SRC/dist/$DEB" | cut -d' ' -f1)"
echo "Copy it into the image projects too (see Read-Me.md, 'ARM64 build')."
