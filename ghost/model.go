// Package ghost holds the Ghost Panel's traffic model and connection scanner.
package ghost

import (
	"net/url"
	"path"
	"strings"
	"sync"
	"time"
)

// Cap is the most rows each list keeps.
const Cap = 300

// Verdicts for outgoing requests.
const (
	Blocked    = "Blocked"
	ThirdParty = "Third-party"
	Allowed    = "Allowed"
	Download   = "Download" // a file saved to disk
)

// Out is a request leaving the browser.
type Out struct {
	Tab                         int // browser tab id, 0 if none
	Time                        time.Time
	Verdict, Method, Host, Type string
	URL                         string
}

// In is a response coming back.
type In struct {
	Tab        int // browser tab id, 0 if none
	Time       time.Time
	Host, Type string
	Status     int
	Size       int64
}

// Model buffers rows until the UI drains them on its main loop.
type Model struct {
	mu                  sync.Mutex
	out                 []Out
	in                  []In
	OutN, BlockedN, InN int
	hosts               map[string]time.Time
	on                  bool
}

// SetEnabled starts or stops recording. Stopping frees every buffered row,
// counter and remembered host; nothing is kept while the panel is closed.
func (m *Model) SetEnabled(on bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.on = on
	m.out, m.in = nil, nil
	m.OutN, m.BlockedN, m.InN = 0, 0, 0
	m.hosts = map[string]time.Time{}
}

// Enabled reports whether rows are being recorded.
func (m *Model) Enabled() bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.on
}

// NewModel returns an empty model.
func NewModel() *Model { return &Model{hosts: map[string]time.Time{}} }

// AddOut records an outgoing request from tab. accept is the Accept header, if any.
func (m *Model) AddOut(tab int, verdict, method, rawURL, accept string) Out {
	o := Out{Tab: tab, Time: time.Now(), Verdict: verdict, Method: method, URL: rawURL,
		Host: HostOf(rawURL), Type: GuessType(rawURL, accept)}
	m.mu.Lock()
	defer m.mu.Unlock()
	if !m.on {
		return o
	}
	m.out = keep(append(m.out, o))
	m.OutN++
	if verdict == Blocked {
		m.BlockedN++
	} else if len(m.hosts) < 2000 {
		m.hosts[o.Host] = o.Time
	}
	return o
}

// AddIn records a finished response for tab.
func (m *Model) AddIn(tab int, rawURL string, status int, mime string, size int64) In {
	i := In{Tab: tab, Time: time.Now(), Host: HostOf(rawURL), Status: status, Type: mime, Size: size}
	m.mu.Lock()
	defer m.mu.Unlock()
	if !m.on {
		return i
	}
	m.in = keep(append(m.in, i))
	m.InN++
	return i
}

func keep[T any](s []T) []T {
	if len(s) > Cap {
		return s[len(s)-Cap:]
	}
	return s
}

// Drain returns and clears the buffered rows.
func (m *Model) Drain() ([]Out, []In) {
	m.mu.Lock()
	defer m.mu.Unlock()
	o, i := m.out, m.in
	m.out, m.in = nil, nil
	return o, i
}

// Reset zeroes counters for list which ("out" or "in").
func (m *Model) Reset(which string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	switch which {
	case "out":
		m.OutN, m.BlockedN = 0, 0
	case "in":
		m.InN = 0
	}
}

// RecentHosts returns hosts contacted in the last ten minutes.
func (m *Model) RecentHosts() []string {
	m.mu.Lock()
	defer m.mu.Unlock()
	cut := time.Now().Add(-10 * time.Minute)
	var hs []string
	for h, t := range m.hosts {
		if t.Before(cut) {
			delete(m.hosts, h)
			continue
		}
		hs = append(hs, h)
	}
	return hs
}

// HostOf returns the lower-case host of rawURL.
func HostOf(rawURL string) string {
	u, err := url.Parse(rawURL)
	if err != nil {
		return ""
	}
	return strings.ToLower(u.Hostname())
}

// GuessType labels a request from its path extension and Accept header.
func GuessType(rawURL, accept string) string {
	u, err := url.Parse(rawURL)
	if err == nil {
		switch strings.ToLower(path.Ext(u.Path)) {
		case ".js", ".mjs":
			return "script"
		case ".css":
			return "style"
		case ".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".ico", ".avif":
			return "image"
		case ".woff", ".woff2", ".ttf", ".otf":
			return "font"
		case ".mp4", ".webm", ".m3u8", ".mp3":
			return "media"
		}
	}
	switch a := accept; {
	case strings.HasPrefix(a, "text/html"):
		return "document"
	case strings.HasPrefix(a, "image/"):
		return "image"
	case strings.HasPrefix(a, "text/css"):
		return "style"
	case a == "" || a == "*/*":
		return "fetch/other"
	}
	return "other"
}
