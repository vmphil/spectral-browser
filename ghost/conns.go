package ghost

import (
	"bufio"
	"context"
	"encoding/hex"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Conn is one established TCP connection owned by a browser process.
type Conn struct {
	Host, Remote, Proto, Process string
}

// Descendants returns pid and every process below it.
func Descendants(pid int) map[int]string {
	parent := map[int]int{}
	names := map[int]string{}
	ents, _ := os.ReadDir("/proc")
	for _, e := range ents {
		p, err := strconv.Atoi(e.Name())
		if err != nil {
			continue
		}
		b, err := os.ReadFile(filepath.Join("/proc", e.Name(), "stat"))
		if err != nil {
			continue
		}
		s := string(b)
		r := strings.LastIndexByte(s, ')')
		l := strings.IndexByte(s, '(')
		if r < 0 || l < 0 {
			continue
		}
		f := strings.Fields(s[r+1:])
		if len(f) < 2 {
			continue
		}
		pp, _ := strconv.Atoi(f[1])
		parent[p], names[p] = pp, s[l+1:r]
	}
	out := map[int]string{pid: names[pid]}
	for changed := true; changed; {
		changed = false
		for p, pp := range parent {
			if _, in := out[pp]; in {
				if _, done := out[p]; !done {
					out[p], changed = names[p], true
				}
			}
		}
	}
	return out
}

func socketInodes(pid int) map[string]bool {
	inodes := map[string]bool{}
	dir := fmt.Sprintf("/proc/%d/fd", pid)
	ents, _ := os.ReadDir(dir)
	for _, e := range ents {
		l, err := os.Readlink(filepath.Join(dir, e.Name()))
		if err == nil && strings.HasPrefix(l, "socket:[") {
			inodes[l[8:len(l)-1]] = true
		}
	}
	return inodes
}

// parseAddr decodes a /proc/net/tcp "HEX:PORT" field.
func parseAddr(s string) (net.IP, int, error) {
	h, p, ok := strings.Cut(s, ":")
	if !ok {
		return nil, 0, fmt.Errorf("bad addr %q", s)
	}
	b, err := hex.DecodeString(h)
	if err != nil || (len(b) != 4 && len(b) != 16) {
		return nil, 0, fmt.Errorf("bad ip %q", h)
	}
	for i := 0; i < len(b); i += 4 { // each 32-bit word is little-endian
		b[i], b[i+1], b[i+2], b[i+3] = b[i+3], b[i+2], b[i+1], b[i]
	}
	port, err := strconv.ParseInt(p, 16, 32)
	ip := net.IP(b)
	if v4 := ip.To4(); v4 != nil {
		ip = v4
	}
	return ip, int(port), err
}

type sock struct {
	ip   net.IP
	port int
}

// established maps socket inode -> remote for ESTABLISHED sockets in pid's netns.
func established(pid int) map[string]sock {
	m := map[string]sock{}
	for _, f := range []string{"tcp", "tcp6"} {
		fh, err := os.Open(fmt.Sprintf("/proc/%d/net/%s", pid, f))
		if err != nil {
			continue
		}
		sc := bufio.NewScanner(fh)
		sc.Scan() // header
		for sc.Scan() {
			c := strings.Fields(sc.Text())
			if len(c) < 10 || c[3] != "01" {
				continue
			}
			ip, port, err := parseAddr(c[2])
			if err == nil {
				m[c[9]] = sock{ip, port}
			}
		}
		fh.Close()
	}
	return m
}

// Scan lists established connections held by pid's process tree.
func Scan(pid int) []Conn {
	var conns []Conn
	tables := map[string]map[string]sock{}
	for p, name := range Descendants(pid) {
		inodes := socketInodes(p)
		if len(inodes) == 0 {
			continue
		}
		ns, _ := os.Readlink(fmt.Sprintf("/proc/%d/ns/net", p))
		t, ok := tables[ns]
		if !ok {
			t = established(p)
			tables[ns] = t
		}
		for ino := range inodes {
			s, ok := t[ino]
			if !ok || s.ip.IsLoopback() {
				continue
			}
			conns = append(conns, Conn{Remote: net.JoinHostPort(s.ip.String(), strconv.Itoa(s.port)),
				Proto: proto(s.port), Process: name})
		}
	}
	return conns
}

func proto(port int) string {
	switch port {
	case 443:
		return "HTTPS (TLS)"
	case 80:
		return "HTTP"
	case 853:
		return "DNS-over-TLS"
	}
	return "TCP"
}

// Watcher periodically scans connections and names their remote IPs.
type Watcher struct {
	mu      sync.Mutex
	conns   []Conn
	version int
	names   map[string]string // ip -> host
	tried   map[string]time.Time
	stop    chan struct{}
	Scans   int // completed scans, for --debug
}

// maxNames caps the IP->host and lookup-attempt caches.
const maxNames = 2000

// Start scans pid every interval, mapping IPs using hosts from m.
func Start(pid int, m *Model, every time.Duration) *Watcher {
	w := &Watcher{names: map[string]string{}, tried: map[string]time.Time{}, stop: make(chan struct{})}
	go func() {
		t := time.NewTicker(every)
		defer t.Stop()
		for {
			select {
			case <-w.stop:
				return
			default:
			}
			w.resolve(m.RecentHosts())
			cs := Scan(pid)
			w.mu.Lock()
			for i := range cs {
				ip, _, _ := net.SplitHostPort(cs[i].Remote)
				cs[i].Host = w.names[ip]
			}
			sort.Slice(cs, func(i, j int) bool { return cs[i].Host+cs[i].Remote < cs[j].Host+cs[j].Remote })
			if fmt.Sprint(cs) != fmt.Sprint(w.conns) {
				w.conns, w.version = cs, w.version+1
			}
			w.mu.Unlock()
			w.reverse(cs)
			w.mu.Lock()
			w.Scans++
			w.mu.Unlock()
			select {
			case <-w.stop:
				return
			case <-t.C:
			}
		}
	}()
	return w
}

// Stop ends scanning and drops every cached connection and name. A lookup
// already in flight finishes (≤1 s) and is then discarded.
func (w *Watcher) Stop() int {
	close(w.stop)
	w.mu.Lock()
	defer w.mu.Unlock()
	n := w.Scans
	w.conns, w.names, w.tried = nil, map[string]string{}, map[string]time.Time{}
	return n
}

// resolve maps forward lookups of recently requested hosts to their IPs.
func (w *Watcher) resolve(hosts []string) {
	for _, h := range hosts {
		if h == "" || !w.due(h) {
			continue
		}
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		ips, _ := net.DefaultResolver.LookupHost(ctx, h)
		cancel()
		w.mu.Lock()
		for _, ip := range ips {
			w.names[ip] = h
		}
		w.mu.Unlock()
	}
}

// reverse falls back to reverse DNS for IPs no request explained.
func (w *Watcher) reverse(cs []Conn) {
	for _, c := range cs {
		ip, _, _ := net.SplitHostPort(c.Remote)
		if c.Host != "" || !w.due("rdns:"+ip) {
			continue
		}
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		names, _ := net.DefaultResolver.LookupAddr(ctx, ip)
		cancel()
		if len(names) > 0 {
			w.mu.Lock()
			if _, ok := w.names[ip]; !ok {
				w.names[ip] = "~" + strings.TrimSuffix(names[0], ".")
			}
			w.mu.Unlock()
		}
	}
}

func (w *Watcher) due(key string) bool {
	w.mu.Lock()
	defer w.mu.Unlock()
	if t, ok := w.tried[key]; ok && time.Since(t) < 5*time.Minute {
		return false
	}
	if len(w.tried) >= maxNames {
		w.tried = map[string]time.Time{}
	}
	if len(w.names) >= maxNames {
		w.names = map[string]string{}
	}
	w.tried[key] = time.Now()
	return true
}

// Snapshot returns the current connections if they changed since version v.
func (w *Watcher) Snapshot(v int) ([]Conn, int, bool) {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.conns, w.version, w.version != v
}
