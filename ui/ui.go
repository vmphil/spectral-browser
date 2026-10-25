// Package ui is the GTK3 + WebKit2GTK 4.1 browser window. Widgets live in
// browser.c; this file holds state and the functions C calls back into.
package ui

/*
#cgo pkg-config: gtk+-3.0 webkit2gtk-4.1
#cgo CFLAGS: -Wno-deprecated-declarations
#include <stdlib.h>
#include "browser.h"
*/
import "C"

import (
	"fmt"
	"html"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"time"
	"unsafe"

	"github.com/philip/spectral-browser/blocklist"
	"github.com/philip/spectral-browser/config"
	"github.com/philip/spectral-browser/ghost"
)

// Options control startup.
type Options struct {
	URL   string // first page; empty means the homepage
	Ghost bool   // open the Ghost Panel immediately
	Debug bool   // log traffic to stdout
}

var (
	cfg        *config.Config
	opts       Options
	model      = ghost.NewModel()
	watcher    *ghost.Watcher
	connVer    = -1
	https      = NewHTTPSOnly()
	blockPath  string
	extDir     string
	widthDirty bool
)

const (
	rowConn = iota
	rowOut
	rowIn
)

// Run opens the browser window and blocks until every window is closed.
func Run(c *config.Config, o Options) {
	runtime.LockOSThread()
	if os.Getenv("SPECTRAL_SELFTEST") != "" {
		o.Debug = true
	}
	cfg, opts = c, o
	var err error
	if blockPath, err = blocklist.Install(config.DataDir()); err != nil {
		log.Printf("blocklist: %v", err)
	}
	extDir = findExtDir()
	if extDir == "" {
		log.Print("warning: web extension not found; tracker blocking and the Outgoing list are off")
	}
	first := o.URL
	if first == "" {
		first = cfg.Home
	}
	u := C.CString(first)
	defer C.free(unsafe.Pointer(u))
	C.sb_run(u, cbool(o.Ghost))
	if widthDirty {
		save()
	}
	if o.Debug {
		log.Printf("[ghost] exit: outgoing=%d blocked=%d incoming=%d", model.OutN, model.BlockedN, model.InN)
	}
}

// findExtDir locates libspectral-ext.so: $SPECTRAL_EXT_DIR, next to the
// binary (bin/ext), or the installed /usr/lib/spectral-browser.
func findExtDir() string {
	var dirs []string
	if d := os.Getenv("SPECTRAL_EXT_DIR"); d != "" {
		dirs = append(dirs, d)
	}
	if exe, err := os.Executable(); err == nil {
		dirs = append(dirs, filepath.Join(filepath.Dir(exe), "ext"))
	}
	dirs = append(dirs, "/usr/lib/spectral-browser")
	for _, d := range dirs {
		if _, err := os.Stat(filepath.Join(d, "libspectral-ext.so")); err == nil {
			return d
		}
	}
	return ""
}

func save() {
	if err := cfg.Save(); err != nil {
		log.Printf("config: %v", err)
	}
	widthDirty = false
}

func cbool(b bool) C.int {
	if b {
		return 1
	}
	return 0
}

func cstr(s string) *C.char { return C.CString(s) }

//export goNormalizeURL
func goNormalizeURL(s *C.char) *C.char { return cstr(NormalizeURL(C.GoString(s))) }

//export goHostOf
func goHostOf(s *C.char) *C.char { return cstr(Host(C.GoString(s))) }

//export goHomeURL
func goHomeURL() *C.char { return cstr(cfg.Home) }

//export goDark
func goDark() C.int { return cbool(cfg.DarkMode) }

//export goDataDir
func goDataDir() *C.char { return cstr(config.DataDir()) }

//export goCacheDir
func goCacheDir() *C.char { return cstr(config.CacheDir()) }

//export goExtDir
func goExtDir() *C.char { return cstr(extDir) }

//export goBlocklistPath
func goBlocklistPath() *C.char { return cstr(blockPath) }

//export goSaveSettings
func goSaveSettings(home *C.char, dark, ask, clearExit C.int, dlDir *C.char) {
	cfg.ClearCookiesOnExit = clearExit != 0
	if h := NormalizeURL(C.GoString(home)); h != "" {
		cfg.Home = h
	}
	cfg.DarkMode = dark != 0
	cfg.AskDownload = ask != 0
	if d := C.GoString(dlDir); d != "" {
		if d == xdgDownloadDir() {
			d = "" // follow the XDG folder if it ever moves
		}
		cfg.DownloadDir = d
	}
	save()
}

//export goSameSite
func goSameSite(a, b *C.char) C.int {
	x, y := C.GoString(a), C.GoString(b)
	return cbool(x != "" && y != "" && Site(x) == Site(y))
}

//export goDebug
func goDebug(msg *C.char) {
	if opts.Debug {
		log.Print(C.GoString(msg))
	}
}

//export goSelftest
func goSelftest() *C.char { return cstr(os.Getenv("SPECTRAL_SELFTEST")) }

//export goSelftestSaveDir
func goSelftestSaveDir() *C.char { return cstr(os.Getenv("SPECTRAL_SELFTEST_SAVEAS_DIR")) }

//export goUpgradeURL
func goUpgradeURL(u *C.char) *C.char { return cstr(https.Upgrade(C.GoString(u))) }

//export goHTTPFallback
func goHTTPFallback(u *C.char) *C.char { return cstr(https.Fallback(C.GoString(u))) }

//export goAllowHTTP
func goAllowHTTP(u *C.char) { https.Allow(C.GoString(u)) }

// Favorites.

//export goFavCount
func goFavCount() C.int { return C.int(len(cfg.Favorites)) }

func fav(i C.int) config.Favorite {
	if int(i) < 0 || int(i) >= len(cfg.Favorites) {
		return config.Favorite{}
	}
	return cfg.Favorites[i]
}

//export goFavName
func goFavName(i C.int) *C.char { return cstr(fav(i).Name) }

//export goFavURL
func goFavURL(i C.int) *C.char { return cstr(fav(i).URL) }

//export goIsFavorite
func goIsFavorite(u *C.char) C.int { return cbool(cfg.FavIndex(C.GoString(u)) >= 0) }

//export goToggleFavorite
func goToggleFavorite(u, title *C.char) {
	cfg.ToggleFavorite(C.GoString(u), C.GoString(title))
	save()
}

//export goRenameFavorite
func goRenameFavorite(i C.int, name *C.char) {
	if n := C.GoString(name); n != "" && int(i) >= 0 && int(i) < len(cfg.Favorites) {
		cfg.Favorites[i].Name = n
		save()
	}
}

//export goRemoveFavorite
func goRemoveFavorite(i C.int) {
	if int(i) >= 0 && int(i) < len(cfg.Favorites) {
		cfg.ToggleFavorite(cfg.Favorites[i].URL, "")
		save()
	}
}

// Cookie consent.

//export goCookiesAllowed
func goCookiesAllowed(host *C.char) C.int { return cbool(cookiesAllowed(C.GoString(host))) }

// cookiesAllowed reports whether host's site (or its group) was accepted.
func cookiesAllowed(host string) bool {
	if cfg.CookiesAllowed(host) {
		return true
	}
	for _, m := range SiteGroup(Site(host)) {
		if cfg.CookiesAllowed(m) {
			return true
		}
	}
	return false
}

//export goClearOnExit
func goClearOnExit() C.int { return cbool(cfg.ClearCookiesOnExit) }

//export goIsGoogleLogin
func goIsGoogleLogin(u *C.char) C.int { return cbool(Host(C.GoString(u)) == "accounts.google.com") }

//export goToggleCookies
func goToggleCookies(host *C.char) {
	h := Site(C.GoString(host))
	on := !cookiesAllowed(h)
	for _, m := range SiteGroup(h) {
		cfg.SetCookiesAllowed(m, false)
	}
	cfg.SetCookiesAllowed(h, on)
	save()
	if opts.Debug {
		log.Printf("[consent] %s cookies allowed=%v", h, on)
	}
}

//export goCookiesPurged
func goCookiesPurged(cookieSites, storageSites C.int) {
	if opts.Debug {
		log.Printf("[consent] cleared cookies for %d site(s) (clear on exit=%v), site storage for %d unaccepted site(s)",
			int(cookieSites), cfg.ClearCookiesOnExit, int(storageSites))
	}
}

//export goConsentFilterJSON
func goConsentFilterJSON() *C.char { return cstr(ConsentFilter(cfg.CookieSites)) }

//export goFilterApplied
func goFilterApplied(ok C.int) {
	if opts.Debug {
		log.Printf("[consent] cookie filter applied=%v accepted=%v", ok != 0, cfg.CookieSites)
	}
}

// Ghost Panel.

//export goPanelWidth
func goPanelWidth() C.int { return C.int(cfg.PanelWidth) }

//export goSetPanelWidth
func goSetPanelWidth(w C.int) {
	if int(w) != cfg.PanelWidth {
		cfg.PanelWidth, widthDirty = int(w), true
	}
}

// goMonitor starts (panel opened) or stops (last panel closed) all Ghost
// Panel monitoring and frees its data.
//
//export goMonitor
func goMonitor(on C.int) {
	model.SetEnabled(on != 0)
	scans := 0
	if on != 0 {
		watcher = ghost.Start(os.Getpid(), model, 2*time.Second)
	} else if watcher != nil {
		scans = watcher.Stop()
		watcher = nil
	}
	connVer, lastConns, headersStale = -1, 0, true
	if opts.Debug {
		if on != 0 {
			log.Print("[ghost] monitoring started (connection scanner, batch timer, extension reports)")
		} else {
			log.Printf("[ghost] monitoring stopped after %d connection scans; rows, counters and host caches freed", scans)
		}
	}
}

//export goPanelShown
func goPanelShown(on C.int) {
	headersStale = true
	if opts.Debug {
		log.Printf("[ghost] panel shown width=%d", cfg.PanelWidth)
	}
}

//export goRecordOut
func goRecordOut(tab C.int, verdict, method, u, accept *C.char) {
	if !model.Enabled() {
		return
	}
	o := model.AddOut(int(tab), C.GoString(verdict), C.GoString(method), C.GoString(u), C.GoString(accept))
	if opts.Debug {
		log.Printf("[ghost] out %-4s %-11s %s %s %s", tabTag(o.Tab), o.Verdict, o.Method, o.Host, o.Type)
	}
}

//export goRecordIn
func goRecordIn(tab C.int, u *C.char, status C.int, mime *C.char, size C.longlong) {
	if !model.Enabled() {
		return
	}
	i := model.AddIn(int(tab), C.GoString(u), int(status), C.GoString(mime), int64(size))
	if opts.Debug {
		log.Printf("[ghost] in  %-4s %d %s %s %d", tabTag(i.Tab), i.Status, i.Host, i.Type, i.Size)
	}
}

// tabTag is the Ghost Panel's Tab column, e.g. "#3" ("" for no tab).
func tabTag(id int) string {
	if id <= 0 {
		return ""
	}
	return "#" + strconv.Itoa(id)
}

//export goClearList
func goClearList(which C.int) {
	switch which {
	case rowOut:
		model.Reset("out")
	case rowIn:
		model.Reset("in")
	case rowConn:
		connVer = -1
	}
	C.sb_clear_rows(which)
	headers()
}

func row(which int, tab, color string, cols ...string) {
	cs := make([]*C.char, 7)
	for i := range cs {
		var v string
		switch {
		case i == 0:
			v = tab
		case i == 6:
			v = color
		case i-1 < len(cols):
			v = cols[i-1]
		}
		cs[i] = C.CString(v)
	}
	C.sb_add_row(C.int(which), cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6])
	for _, c := range cs {
		C.free(unsafe.Pointer(c))
	}
}

var lastConns int

// goFlush runs on the GTK main loop every 500 ms and moves buffered rows
// into the list stores in one batch.
//
//export goFlush
func goFlush() {
	if watcher == nil {
		return
	}
	outs, ins := model.Drain()
	dirty := len(outs)+len(ins) > 0
	for _, o := range outs {
		color := ""
		switch o.Verdict {
		case ghost.Blocked:
			color = "#e53935"
		case ghost.ThirdParty:
			color = "#f0a030"
		case ghost.Download:
			color = "#4a90d9"
		}
		row(rowOut, tabTag(o.Tab), color, o.Time.Format("15:04:05"), o.Verdict, o.Method, o.Host, o.Type)
	}
	for _, i := range ins {
		color := ""
		if i.Status >= 400 || i.Status == 0 {
			color = "#e53935"
		}
		row(rowIn, tabTag(i.Tab), color, i.Time.Format("15:04:05"), strconv.Itoa(i.Status), i.Type, i.Host, size(i.Size))
	}
	if conns, v, changed := watcher.Snapshot(connVer); changed {
		connVer, lastConns = v, len(conns)
		for i, c := range conns {
			h := c.Host
			if h == "" {
				h = "(unknown)"
			}
			setConnRow(i, h, c.Remote, c.Proto, c.Process)
		}
		C.sb_trim_rows(rowConn, C.int(len(conns)))
		dirty = true
		if opts.Debug && len(conns) > 0 {
			log.Printf("[ghost] active connections: %d (e.g. %s %s)", len(conns), conns[0].Host, conns[0].Remote)
		}
	}
	if dirty || headersStale {
		headers()
	}
}

// headersStale forces a header refresh on the next flush (panel just opened).
var headersStale = true

func setConnRow(i int, cols ...string) {
	cs := make([]*C.char, 4)
	for j := range cs {
		cs[j] = C.CString(cols[j])
	}
	C.sb_set_row(C.int(rowConn), C.int(i), cs[0], cs[1], cs[2], cs[3])
	for _, c := range cs {
		C.free(unsafe.Pointer(c))
	}
}

func headers() {
	headersStale = false
	h := func(s string) *C.char { return C.CString("<b>" + html.EscapeString(s) + "</b>") }
	a := h(fmt.Sprintf("Active connections (%d)", lastConns))
	b := h(fmt.Sprintf("Outgoing (%d, %d blocked)", model.OutN, model.BlockedN))
	c := h(fmt.Sprintf("Incoming (%d)", model.InN))
	C.sb_set_headers(a, b, c)
	C.free(unsafe.Pointer(a))
	C.free(unsafe.Pointer(b))
	C.free(unsafe.Pointer(c))
}

func size(n int64) string {
	switch {
	case n <= 0:
		return "?"
	case n < 1024:
		return fmt.Sprintf("%d B", n)
	case n < 1<<20:
		return fmt.Sprintf("%.1f KB", float64(n)/1024)
	}
	return fmt.Sprintf("%.1f MB", float64(n)/(1<<20))
}
