## About

Spectral Browser is a small, privacy-minded web browser for Linux, written by Philip Orona in Go with a GTK 3 interface and the WebKit2GTK 4.1 engine. It runs on everyday amd64 desktops (Pop!_OS 24.04) and on arm64 devices like the PineTab2, where it comes preinstalled in the Arrow Silver PineTab2 image.

The idea is simple: block the junk by default and show you what's really happening. Trackers and ads are stopped before they load, pages stay on HTTPS, cookies stay off until you allow them for a site, third-party cookies are always blocked, and everything is wiped when you close the browser. When you're curious, press F8 and the Ghost Panel shows every connection a page makes, live.

- Ghost Panel (F8): a live network monitor that runs only while open
- Tracker and ad blocking (nearly 95,000 hosts from EasyPrivacy and EasyList)
- HTTPS-only browsing
- Per-site cookie toggle, with cookies cleared on close
- Bubblewrap-sandboxed web process; WebRTC and DNS prefetch off
- Simple settings: homepage, dark mode, download folder, ask where to save
- Downloads manager (Ctrl+Shift+Y) and save pages as HTML or complete .mhtml
- Google sign-in bar: one click allows cookies for Google so sign-in works
- Private windows, tabs, favorites bar and DuckDuckGo search
