# Spectral Browser Changelog

All notable changes to Spectral Browser. Newest first.

## [1.3.3] - 2026-10-25

### Fixed
- **Open Link in New Tab** asked to save the link as a file. All right-click items Spectral adds shared one action name, so WebKit ran *Save Link As…* for each of them. Each item now has its own action.

## [1.3.2] - 2026-10-13

### Changed
- **Lighter page cache.** WebKit now uses the *document browser* cache model instead of *web browser*: smaller in-memory resource/decoded-image cache and disk cache, while still keeping a small back/forward page cache so Back/Forward stay quick.
- **Ghost Panel refresh is cheaper while open.** Rows are batched every 1 s (was 0.5 s) and the connection scan runs every 2 s (was 1.5 s). The Active connections list is updated in place (changed rows only, extras trimmed) instead of being cleared and rebuilt, and the section headers are only redrawn when something changed. With the panel closed there is still no timer, scanner or reporting at all, and opening it refreshes immediately.

## [1.3.1] - 2026-10-12

### Changed
- **Ghost Panel monitoring runs only while the panel is open.** Opening it (F8, ☰ → Ghost Panel, `--ghost`) starts the `/proc` connection scanner, the host-name lookups, the 0.5 s row timer and the web extension's per-request reports. Closing it (F8 or the new × button) stops them all and frees every row, counter and host/IP cache; the row lists are destroyed, not hidden. The extension keeps blocking trackers while the panel is closed, it just stops reporting.
- Lists keep 300 rows each (was 500), and the IP-to-host and lookup caches are capped at 2,000 entries.
- **Clear cookies when browser closes** (new Settings switch, on by default) deletes every cookie, including allowed sites, when the last window closes and again at start-up (in case of a crash). The list of allowed sites is kept. Local storage, IndexedDB, service workers and cache storage of sites you haven't allowed are deleted as well.

### Fixed
- **Google sign-in** showed "Something went wrong" on accounts.google.com: the per-site cookie rule stripped Google's cookies. Google's domains (google.com, gmail.com, youtube.com, gstatic.com, googleusercontent.com, googleapis.com, googlevideo.com, ytimg.com, ggpht.com, withgoogle.com) now share one cookie choice, and the sign-in page shows an *Allow cookies for Google* bar while cookies are off. Tracker blocking is unchanged.

### Notes
- No Ghost Panel data is written to disk. `--debug` prints traffic to stderr only.

## [1.3.0] - 2026-10-08

### Fixed
- **Downloads didn't save.** The browser had no download handling, so it used WebKit's defaults: a page response WebKit can't display (zip, exe, iso, `application/octet-stream`, ...) was silently ignored, and only `Content-Disposition: attachment` files and the stock right-click "Download Image" went to `~/Downloads`, with no dialog, progress or message, so they looked like failures too. Responses that can't be shown and attachments now download (error pages and empty 204/205 responses don't).

### Added
- **Downloads**
  - A save dialog for each download (setting: *Ask where to save each download*, on by default), or straight into the download folder (setting: *Download folder*, default: XDG Downloads).
  - Never overwrites without asking: automatic saves pick `name (1).ext` and so on, and the dialog confirms before replacing. Suggested names are cleaned (no paths, leading dots or control characters).
  - A downloads bar in the window (progress, Open, Show in folder, error text) and a Downloads window (Ctrl+Shift+Y, ⬇ toolbar button or ☰ menu) listing this session's downloads with Open, Show in folder, Cancel and Clear finished.
  - Right-click menu: *Save Image As…*, *Download Image*, *Save Link As…*, *Download Linked File*, *Open Link in New Tab / Window*, *Open Image in New Tab*, and *Save Page As…* on the page.
  - *Save page* (Ctrl+S): HTML only or a complete `.mhtml` web archive.
  - Downloads work in private windows, and are logged in the Ghost Panel (blue **Download** rows in Outgoing, the response in Incoming).
  - Closing the last window waits for running downloads (the Downloads window stays open).
- **Tabbed browsing**
  - Tabs in every window (one web view each, sharing the window's settings, cookie rules and session; private-window tabs share the private session). Each tab shows a loading spinner, the page title and a close button, and tabs can be dragged to reorder.
  - + button and Ctrl+T (opens the homepage), Ctrl+W (closing the last tab closes the window), middle-click a tab to close it.
  - Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+PgDn / Ctrl+PgUp, Ctrl+1…8, Ctrl+9 (last tab).
  - Middle-click or Ctrl+click a link, or *Open Link in New Tab*, opens a background tab. `target="_blank"` links and click-triggered `window.open()` open a tab next to the current one; pop-ups without a click are still blocked. A tab opened only to download a file closes itself again.
  - Address bar, back/forward/reload, cookie button, ☆, HTTPS fallback bar and window title follow the active tab.
  - Ghost Panel: a **Tab** column (`#n`, shown when you hover a tab) and a *Current tab only* filter.
- Shortcuts: Ctrl+N new window, Ctrl+Shift+N private window, Ctrl+R / F5 reload, Alt+Left / Alt+Right back / forward. Favorites: middle-click or right-click → *Open in new tab*.
- Config keys `ask_where_to_save` (default `true`) and `download_dir` (default empty = XDG Downloads). Older config files keep working.

### Known limits
- Tabs have no site icons (favicons): WebKitGTK only provides them through an on-disk icon database that would keep a list of sites you visit.
- Tabs can't be dragged between windows, and closed tabs can't be reopened.
- The download list lasts for the session only.
- *Save page as HTML only* downloads the page again rather than saving what's on screen. Use the `.mhtml` option for an exact copy.

## [1.2.0] - 2026-10-07

### Added
- **Ghost Panel** (F8 or the ☰ menu): a resizable panel on the right with three sections.
  - Active connections: host, IP:port, protocol and process, refreshed every 1.5 s.
  - Outgoing: time, flag, method, host and type. Blocked rows are red and third-party rows are orange.
  - Incoming: time, status, content type, host and size.
  - Each list keeps up to 500 rows and has a count and a Clear button. The panel remembers its width.
- **Tracker blocking**: about 95,000 tracker and ad hosts from EasyPrivacy and EasyList, blocked when loaded from another site (WebKit extension `libspectral-ext.so`). Run `make blocklist` to refresh the list.
- **Per-site cookie consent**: a "🛡 Cookies off / 🍪 Cookies on" button by the address bar. Cookies are blocked unless you allow them for that site.
- **HTTPS-only mode**: `http://` is upgraded to `https://`, with a per-host "Load over HTTP for this session" fallback.
- **Favorites**: the ☆/★ button adds or removes the current page, and a favorites bar shows them (right-click to rename or remove).
- **Homepage**: a Home button, plus a Settings dialog with a homepage field, "Use current page" and a dark-mode switch.
- "Clear browsing data" and "New private window" menu items.
- Command-line flags: `--ghost`, `--debug` and `--version`.
- JSON config at `~/.config/spectral-browser/config.json` (folder 0700, file 0600). A broken file is moved aside.

### Changed
- Ported from WebKit2GTK 4.0 (2.28.1, from 2020) to WebKit2GTK 4.1 (2.52.6) on GTK3.
- Web content now runs in WebKit's bubblewrap sandbox.
- Third-party cookies are always blocked. Cookies are stored in `~/.local/share/spectral-browser/` (0700).
- DNS prefetch and WebRTC are off, and pages can't open pop-ups or read the clipboard on their own.
- Permission requests (camera, mic, location, notifications) ask first, with Deny as the default.
- .deb dependencies: libc6, libglib2.0-0t64, libgtk-3-0t64, libsoup-3.0-0, libwebkit2gtk-4.1-0, bubblewrap and xdg-dbus-proxy.

### Known limits
- Tracker blocking matches whole hosts only, so some trackers get through and fingerprinting isn't blocked.
- Host names in Active connections are best guesses (shared CDN IPs), and only TCP is shown.
- Sites you haven't allowed can set cookies with scripts for the current session. These are deleted at the next start or exit.

## [1.1.0] - 2026-10-07

### Fixed
- The launcher icon did nothing: the Go build was a console stub with no window. Rewritten in Go with cgo GTK3 and WebKit2GTK, so it now opens a real browser window.
- The `.desktop` launcher now points at the binary that was actually tested.

### Added
- Debian package `spectral-browser_1.1.0_amd64.deb` (`make deb`).
- `Read-Me.md` with version, dependencies and install instructions.

## [1.0.0]

- First version, scaffolded by Goose. It had a console-only Go build and an unfinished C/GTK attempt (kept in `old-c/`).
