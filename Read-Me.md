# Spectral Browser — Read-Me

A small, privacy-minded web browser for Linux, written in Go with GTK 3 and WebKit2GTK 4.1.

## Version

**1.3.3** (2026-10-25)

## Changes

### 1.3.3 — 2026-10-25
- **Open Link in New Tab** opens the page in a new tab again instead of asking to save it as a file.

### 1.3.2 — 2026-10-13
- Lighter page cache and a cheaper Ghost Panel refresh. See CHANGELOG.md.

### 1.3.1 — 2026-10-12
- **Ghost Panel monitoring only runs while the panel is open.** F8 (or ☰ → *Ghost Panel*, or `--ghost`) starts it; F8 or the panel's × stops it and frees everything it collected. See *Ghost Panel* below.
- **All cookies are cleared when the browser closes**, including sites you allowed (Settings → *Clear cookies when browser closes*, on by default). You'll need to sign in again after a restart.
- **Google sign-in works** once cookies are allowed for Google. A bar on the sign-in page offers *Allow cookies for Google*. Google's domains (google.com, gmail.com, youtube.com, gstatic.com, ...) now count as one site for cookies.

### 1.3.0 — 2026-10-08
- **Downloads work.** Files the page can't show (zip, exe, iso, `application/octet-stream`, ...) used to be silently dropped, and attachments were saved to Downloads with no sign anything happened. Now every download goes through one path with a save dialog, progress, and a list of what you saved. See *Downloads* below.
- **Right-click menu:** *Save Image As…*, *Download Image*, *Save Link As…*, *Download Linked File*, *Open Link in New Tab*, *Open Image in New Tab*, and *Save Page As…* on the page background.
- **Save page (Ctrl+S):** as HTML only, or as a complete web archive (`.mhtml`).
- **Tabbed browsing.** See *Tabs* below.
- New window (Ctrl+N), private window shortcut (Ctrl+Shift+N), reload (Ctrl+R / F5), back/forward (Alt+Left/Right).
- Ghost Panel rows now show which tab made them, and a *Current tab only* box filters to the tab you're on. Downloads show as blue **Download** rows.
- Settings: *Ask where to save each download* (on by default) and *Download folder*.

### 1.2.0 — 2026-10-07
- **New engine:** moved from WebKit2GTK 4.0 (2.28.1, from 2020) to WebKit2GTK 4.1 (2.52.6, current Ubuntu 24.04 security updates).
- **Web process sandbox** turned on (bubblewrap), so a compromised page is boxed in and can't use your account's access.
- **HTTPS-only:** `http://` addresses are upgraded to `https://`. If a site doesn't work over HTTPS, a yellow bar offers *Load over HTTP for this session* for that host. Local addresses (localhost, 192.168.x.x, `.local`) are left alone.
- **Third-party cookies blocked** everywhere. Intelligent Tracking Prevention is on.
- **Tracker and ad blocking** using a bundled list of about 95,000 hosts (see *Blocklist* below).
- **Per-site cookie consent** with the 🛡/🍪 button next to the address bar (see *Privacy* below).
- **Permission prompts:** camera, microphone, location, notifications, device lists, pointer lock and embedded-cookie access are all denied unless you click *Allow once*.
- Leak fixes: DNS prefetching off, WebRTC off (it can expose your local IP), pages can't open pop-ups on their own or read your clipboard.
- **Clear browsing data** (menu): deletes cookies, cache, local storage, IndexedDB and service workers.
- **New private window** (menu): nothing it does is saved to disk.
- **Ghost Panel (F8)**, see below.
- **Favorites you can set:** ☆/★ adds or removes the current page. The favorites bar under the address bar opens a page on click; right-click to rename or remove. The three old built-in favorites carry over as starting entries.
- **Homepage:** Home button in the toolbar, and a *Settings…* dialog (☰ menu) with a Homepage field, *Use current page*, and a dark mode switch.
- Settings are now saved in `~/.config/spectral-browser/config.json`. A broken file is set aside as `config.json.corrupt-<time>` and defaults are used.
- Typing words with no dot in the address bar searches DuckDuckGo. Ctrl+L focuses the address bar.
- New command-line options: `--ghost` (open the panel at start), `--debug` (log traffic to the terminal), `--version`.

### 1.1.0 — 2026-10-07
- Rewrote the app as a real GTK window with a WebKit web view (1.0.0 only printed text to the terminal and never opened a window).
- URL bar: press Enter to go; `https://` is added if you leave it off.
- Back, forward, and reload buttons.
- Window title shows the current page title.
- Opens https://duckduckgo.com by default, or a URL passed on the command line.
- Dark mode on by default (GTK dark theme).
- ★ favorites menu: DuckDuckGo, Go Docs, Wikipedia (built in, set in `config/config.go`).
- Closing the window exits cleanly.
- Packaged as `spectral-browser_1.1.0_amd64.deb` with a menu launcher and icon.

### 1.0.0 — 2026-10-06
- First attempt (console-only stub). Kept in `.bak-console-stub/` for reference.

## Tabs

- **+** button or **Ctrl+T** opens a new tab with your homepage. **Ctrl+W** or the tab's **×** (or middle-click on the tab) closes it. Closing the last tab closes the window.
- **Ctrl+Tab** / **Ctrl+Shift+Tab** and **Ctrl+PgDn** / **Ctrl+PgUp** go to the next / previous tab. **Ctrl+1** to **Ctrl+8** jump to that tab, **Ctrl+9** to the last one.
- Drag tabs to reorder them. Hover a tab to see its full title and its number (the number used in the Ghost Panel).
- Middle-click or Ctrl+click a link, or right-click → *Open Link in New Tab*, to open it in a background tab. Links that open a new window (`target="_blank"`) and pop-ups open as a new tab next to the current one. Pages still can't open pop-ups on their own; it has to come from a click.
- The address bar, back/forward/reload, the 🛡/🍪 cookie button, ☆, the HTTPS bar and the window title all follow the tab you're on.
- Tabs in a window share its settings and cookie rules. Tabs in a private window share that private session.
- Favorites: middle-click a favorite to open it in a background tab, or right-click → *Open in new tab*.

## Downloads

- By default a **save dialog** asks where to put each file, starting in your Downloads folder (`~/Downloads`). Turn off *Ask where to save each download* in Settings (☰ → *Settings…*) to save straight into the download folder.
- **Nothing is overwritten** without asking. Saving straight to the folder names a second copy `photo (1).jpg`, `photo (2).jpg`, and so on. The dialog asks before replacing a file.
- Server-suggested names are cleaned up: no folders, hidden-file dots or control characters.
- A **downloads bar** at the bottom of the window shows progress, then **Open** and **Show in folder** when done (or the error if it failed).
- **All downloads** (bar button, the ⬇ toolbar button, ☰ → *Downloads*, or **Ctrl+Shift+Y**) lists everything from this session with Open, Show in folder and Cancel. *Clear finished* tidies the list (the files stay on disk).
- Pages: **Ctrl+S** (or right-click the page → *Save Page As…*) saves the page. Pick *Web page, HTML only* for the HTML file, or *Web archive, complete (.mhtml)* for one file with images and styles.
- Downloads work in private windows too. The file is saved to disk like any other download; only the browsing session is private.
- If you close the last window while something is still downloading, the Downloads window stays open until it finishes. Closing that window cancels what's left.
- A link that opens a new tab just to download a file closes that empty tab again.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| Ctrl+T / Ctrl+W | New tab / close tab |
| Ctrl+Tab, Ctrl+PgDn | Next tab |
| Ctrl+Shift+Tab, Ctrl+PgUp | Previous tab |
| Ctrl+1 … Ctrl+8, Ctrl+9 | Go to tab 1–8, last tab |
| Ctrl+N / Ctrl+Shift+N | New window / new private window |
| Ctrl+L | Focus the address bar |
| Ctrl+R, F5 | Reload |
| Alt+Left / Alt+Right | Back / forward |
| Ctrl+S | Save page |
| Ctrl+Shift+Y | Downloads |
| F8 | Ghost Panel |

## Ghost Panel (F8)

Press **F8** while the browser window is focused, or pick *Ghost Panel* from the ☰ menu. It docks on the right like a dev-tools panel. Drag the divider to resize it, and the width is remembered.

**Monitoring is off until you open the panel.** While every panel is closed, nothing is watched or kept: no connection scanning, no DNS lookups for host names, no request reports from pages, no rows in memory. Opening a panel starts all of it from an empty list. Closing it (F8 again, or the × at the top of the panel) stops it and frees every row, counter and cached host name. If several windows have the panel open, monitoring stops when the last one closes. The Outgoing/Blocked counts only exist while the panel is open; tracker blocking and cookie rules work the same either way.

**Nothing from the Ghost Panel is written to disk.** It lives in memory only. With `--debug`, traffic lines are printed to the terminal (stderr) and go wherever you send that output; a normal launch prints nothing.

Each list keeps the newest 300 rows, shows a count in its header, and has a **Clear** button. Rows are added in batches every half second so the panel doesn't slow pages down. Outgoing and Incoming rows start with the **Tab** number (`#3`; hover a tab to see its number). Tick **Current tab only** at the top to see just the tab you're on.

1. **Active connections:** open TCP connections held by the browser's own processes, mainly WebKitNetworkProcess. The list is read from `/proc` every 1.5 s and shows host, remote IP:port, protocol (by port: HTTPS/HTTP), and process. Host names come from matching IPs to hosts the browser recently requested. A name starting with `~` came from reverse DNS instead, and `(unknown)` means neither worked.
2. **Outgoing:** every request a page makes: time, flag, method, host, and type.
   - **Blocked** (red): a third-party request to a known tracker, ad or data-collection host. It was cancelled and never left your computer.
   - **Third-party** (orange): went to a different site than the page you're on and was allowed.
   - **Allowed:** first-party.
   - **Download** (blue): a file you saved with the right-click menu, Save As or Ctrl+S. (A link that turns into a download shows as its normal page request.)
3. **Incoming:** responses: time, status code (errors in red), content type, host, size. Size shows `?` when the server didn't send a length.

## Privacy: what it does and doesn't stop

**Cookie consent.** Every site starts with **🛡 Cookies off**. Click it to switch the current site (for example all of `wikipedia.org`) to **🍪 Cookies on**. The list is saved in the config file (`cookie_sites`).
- *Cookies off:* the browser strips cookies from every network request and ignores cookies servers try to set. A page's own scripts can still set cookies while you're on it, but those are never sent back over the network. They're also deleted the next time Spectral starts and when you close the last window, so they last one session at most. Local storage, IndexedDB and service workers of sites you haven't allowed are deleted then too.
- *Clear cookies when browser closes* (Settings, on by default): when the last window closes, **every** cookie is deleted, including sites you allowed. Your list of allowed sites is kept, so those sites still get cookies during the next session. This runs again at start-up in case the browser crashed or was killed. **Logins don't survive a restart** while this is on; turn it off to stay signed in.
- **Google:** google.com, gmail.com, youtube.com, gstatic.com, googleusercontent.com and googleapis.com share one cookie choice. Google sign-in fails with "Something went wrong" while cookies are off, so the sign-in page shows a bar with *Allow cookies for Google*.
- *Cookies on:* first-party cookies are kept normally, so you can stay logged in.
- Third-party cookies are blocked either way.

**What this does stop:**
- Requests to about 95,000 known tracker and ad hosts, when they're embedded in another site.
- Cross-site cookie tracking, and long-lived cookies from sites you haven't accepted.
- Silent camera, mic or location access.
- `http://` snooping (HTTPS-only mode).

**What it does *not* stop. Be aware:**
- **Fingerprinting.** Sites can still identify a browser from screen size, fonts, WebGL/canvas output and similar signals. WebKitGTK has no anti-fingerprinting setting. WebGL is left on so maps and 3D sites work.
- **First-party analytics.** A site can track you with its own scripts, or with trackers served from its own domain (for example `metrics.example.com` on example.com). These aren't blocked because they look first-party.
- **Unlisted trackers.** Only whole-host rules are used, so trackers on a shared host or with a new name get through.
- **Local storage / IndexedDB** on unaccepted sites isn't purged automatically. Use *Clear browsing data*.
- **Ping auditing:** WebKit 2.52 ignores the old switch that turned off `<a ping>`. Pings to listed trackers are still blocked.
- Private windows don't hide your IP address or DNS lookups from your network.

## Blocklist

`blocklist/trackers.txt` holds the whole-host rules (`||host^`) taken from **EasyPrivacy** and **EasyList** (https://easylist.to). Both lists are © The EasyList authors and dual-licensed under GPLv3 and CC BY-SA 3.0 (https://easylist.to/pages/licence.html). The list was generated on 2026-10-07. To refresh it, run `make blocklist` and rebuild. On first run it's copied to `~/.local/share/spectral-browser/blocklist.txt`.

Blocking happens in a small WebKit web-process extension (`libspectral-ext.so`). It sees every request a page makes, so blocked ones can be shown in the Ghost Panel. Per-site cookie consent uses WebKit's content-filter engine.

## Dependencies

Installed automatically by `apt` with the .deb:

| Package | Minimum version | Purpose |
|---|---|---|
| libc6 | 2.34 | C runtime |
| libglib2.0-0t64 | 2.66.0 | GLib |
| libgtk-3-0t64 | 3.21.5 | GTK 3 window toolkit |
| libsoup-3.0-0 | 2.26.1 | HTTP library used by WebKit |
| libwebkit2gtk-4.1-0 | 2.39.90 (2.52.6 on Pop 24.04) | WebKit web engine |
| libjavascriptcoregtk-4.1-0 | any | WebKit's JavaScript engine (already pulled in by WebKit) |
| bubblewrap | any | Web process sandbox |
| xdg-dbus-proxy | any | Web process sandbox |

Exact names and minimum versions are filled in by `dpkg-shlibdeps` at build time. These names match Ubuntu 24.04 and Pop!_OS 24.04. Older releases that only have WebKit2GTK 4.0 won't work with 1.2.0 or later.

To build from source you also need: Go 1.22+, gcc, `pkg-config`, `libgtk-3-dev`, and `libwebkit2gtk-4.1-dev`.

## Install

```bash
sudo apt install ./dist/spectral-browser_1.3.3_amd64.deb
```

This upgrades 1.3.2 (or older) in place. If you have `~/.local/share/applications/spectral-browser.desktop`, delete it so the menu icon opens the installed copy.

## Remove

```bash
sudo apt remove spectral-browser
```

Your settings and site data stay in `~/.config/spectral-browser/`, `~/.local/share/spectral-browser/` and `~/.cache/spectral-browser/`. Delete those folders to remove them too.

## Build

```bash
make build      # bin/spectral-browser + bin/ext/libspectral-ext.so
make run        # build and launch with --debug
make test vet   # unit tests and go vet
make blocklist  # refresh the tracker list from easylist.to
make deb        # dist/spectral-browser_1.3.3_amd64.deb
```

## Troubleshooting

- Pages blank: `WEBKIT_DISABLE_COMPOSITING_MODE=1 spectral-browser`
- A site broken by blocking: open the Ghost Panel and look for red **Blocked** rows. To let it keep cookies, click 🛡.
- See the traffic in a terminal: `spectral-browser --debug`

## Files

- `/usr/bin/spectral-browser`: the program
- `/usr/lib/spectral-browser/libspectral-ext.so`: blocking/logging web extension
- `/usr/share/applications/spectral-browser.desktop`: menu launcher
- `/usr/share/icons/hicolor/*/apps/spectral-browser.png`: icon
- `/usr/share/doc/spectral-browser/README.md`: this file
- `~/.config/spectral-browser/config.json`: settings, favorites, accepted cookie sites, `ask_where_to_save`, `download_dir` (empty = your XDG Downloads folder) and `clear_cookies_on_exit`
- `~/.local/share/spectral-browser/`: cookies (`cookies.sqlite`) and site data (folder mode 0700)

## ARM64 build (PineTab2, Arrow Silver ARM64)
The arm64 package is built natively inside a Debian forky arm64 chroot run through qemu, so it links against the same libraries the Arrow Silver ARM64 images have.

One-time host setup on Pop: `sudo apt install debootstrap qemu-user-static rsync`

Build (the version comes from the Makefile; override with `VERSION=`):

    sudo ~/spectral-browser/packaging/build-arm64.sh

The first run creates the build chroot at `/var/tmp/spectral-arm64-forky` (about 10 minutes under qemu); later runs reuse it. Output: `dist/spectral-browser_<VER>_arm64.deb`. `packaging/build-deb.sh` now takes `ARCH` from the environment, or from `dpkg --print-architecture`, so `make deb` on Pop still makes the amd64 package.

After a new version, copy the .deb into both image projects, replacing the old one (each build expects exactly one):
- `~/Documents/Arrow-Silver/live-pinetab/packages/`
- `.../Written Apps/Arrow-Silver-Daily-ARM64/staging/debs-arm64/`

Inside a qemu chroot the WebKit sandbox (bubblewrap) cannot start, so a test launch there needs `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1`. On real hardware it runs normally.
