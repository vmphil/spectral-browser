package ghost

import (
	"os"
	"testing"
)

func TestParseAddr(t *testing.T) {
	ip, port, err := parseAddr("0100007F:01BB")
	if err != nil || ip.String() != "127.0.0.1" || port != 443 {
		t.Fatalf("%v %d %v", ip, port, err)
	}
	ip, _, _ = parseAddr("00000000000000000000000001000000:0050")
	if ip.String() != "::1" {
		t.Fatalf("v6 %v", ip)
	}
}

func TestModelCap(t *testing.T) {
	m := NewModel()
	m.AddOut(1, Allowed, "GET", "https://off.example/", "")
	if o, _ := m.Drain(); len(o) != 0 || m.OutN != 0 {
		t.Fatal("recorded while disabled")
	}
	m.SetEnabled(true)
	for i := 0; i < Cap+10; i++ {
		m.AddOut(2, Blocked, "GET", "https://t.example/a.js", "")
	}
	m.AddIn(3, "https://x.example/f.zip", 200, "application/zip", 10)
	o, in := m.Drain()
	if len(o) != Cap || m.BlockedN != Cap+10 || o[0].Type != "script" || o[0].Tab != 2 {
		t.Fatalf("%d %d %q tab %d", len(o), m.BlockedN, o[0].Type, o[0].Tab)
	}
	if len(in) != 1 || in[0].Tab != 3 || in[0].Host != "x.example" {
		t.Fatalf("in %+v", in)
	}
}

func TestDescendantsSelf(t *testing.T) {
	if _, ok := Descendants(os.Getpid())[os.Getpid()]; !ok {
		t.Fatal("self missing")
	}
}

func TestDisableFrees(t *testing.T) {
	m := NewModel()
	m.SetEnabled(true)
	m.AddOut(1, Allowed, "GET", "https://a.example/", "")
	m.SetEnabled(false)
	if o, i := m.Drain(); len(o)+len(i) != 0 || m.OutN != 0 || len(m.RecentHosts()) != 0 {
		t.Fatal("data kept after disable")
	}
}
