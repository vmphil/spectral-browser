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
	path               string
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
