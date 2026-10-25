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
