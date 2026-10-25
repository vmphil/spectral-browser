package ui

import (
	"net"
	"net/url"
	"strings"

	"golang.org/x/net/publicsuffix"
)

// NormalizeURL turns user input into a URL: bare hosts get https://,
// and anything that isn't a URL becomes a DuckDuckGo search.
func NormalizeURL(s string) string {
	s = strings.TrimSpace(s)
	switch {
	case s == "", strings.Contains(s, "://"), strings.HasPrefix(s, "about:"):
		return s
	case strings.ContainsAny(s, " \t") || !strings.ContainsAny(s, ".:"):
		return "https://duckduckgo.com/?q=" + url.QueryEscape(s)
	}
	return "https://" + s
}

// Host returns the lower-case host of rawURL ("" if none).
func Host(rawURL string) string {
	u, err := url.Parse(rawURL)
	if err != nil {
		return ""
	}
	return strings.ToLower(u.Hostname())
}

// localHost reports hosts that may stay on plain HTTP (no public TLS possible).
func localHost(h string) bool {
	if h == "localhost" || strings.HasSuffix(h, ".local") || strings.HasSuffix(h, ".localhost") {
		return true
	}
	ip := net.ParseIP(h)
	return ip != nil && (ip.IsLoopback() || ip.IsPrivate() || ip.IsLinkLocalUnicast())
}

// HTTPSOnly tracks per-session HTTP exceptions and which hosts were upgraded.
type HTTPSOnly struct {
	allowed, upgraded map[string]bool
}

// NewHTTPSOnly returns an empty policy.
func NewHTTPSOnly() *HTTPSOnly {
	return &HTTPSOnly{allowed: map[string]bool{}, upgraded: map[string]bool{}}
}

// Upgrade returns the https:// form of an http:// URL that should be
// upgraded, or "" to leave the navigation alone.
func (p *HTTPSOnly) Upgrade(rawURL string) string {
	u, err := url.Parse(rawURL)
	if err != nil || u.Scheme != "http" {
		return ""
	}
	h := strings.ToLower(u.Hostname())
	if p.allowed[h] || localHost(h) {
		return ""
	}
	p.upgraded[h] = true
	u.Scheme = "https"
	if u.Port() == "80" {
		u.Host = u.Hostname()
	}
	return u.String()
}

// Fallback returns the http:// URL to offer when an upgraded https:// load
// failed, or "".
func (p *HTTPSOnly) Fallback(rawURL string) string {
	u, err := url.Parse(rawURL)
	if err != nil || u.Scheme != "https" || !p.upgraded[strings.ToLower(u.Hostname())] {
		return ""
	}
	u.Scheme = "http"
	return u.String()
}

// Allow permits plain HTTP for rawURL's host for the rest of the session.
func (p *HTTPSOnly) Allow(rawURL string) { p.allowed[Host(rawURL)] = true }

// Site returns the registrable domain ("eTLD+1") of a host, e.g.
// en.wikipedia.org -> wikipedia.org. Cookie consent is stored per site.
//
// Google's sign-in hops between several domains, so they count as one site
// ("google.com") for cookie consent.
func Site(host string) string {
	s, err := publicsuffix.EffectiveTLDPlusOne(host)
	if err != nil {
		s = host
	}
	for _, g := range siteGroups {
		for _, m := range g {
			if s == m {
				return g[0]
			}
		}
	}
	return s
}

var siteGroups = [][]string{
	{"google.com", "gmail.com", "youtube.com", "gstatic.com", "googleusercontent.com",
		"googleapis.com", "googlevideo.com", "ytimg.com", "ggpht.com", "withgoogle.com"},
}

// SiteGroup returns every domain that shares site's cookie consent.
func SiteGroup(site string) []string {
	for _, g := range siteGroups {
		if g[0] == site {
			return g
		}
	}
	return []string{site}
}
