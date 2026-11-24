//go:build freebsd

package ghost

import (
	"log"
	"os/exec"
	"sync"
)

// Descendants returns pid and every process below it.
// FreeBSD has no Linux /proc stat table mounted by default; ps reads the
// same process list as sysctl kern.proc.
func Descendants(pid int) map[int]string {
	b, err := exec.Command("/bin/ps", "-ax", "-o", "pid=,ppid=,comm=").Output()
	if err != nil {
		return map[int]string{pid: ""}
	}
	parent, names := parsePS(string(b))
	return descendantsFrom(pid, parent, names)
}

var sockstatOnce sync.Once

// Scan lists established TCP connections held by pid's process tree.
// sockstat (base system) is the FreeBSD equivalent of reading /proc/net/tcp:
// it prints the owning pid. netstat -p tcp has no pid, so it is not used;
// showing every connection on the machine would leak other programs.
// If sockstat cannot be run, the list is empty and the rest of the panel
// keeps working.
func Scan(pid int) []Conn {
	procs := Descendants(pid)
	b, err := exec.Command("/usr/bin/sockstat", "-c", "-P", "tcp").Output()
	if err != nil {
		sockstatOnce.Do(func() {
			log.Printf("ghost: sockstat failed (%v); active connections will stay empty", err)
		})
		return nil
	}
	return connsFromSockstat(string(b), procs)
}
