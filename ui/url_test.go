package ui

import (
	"strings"
	"testing"
)

func TestNormalizeURL(t *testing.T) {
	for in, want := range map[string]string{
		"go.dev":          "https://go.dev",
		"http://x.com":    "http://x.com",
		"golang generics": "https://duckduckgo.com/?q=golang+generics",
		"localhost:8080":  "https://localhost:8080",
		"about:blank":     "about:blank",
	} {
		if got := NormalizeURL(in); got != want {
			t.Errorf("%q: got %q want %q", in, got, want)
		}
	}
}

func TestHTTPSOnly(t *testing.T) {
	p := NewHTTPSOnly()
	if got := p.Upgrade("http://example.com/a?b=1"); got != "https://example.com/a?b=1" {
		t.Fatal(got)
	}
	if p.Upgrade("http://192.168.1.5/") != "" || p.Upgrade("https://example.com") != "" {
		t.Fatal("should not upgrade")
	}
	if got := p.Fallback("https://example.com/a"); got != "http://example.com/a" {
		t.Fatal(got)
	}
	p.Allow("http://example.com/")
	if p.Upgrade("http://example.com/") != "" {
		t.Fatal("allowed host upgraded")
	}
}

func TestSiteAndConsentFilter(t *testing.T) {
	if s := Site("en.wikipedia.org"); s != "wikipedia.org" {
		t.Fatal(s)
	}
	if s := Site("www.bbc.co.uk"); s != "bbc.co.uk" {
		t.Fatal(s)
	}
	want := `[{"trigger":{"url-filter":".*","unless-top-url":["^https?://([^/]*\\.)?wikipedia\\.org[:/]"]},"action":{"type":"block-cookies"}}]`
	if got := ConsentFilter([]string{"wikipedia.org"}); got != want {
		t.Fatalf("got %s", got)
	}
}

func TestGoogleSiteGroup(t *testing.T) {
	for _, h := range []string{"accounts.google.com", "mail.google.com", "www.youtube.com", "ssl.gstatic.com", "lh3.googleusercontent.com", "gmail.com"} {
		if s := Site(h); s != "google.com" {
			t.Errorf("%s -> %s", h, s)
		}
	}
	if s := Site("googlesyndication.com"); s != "googlesyndication.com" {
		t.Errorf("ad domain grouped: %s", s)
	}
	f := ConsentFilter([]string{"google.com"})
	for _, d := range []string{`youtube\\.com`, `gmail\\.com`, `gstatic\\.com`} {
		if !strings.Contains(f, d) {
			t.Errorf("filter missing %s", d)
		}
	}
}
