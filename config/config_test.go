package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadMissingAndCorrupt(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	c, err := Load()
	if err != nil || len(c.Favorites) != 3 {
		t.Fatalf("missing: %v %+v", err, c)
	}
	p := filepath.Join(Dir(), "config.json")
	os.WriteFile(p, []byte("{nope"), 0o600)
	c, err = Load()
	if err == nil || c.Home == "" || !c.AskDownload {
		t.Fatalf("corrupt should warn and default: %v", err)
	}
	if fi, _ := os.Stat(Dir()); fi.Mode().Perm() != 0o700 {
		t.Fatalf("dir mode %v", fi.Mode())
	}
}

func TestCookieSites(t *testing.T) {
	c := Default()
	c.SetCookiesAllowed("example.com", true)
	for host, want := range map[string]bool{"example.com": true, "www.example.com": true, "badexample.com": false} {
		if got := c.CookiesAllowed(host); got != want {
			t.Errorf("%s: got %v", host, got)
		}
	}
	c.SetCookiesAllowed("www.example.com", false)
	if c.CookiesAllowed("example.com") {
		t.Error("should be removed")
	}
}

func TestOldConfigKeepsDownloadDefaults(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	os.MkdirAll(Dir(), 0o700)
	os.WriteFile(filepath.Join(Dir(), "config.json"), []byte(`{"home":"https://go.dev","favorites":[]}`), 0o600)
	c, err := Load()
	if err != nil || !c.AskDownload || !c.ClearCookiesOnExit || c.DownloadDir != "" || c.Home != "https://go.dev" {
		t.Fatalf("1.2.0 config: %v %+v", err, c)
	}
}

func TestLowPowerGraphicsEnabled(t *testing.T) {
	t.Setenv("SPECTRAL_LOW_POWER", "")
	c := Default()
	if c.LowPowerGraphicsEnabled() != LowPowerGraphicsDefault() {
		t.Fatalf("default: got %v want %v", c.LowPowerGraphicsEnabled(), LowPowerGraphicsDefault())
	}
	on, off := true, false
	c.LowPowerGraphics = &on
	if !c.LowPowerGraphicsEnabled() {
		t.Fatal("saved true should be on")
	}
	c.LowPowerGraphics = &off
	if c.LowPowerGraphicsEnabled() {
		t.Fatal("saved false should be off")
	}
	t.Setenv("SPECTRAL_LOW_POWER", "0")
	c.LowPowerGraphics = &on
	if c.LowPowerGraphicsEnabled() {
		t.Fatal("SPECTRAL_LOW_POWER=0 should override saved true")
	}
	t.Setenv("SPECTRAL_LOW_POWER", "1")
	c.LowPowerGraphics = &off
	if !c.LowPowerGraphicsEnabled() {
		t.Fatal("SPECTRAL_LOW_POWER=1 should override saved false")
	}
}

func TestPauseBackgroundTabsEnabled(t *testing.T) {
	t.Setenv("SPECTRAL_PAUSE_BG", "")
	c := Default()
	if !c.PauseBackgroundTabsEnabled() {
		t.Fatal("default should be on")
	}
	on, off := true, false
	c.PauseBackgroundTabs = &off
	if c.PauseBackgroundTabsEnabled() {
		t.Fatal("saved false should be off")
	}
	c.PauseBackgroundTabs = &on
	if !c.PauseBackgroundTabsEnabled() {
		t.Fatal("saved true should be on")
	}
	t.Setenv("SPECTRAL_PAUSE_BG", "0")
	if c.PauseBackgroundTabsEnabled() {
		t.Fatal("SPECTRAL_PAUSE_BG=0 should override saved true")
	}
	t.Setenv("SPECTRAL_PAUSE_BG", "1")
	c.PauseBackgroundTabs = &off
	if !c.PauseBackgroundTabsEnabled() {
		t.Fatal("SPECTRAL_PAUSE_BG=1 should override saved false")
	}
}

func TestJSEnabledAndToggle(t *testing.T) {
	c := Default()
	if !c.JSEnabled("duckduckgo.com") || !c.JSEnabled("") {
		t.Fatal("JS should be on by default, including about:blank")
	}
	if c.ToggleJS("duckduckgo.com") {
		t.Fatal("toggle should block duckduckgo.com")
	}
	if c.JSEnabled("duckduckgo.com") || c.JSEnabled("www.duckduckgo.com") {
		t.Fatal("blocked site and subdomain")
	}
	if !c.JSEnabled("example.com") {
		t.Fatal("other sites stay allowed")
	}
	if !c.ToggleJS("www.duckduckgo.com") {
		t.Fatal("toggle should allow again")
	}
	if !c.JSEnabled("duckduckgo.com") {
		t.Fatal("unblocked")
	}

	c.JSBlockDefault = true
	c.JSBlocked, c.JSAllowed = nil, nil
	if c.JSEnabled("example.com") || c.JSEnabled("") {
		t.Fatal("default-block: JS off everywhere, including about:blank")
	}
	if !c.ToggleJS("example.com") {
		t.Fatal("toggle should allow example.com")
	}
	if !c.JSEnabled("www.example.com") || c.JSEnabled("other.com") {
		t.Fatal("allow list covers subdomain only")
	}
}

func TestOldConfigPauseAndJSDefaults(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	t.Setenv("SPECTRAL_PAUSE_BG", "")
	os.MkdirAll(Dir(), 0o700)
	os.WriteFile(filepath.Join(Dir(), "config.json"), []byte(`{"home":"https://go.dev","favorites":[]}`), 0o600)
	c, err := Load()
	if err != nil || !c.PauseBackgroundTabsEnabled() || c.JSBlockDefault || c.JSEnabled("example.com") == false {
		t.Fatalf("old config should keep pause on and JS allowed: %v %+v", err, c)
	}
}
