![Spectral Browser icon](assets/SpecB.png)

# Spectral Browser

**A small, privacy-minded web browser for Linux.**

Spectral is a lightweight browser written in Go with GTK 3 and WebKit2GTK 4.1. It blocks about 95,000 tracker and ad hosts, keeps you on HTTPS, asks before sites set cookies, and clears them when you close it. Press F8 to open the Ghost Panel and watch every connection a page makes, live. Version 1.3.3 runs on amd64 (Pop!_OS / Ubuntu 24.04) and arm64 (PineTab2 / Arrow Silver).

- Ghost Panel (F8): a live traffic monitor that runs only while open and writes nothing to disk
- Tracker and ad blocking from EasyPrivacy and EasyList
- HTTPS-only mode, with a per-session HTTP fallback
- Per-site cookie consent; all cookies cleared on close by default
- Web process sandboxed with bubblewrap; WebRTC and DNS prefetch off

[Download 1.3.3](#download) · [Features](#features) · [Install guide](#install)
