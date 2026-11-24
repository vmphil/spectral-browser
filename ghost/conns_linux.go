//go:build linux

package ghost

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Descendants returns pid and every process below it, from /proc.
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
	return descendantsFrom(pid, parent, names)
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
// Linux reads socket inodes from /proc/<pid>/fd and matches them to
// /proc/<pid>/net/tcp{,6}. There is no netlink or ss/ip dependency.
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
