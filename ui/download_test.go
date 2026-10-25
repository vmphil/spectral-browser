package ui

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSafeName(t *testing.T) {
	for in, want := range map[string]string{
		"photo.jpg":          "photo.jpg",
		"../../etc/passwd":   "passwd",
		`C:\Users\x\a b.txt`: "a b.txt",
		".bashrc":            "bashrc",
		"..":                 "download",
		"":                   "download",
		"a\x00b\nc.txt":      "abc.txt",
		"  spaced.zip  ":     "spaced.zip",
	} {
		if got := SafeName(in); got != want {
			t.Errorf("SafeName(%q) = %q, want %q", in, got, want)
		}
	}
	long := strings.Repeat("é", 300) + ".tar.gz"
	got := SafeName(long)
	if len(got) > maxName || !strings.HasSuffix(got, ".tar.gz") {
		t.Errorf("long name: %d bytes %q", len(got), got[len(got)-10:])
	}
}

func TestUniquePath(t *testing.T) {
	dir := t.TempDir()
	if p := UniquePath(dir, "pic.png"); p != filepath.Join(dir, "pic.png") {
		t.Fatal(p)
	}
	for _, f := range []string{"pic.png", "pic (1).png", "src.tar.gz", "README"} {
		os.WriteFile(filepath.Join(dir, f), nil, 0o600)
	}
	for name, want := range map[string]string{
		"pic.png":    "pic (2).png",
		"src.tar.gz": "src (1).tar.gz",
		"README":     "README (1)",
	} {
		if p := UniquePath(dir, name); p != filepath.Join(dir, want) {
			t.Errorf("%s: got %s", name, filepath.Base(p))
		}
	}
}
