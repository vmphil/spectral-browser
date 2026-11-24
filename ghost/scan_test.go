package ghost

import (
	"os"
	"testing"
)

func TestScanSelf(t *testing.T) {
	// Must not panic. On Linux this reads /proc; on FreeBSD it runs
	// sockstat; elsewhere the list is empty.
	_ = Scan(os.Getpid())
	if _, ok := Descendants(os.Getpid())[os.Getpid()]; !ok {
		t.Fatal("self missing")
	}
}

func TestDescendantsFrom(t *testing.T) {
	parent := map[int]int{1: 0, 2: 1, 3: 1, 4: 2}
	names := map[int]string{1: "browser", 2: "net", 3: "web", 4: "gpu"}
	got := descendantsFrom(1, parent, names)
	if len(got) != 4 || got[4] != "gpu" || got[3] != "web" {
		t.Fatalf("%v", got)
	}
	sub := descendantsFrom(2, parent, names)
	if len(sub) != 2 || sub[4] != "gpu" {
		t.Fatalf("subtree %v", sub)
	}
	if _, ok := sub[3]; ok {
		t.Fatal("sibling included")
	}
}

func TestParsePS(t *testing.T) {
	parent, names := parsePS("  10   1 browser\n  11  10 WebKitNetwork\nnot a row\n")
	if parent[11] != 10 || names[10] != "browser" || names[11] != "WebKitNetwork" {
		t.Fatalf("%v %v", parent, names)
	}
}

func TestParseSockAddr(t *testing.T) {
	ip, port, ok := parseSockAddr("93.184.216.34:443")
	if !ok || ip != "93.184.216.34" || port != 443 {
		t.Fatalf("%s %d %v", ip, port, ok)
	}
	ip, port, ok = parseSockAddr("[2001:db8::1%vtnet0]:853")
	if !ok || ip != "2001:db8::1" || port != 853 {
		t.Fatalf("v6 %s %d %v", ip, port, ok)
	}
	if _, _, ok = parseSockAddr("*:*"); ok {
		t.Fatal("wildcard accepted")
	}
}

func TestConnsFromSockstat(t *testing.T) {
	text := "" +
		"USER     COMMAND    PID   FD PROTO  LOCAL ADDRESS         FOREIGN ADDRESS\n" +
		"user     spectral-b 4242  15 tcp4   192.0.2.10:54321      93.184.216.34:443\n" +
		"user     WebKitNetw 4243  22 tcp6   [2001:db8::10]:40000  [2001:db8::1]:80\n" +
		"user     spectral-b 4242  16 tcp4   127.0.0.1:12345       127.0.0.1:8080\n" +
		"user     sshd       99    4  tcp4   *:22                  *:*\n" +
		"user     other      7     3  tcp4   192.0.2.10:1          198.51.100.9:443\n"
	procs := map[int]string{4242: "spectral-browser", 4243: "WebKitNetworkProcess"}
	got := connsFromSockstat(text, procs)
	if len(got) != 2 {
		t.Fatalf("%d conns: %+v", len(got), got)
	}
	if got[0].Remote != "93.184.216.34:443" || got[0].Proto != "HTTPS (TLS)" || got[0].Process != "spectral-browser" {
		t.Fatalf("v4 %+v", got[0])
	}
	if got[1].Remote != "[2001:db8::1]:80" || got[1].Proto != "HTTP" || got[1].Process != "WebKitNetworkProcess" {
		t.Fatalf("v6 %+v", got[1])
	}
}
