//go:build !linux && !freebsd

package ghost

// Descendants reports only pid. Other systems have no /proc scanner and no
// sockstat; the Ghost Panel's connection list stays empty.
func Descendants(pid int) map[int]string {
	return map[int]string{pid: ""}
}

// Scan has no connection table to read on this OS.
func Scan(pid int) []Conn { return nil }
