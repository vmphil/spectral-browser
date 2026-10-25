// Package blocklist bundles the tracker host list used by the web extension.
package blocklist

import (
	"bytes"
	_ "embed"
	"os"
	"path/filepath"
)

//go:embed trackers.txt
var data []byte

// Install writes the list to dir/blocklist.txt (if changed) and returns the path.
func Install(dir string) (string, error) {
	p := filepath.Join(dir, "blocklist.txt")
	if old, err := os.ReadFile(p); err == nil && bytes.Equal(old, data) {
		return p, nil
	}
	return p, os.WriteFile(p, data, 0o600)
}
