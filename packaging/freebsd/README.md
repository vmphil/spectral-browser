# Spectral Browser on FreeBSD 14.x

The desktop app is a Go program (module `github.com/philip/spectral-browser`, version **1.3.4**) with a GTK 3 UI and a WebKit2GTK 4.1 web view. The small C web-process extension (`ext/spectral-ext.c`) is built as `libspectral-ext.so`. The Android app is not part of this package.

`build-pkg.sh` has to run on FreeBSD. It builds the binary, stages it under `/usr/local`, and runs `pkg create`.

Tested on FreeBSD 14.5-RELEASE amd64.

## Dependencies

`pkg add` does not download dependencies. Install the runtime packages before adding the `.pkg`.

### Runtime

| Package | Port | Role |
|---|---|---|
| `gtk3` | `x11-toolkits/gtk30` | GTK 3 |
| `webkit2-gtk_41` | `www/webkit2-gtk` flavor **41** | WebKit2GTK 4.1 (`webkit2gtk-4.1`). Pulls in `libsoup3` (`devel/libsoup3`), `glib` (`devel/glib20`), and the JavaScript engine |

`pkg query` reports the WebKit origin as `www/webkit2-gtk`. The `_41` name is the flavor.

Do not install `webkit2-gtk_40` or `webkit2-gtk_60`. Flavor 40 is the old 4.0 API (libsoup 2). Flavor 60 is GTK 4. This browser needs flavor 41.

`bubblewrap` and `xdg-dbus-proxy` are not required. They are the Linux web-process sandbox tools. WebKit's bubblewrap sandbox is a no-op on FreeBSD.

The Ghost Panel's active-connection list uses `sockstat` (and `ps` for the process tree). Linux still reads `/proc`. If `sockstat` cannot be run, that list stays empty and the rest of the panel still works.

```sh
pkg install gtk3 webkit2-gtk_41
pkg add ./spectral-browser-1.3.4.pkg
```

### Build

| Package | Port | Role |
|---|---|---|
| `gtk3`, `webkit2-gtk_41` | see Runtime | Headers and libraries (`gtk+-3.0`, `webkit2gtk-4.1`) |
| `pkgconf` | `devel/pkgconf` | `pkg-config` |
| `go` | `lang/go` | Go 1.26 or newer |
| base `cc` | — | Clang, already in the base system |

```sh
pkg install gtk3 webkit2-gtk_41 pkgconf go
sh packaging/freebsd/build-pkg.sh
pkg add ./packaging/freebsd/out/spectral-browser-1.3.4.pkg
```

Linux package names for the same stack (Ubuntu 24.04): `libgtk-3-0t64` / `libgtk-3-dev`, `libwebkit2gtk-4.1-0` / `libwebkit2gtk-4.1-dev`, `libsoup-3.0-0`, `libglib2.0-0t64`, plus `bubblewrap` and `xdg-dbus-proxy` for the sandbox. Those two sandbox packages are Linux-only.

## Build and install

The script rewrites dependency versions from the local package database into the manifest it passes to `pkg create` (the `+MANIFEST` in this directory is the checked-in template: name `spectral-browser`, version `1.3.4`, origin `www/spectral-browser`). `abi` follows `pkg config abi` (`FreeBSD:14:amd64`). The package `arch` field is the form official packages use (`freebsd:14:x86:64` on amd64).

`pkg create -M +MANIFEST` alone does not pack files: on pkg 2.9 that flag stores only the metadata unless the manifest already lists each file with pkg's checksum. The script uses the from-scratch form, a metadata directory containing `+MANIFEST` plus `pkg-plist`:

```sh
pkg create -m "$META" -p packaging/freebsd/pkg-plist -r stage -o out
```

Installed paths:

- `/usr/local/bin/spectral-browser`
- `/usr/local/lib/spectral-browser/libspectral-ext.so`
- `/usr/local/share/applications/spectral-browser.desktop`
- `/usr/local/share/icons/hicolor/256x256/apps/spectral-browser.png`
- `/usr/local/share/icons/hicolor/512x512/apps/spectral-browser.png`
- `/usr/local/share/pixmaps/spectral-browser.png`
- `/usr/local/share/doc/spectral-browser/README.md`

Opening the window needs a running X11 or Wayland session. `--version` only checks that the binary and its libraries load.

Remove the package with `pkg delete spectral-browser`. Settings stay in `~/.config/spectral-browser/`, `~/.local/share/spectral-browser/`, and `~/.cache/spectral-browser/`.

## What changed for FreeBSD

Linux behavior is unchanged.

- **Active connections.** On Linux the Ghost Panel still reads `/proc/<pid>/stat`, `/proc/<pid>/fd`, and `/proc/<pid>/net/tcp{,6}`. FreeBSD does not mount that proc layout. The FreeBSD build uses `ps` (the same process list as `sysctl kern.proc`) and `sockstat -c -P tcp`, which prints the owning pid. `netstat` is not used for the list: on FreeBSD it does not show which process owns a TCP connection, and listing every connection on the machine would include other programs. If `sockstat` fails, the connection list stays empty and the rest of the panel still works.
- **Self-test RSS.** The Linux self-test reads `VmRSS` from `/proc/self/status`. The FreeBSD build uses `sysctl kern.proc.pid.<pid>` (`ki_rssize`).
- **Extension lookup.** The loader also looks in `/usr/local/lib/spectral-browser`, the FreeBSD prefix. `/usr/lib/spectral-browser` is still checked for the Linux `.deb`.
- **Sandbox.** `webkit_web_context_set_sandbox_enabled` stays on. Upstream WebKit implements that sandbox with bubblewrap only on Linux; the FreeBSD port leaves it as a no-op.

## Ports skeleton

`Makefile`, `pkg-descr`, and `pkg-plist` are an optional ports-tree entry. Copy this directory to `/usr/ports/www/spectral-browser` and build against a local checkout:

```sh
cd /usr/ports/www/spectral-browser
make SPECTRAL_SRC=/path/to/spectral-browser package
```

There is no distfile and no release tag. `build-pkg.sh` is the path used to produce `spectral-browser-1.3.4.pkg`.
