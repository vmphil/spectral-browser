package ui

/*
#include <stdlib.h>
#include "browser.h"
*/
import "C"

import (
	"errors"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"unicode"
	"unicode/utf8"
)

// maxName keeps file names well under the 255-byte limit of most filesystems.
const maxName = 200

// SafeName turns a server- or page-suggested file name into a plain base
// name: no directories, control characters or leading dots, never empty.
func SafeName(s string) string {
	s = strings.ReplaceAll(s, "\\", "/")
	if i := strings.LastIndex(s, "/"); i >= 0 {
		s = s[i+1:]
	}
	s = strings.Map(func(r rune) rune {
		if r == utf8.RuneError || unicode.IsControl(r) {
			return -1
		}
		return r
	}, s)
	s = strings.TrimLeft(strings.TrimSpace(s), ".")
	s = strings.TrimSpace(s)
	if s == "" {
		return "download"
	}
	if len(s) > maxName {
		stem, ext := splitExt(s)
		if len(ext) > 20 {
			stem, ext = s, ""
		}
		for len(stem)+len(ext) > maxName {
			_, n := utf8.DecodeLastRuneInString(stem)
			stem = stem[:len(stem)-n]
		}
		s = stem + ext
	}
	return s
}

// splitExt splits "a.tar.gz" into "a" and ".tar.gz", "b.png" into "b", ".png".
func splitExt(name string) (string, string) {
	lower := strings.ToLower(name)
	for _, c := range []string{".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst"} {
		if strings.HasSuffix(lower, c) && len(name) > len(c) {
			return name[:len(name)-len(c)], name[len(name)-len(c):]
		}
	}
	ext := filepath.Ext(name)
	if ext == name {
		ext = ""
	}
	return name[:len(name)-len(ext)], ext
}

// UniquePath returns dir/name, or dir/"name (n).ext" with the first free n,
// so an existing file is never overwritten.
func UniquePath(dir, name string) string {
	name = SafeName(name)
	p := filepath.Join(dir, name)
	if !exists(p) {
		return p
	}
	stem, ext := splitExt(name)
	for n := 1; ; n++ {
		p = filepath.Join(dir, stem+" ("+strconv.Itoa(n)+")"+ext)
		if !exists(p) {
			return p
		}
	}
}

func exists(p string) bool {
	_, err := os.Lstat(p)
	return !errors.Is(err, fs.ErrNotExist)
}

func xdgDownloadDir() string {
	if d := C.GoString(C.sb_xdg_download_dir()); d != "" {
		return d
	}
	return filepath.Join(os.Getenv("HOME"), "Downloads")
}

// DownloadDir is the configured download folder (default: the XDG Downloads
// folder), created if missing.
func DownloadDir() string {
	d := cfg.DownloadDir
	if d == "" {
		d = xdgDownloadDir()
	}
	if err := os.MkdirAll(d, 0o755); err != nil {
		log.Printf("download folder %s: %v; using home", d, err)
		return os.Getenv("HOME")
	}
	return d
}

//export goSafeName
func goSafeName(s *C.char) *C.char { return cstr(SafeName(C.GoString(s))) }

//export goUniqueDownloadPath
func goUniqueDownloadPath(name *C.char) *C.char {
	return cstr(UniquePath(DownloadDir(), C.GoString(name)))
}

//export goDownloadFolder
func goDownloadFolder() *C.char { return cstr(DownloadDir()) }

//export goAskDownload
func goAskDownload() C.int { return cbool(cfg.AskDownload) }

//export goHumanSize
func goHumanSize(n C.longlong) *C.char {
	if n <= 0 {
		return cstr("0 B")
	}
	return cstr(size(int64(n)))
}

//export goDownloadEvent
func goDownloadEvent(kind *C.char, tab C.int, u, detail *C.char) {
	if opts.Debug {
		log.Printf("[download] %s %s %s %s", C.GoString(kind), tabTag(int(tab)), C.GoString(u), C.GoString(detail))
	}
}
