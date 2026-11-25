// Package config loads and saves browser settings as JSON.
package config

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"time"
)

// Favorite is a bookmarked page.
type Favorite struct {
	Name string `json:"name"`
	URL  string `json:"url"`
}

// Config is the browser configuration.
type Config struct {
	Home        string     `json:"home"`
	DarkMode    bool       `json:"dark_mode"`
	Favorites   []Favorite `json:"favorites"`
	CookieSites []string   `json:"cookie_sites"` // hosts allowed to keep cookies
	PanelWidth  int        `json:"ghost_panel_width"`
	AskDownload bool       `json:"ask_where_to_save"`      // show a save dialog for each download
	DownloadDir string     `json:"download_dir,omitempty"` // "" = XDG Downloads folder
	// ClearCookiesOnExit deletes every cookie (accepted sites too) when the
	// browser closes and again at start, in case it was killed.
	ClearCookiesOnExit bool `json:"clear_cookies_on_exit"`
	// LowPowerGraphics, when true, asks pages to reduce motion and turns
	// off smooth scrolling. It does not disable GPU compositing: forcing
	// the software compositor (including WEBKIT_DISABLE_COMPOSITING_MODE /
	// WEBKIT_DISABLE_DMABUF_RENDERER) has been seen to raise
	// WebKitWebProcess CPU. Off by default. SPECTRAL_LOW_POWER=0 or 1
	// overrides the saved value. nil means off.
	LowPowerGraphics *bool `json:"low_power_graphics,omitempty"`
	// PauseBackgroundTabs hides and mutes WebKit views for tabs that are
	// not visible (and when the window is minimized) so timers and rAF
	// should throttle. On by default. SPECTRAL_PAUSE_BG=0 or 1 overrides.
	// nil means the default (on).
	PauseBackgroundTabs *bool `json:"pause_background_tabs,omitempty"`
	// JSBlockDefault, when true, blocks JavaScript on every site unless
	// the host is in JSAllowed. When false (the default), JavaScript runs
	// unless the host is in JSBlocked.
	JSBlockDefault bool `json:"js_block_default,omitempty"`
	// JSBlocked is the per-site block list used when JSBlockDefault is false.
	JSBlocked []string `json:"js_blocked,omitempty"`
	// JSAllowed is the per-site allow list used when JSBlockDefault is true.
	JSAllowed []string `json:"js_allowed,omitempty"`
	path      string
}

// Default returns the built-in configuration.
func Default() Config {
	return Config{
		Home:     "https://duckduckgo.com",
		DarkMode: true,
		Favorites: []Favorite{
			{"DuckDuckGo", "https://duckduckgo.com"},
			{"Go Docs", "https://go.dev/doc/"},
			{"Wikipedia", "https://en.wikipedia.org"},
		},
		CookieSites:        []string{},
		PanelWidth:         420,
		AskDownload:        true,
		ClearCookiesOnExit: true,
	}
}

// Dir returns ~/.config/spectral-browser.
func Dir() string {
	d, err := os.UserConfigDir()
	if err != nil {
		d = filepath.Join(os.Getenv("HOME"), ".config")
	}
	return filepath.Join(d, "spectral-browser")
}

// DataDir returns ~/.local/share/spectral-browser, created 0700.
func DataDir() string {
	d := os.Getenv("XDG_DATA_HOME")
	if d == "" {
		d = filepath.Join(os.Getenv("HOME"), ".local", "share")
	}
	return ensure(filepath.Join(d, "spectral-browser"))
}

// CacheDir returns ~/.cache/spectral-browser, created 0700.
func CacheDir() string {
	d, err := os.UserCacheDir()
	if err != nil {
		d = filepath.Join(os.Getenv("HOME"), ".cache")
	}
	return ensure(filepath.Join(d, "spectral-browser"))
}

func ensure(dir string) string {
	_ = os.MkdirAll(dir, 0o700)
	_ = os.Chmod(dir, 0o700)
	return dir
}

// Load reads the config file. A missing file yields defaults (and is
// written); a corrupt file is moved aside and defaults are used.
func Load() (*Config, error) {
	c := Default()
	c.path = filepath.Join(ensure(Dir()), "config.json")
	b, err := os.ReadFile(c.path)
	if errors.Is(err, fs.ErrNotExist) {
		return &c, c.Save()
	}
	if err != nil {
		return &c, err
	}
	loaded := Default()
	if err := json.Unmarshal(b, &loaded); err != nil {
		bad := fmt.Sprintf("%s.corrupt-%d", c.path, time.Now().Unix())
		_ = os.Rename(c.path, bad)
		return &c, fmt.Errorf("config corrupt, moved to %s: %w", bad, err)
	}
	loaded.path = c.path
	if loaded.Home == "" {
		loaded.Home = c.Home
	}
	if loaded.PanelWidth < 150 {
		loaded.PanelWidth = c.PanelWidth
	}
	return &loaded, nil
}

// Save writes the config atomically with mode 0600.
func (c *Config) Save() error {
	if c.path == "" {
		c.path = filepath.Join(ensure(Dir()), "config.json")
	}
	b, err := json.MarshalIndent(c, "", "  ")
	if err != nil {
		return err
	}
	tmp := c.path + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, c.path)
}

// FavIndex returns the index of the favorite with url, or -1.
func (c *Config) FavIndex(url string) int {
	return slices.IndexFunc(c.Favorites, func(f Favorite) bool { return f.URL == url })
}

// ToggleFavorite adds url (named name) or removes it if present.
func (c *Config) ToggleFavorite(url, name string) {
	if i := c.FavIndex(url); i >= 0 {
		c.Favorites = slices.Delete(c.Favorites, i, i+1)
		return
	}
	if strings.TrimSpace(name) == "" {
		name = url
	}
	c.Favorites = append(c.Favorites, Favorite{Name: name, URL: url})
}

// CookiesAllowed reports whether host (or a parent domain) was accepted.
func (c *Config) CookiesAllowed(host string) bool {
	for _, s := range c.CookieSites {
		if host == s || strings.HasSuffix(host, "."+s) {
			return true
		}
	}
	return false
}

// SetCookiesAllowed adds or removes host from the accepted list.
func (c *Config) SetCookiesAllowed(host string, on bool) {
	c.CookieSites = slices.DeleteFunc(c.CookieSites, func(s string) bool {
		return s == host || strings.HasSuffix(host, "."+s)
	})
	if on {
		c.CookieSites = append(c.CookieSites, host)
	}
}

// LowPowerGraphicsDefault is false on every platform. The setting is
// opt-in reduce-motion / no smooth scrolling, not a software compositor.
func LowPowerGraphicsDefault() bool {
	return false
}

// envLowPower reports the SPECTRAL_LOW_POWER override, if set.
func envLowPower() (on bool, set bool) {
	v := strings.TrimSpace(os.Getenv("SPECTRAL_LOW_POWER"))
	if v == "" {
		return false, false
	}
	switch strings.ToLower(v) {
	case "0", "false", "off", "no":
		return false, true
	default:
		return true, true
	}
}

// LowPowerGraphicsEnabled is the effective low-power graphics setting:
// SPECTRAL_LOW_POWER, then the saved config, then the platform default.
func (c *Config) LowPowerGraphicsEnabled() bool {
	if on, set := envLowPower(); set {
		return on
	}
	if c != nil && c.LowPowerGraphics != nil {
		return *c.LowPowerGraphics
	}
	return LowPowerGraphicsDefault()
}

// PauseBackgroundTabsDefault is true on every platform: hidden tabs
// should not keep burning CPU.
func PauseBackgroundTabsDefault() bool {
	return true
}

func envPauseBackground() (on bool, set bool) {
	v := strings.TrimSpace(os.Getenv("SPECTRAL_PAUSE_BG"))
	if v == "" {
		return false, false
	}
	switch strings.ToLower(v) {
	case "0", "false", "off", "no":
		return false, true
	default:
		return true, true
	}
}

// PauseBackgroundTabsEnabled is the effective setting: SPECTRAL_PAUSE_BG,
// then the saved config, then on.
func (c *Config) PauseBackgroundTabsEnabled() bool {
	if on, set := envPauseBackground(); set {
		return on
	}
	if c != nil && c.PauseBackgroundTabs != nil {
		return *c.PauseBackgroundTabs
	}
	return PauseBackgroundTabsDefault()
}

func hostListed(list []string, host string) bool {
	host = strings.ToLower(strings.TrimSpace(host))
	if host == "" {
		return false
	}
	for _, s := range list {
		s = strings.ToLower(strings.TrimSpace(s))
		if s != "" && (host == s || strings.HasSuffix(host, "."+s)) {
			return true
		}
	}
	return false
}

func setHostListed(list *[]string, host string, on bool) {
	host = strings.ToLower(strings.TrimSpace(host))
	*list = slices.DeleteFunc(*list, func(s string) bool {
		s = strings.ToLower(strings.TrimSpace(s))
		return s == host || (host != "" && (strings.HasSuffix(host, "."+s) || strings.HasSuffix(s, "."+host)))
	})
	if on && host != "" {
		*list = append(*list, host)
	}
}

// JSEnabled reports whether JavaScript should run for host. An empty
// host (about:blank) follows the global default: allowed unless
// JSBlockDefault is on.
func (c *Config) JSEnabled(host string) bool {
	if c == nil {
		return true
	}
	if c.JSBlockDefault {
		if host == "" {
			return false
		}
		return hostListed(c.JSAllowed, host)
	}
	if host == "" {
		return true
	}
	return !hostListed(c.JSBlocked, host)
}

// ToggleJS flips JavaScript for host and returns the new enabled state.
// host should already be the site the caller wants stored (eTLD+1).
func (c *Config) ToggleJS(host string) bool {
	host = strings.ToLower(strings.TrimSpace(host))
	on := !c.JSEnabled(host)
	if c.JSBlockDefault {
		setHostListed(&c.JSAllowed, host, on)
	} else {
		setHostListed(&c.JSBlocked, host, !on)
	}
	return on
}
