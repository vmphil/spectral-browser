package ghost

import (
	"bufio"
	"context"
	"encoding/hex"
	"fmt"
	"net"
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

// descendantsFrom returns pid and every process below it.
// parent maps pid to ppid; names maps pid to the process name.
func descendantsFrom(pid int, parent map[int]int, names map[int]string) map[int]string {
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

// parsePS reads "ps -o pid=,ppid=,comm=" output.
func parsePS(text string) (parent map[int]int, names map[int]string) {
	parent = map[int]int{}
	names = map[int]string{}
	sc := bufio.NewScanner(strings.NewReader(text))
	for sc.Scan() {
		f := strings.Fields(sc.Text())
		if len(f) < 3 {
			continue
		}
		pid, err1 := strconv.Atoi(f[0])
		ppid, err2 := strconv.Atoi(f[1])
		if err1 != nil || err2 != nil {
			continue
		}
		parent[pid] = ppid
		names[pid] = strings.Join(f[2:], " ")
	}
	return parent, names
}

func isTCPProto(s string) bool {
	switch s {
	case "tcp", "tcp4", "tcp6", "tcp46":
		return true
	}
	return false
}

// parseSockAddr parses a sockstat address (1.2.3.4:443 or [2001:db8::1]:443).
func parseSockAddr(s string) (string, int, bool) {
	s = strings.TrimSpace(s)
	if s == "" || s == "*:*" {
		return "", 0, false
	}
	var host, portStr string
	if strings.HasPrefix(s, "[") {
		end := strings.LastIndex(s, "]:")
		if end < 0 {
			return "", 0, false
		}
		host = s[1:end]
		portStr = s[end+2:]
	} else {
		i := strings.LastIndex(s, ":")
		if i < 0 {
			return "", 0, false
		}
		host = s[:i]
		portStr = s[i+1:]
	}
	if host == "" || host == "*" {
		return "", 0, false
	}
	if z := strings.IndexByte(host, '%'); z >= 0 {
		host = host[:z]
	}
	port, err := strconv.Atoi(portStr)
	if err != nil || port < 0 || port > 65535 {
		return "", 0, false
	}
	return host, port, true
}

// connsFromSockstat keeps established TCP sockets owned by procs.
// text is the output of `sockstat -c -P tcp`.
func connsFromSockstat(text string, procs map[int]string) []Conn {
	var conns []Conn
	sc := bufio.NewScanner(strings.NewReader(text))
	for sc.Scan() {
		f := strings.Fields(sc.Text())
		protoIdx := -1
		for i, field := range f {
			if isTCPProto(field) {
				protoIdx = i
				break
			}
		}
		// pid, fd, proto, local, foreign
		if protoIdx < 2 || protoIdx+2 >= len(f) {
			continue
		}
		pid, err := strconv.Atoi(f[protoIdx-2])
		if err != nil {
			continue
		}
		name, ok := procs[pid]
		if !ok {
			continue
		}
		ipStr, port, ok := parseSockAddr(f[protoIdx+2])
		if !ok {
			continue
		}
		ip := net.ParseIP(ipStr)
		if ip == nil || ip.IsLoopback() {
			continue
		}
		conns = append(conns, Conn{
			Remote:  net.JoinHostPort(ip.String(), strconv.Itoa(port)),
			Proto:   proto(port),
			Process: name,
		})
	}
	return conns
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
