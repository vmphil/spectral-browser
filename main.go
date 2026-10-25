// Command spectral-browser is a small privacy-minded GTK/WebKit web browser.
package main

import (
	"flag"
	"fmt"
	"log"
	"os"

	"github.com/philip/spectral-browser/config"
	"github.com/philip/spectral-browser/ui"
)

// Version is the release number, shown by --version.
const Version = "1.3.3"

func main() {
	ghost := flag.Bool("ghost", false, "open the Ghost Panel at startup")
	debug := flag.Bool("debug", os.Getenv("SPECTRAL_DEBUG") != "", "log Ghost Panel traffic to stdout")
	version := flag.Bool("version", false, "print the version and exit")
	flag.Parse()
	if *version {
		fmt.Println("spectral-browser", Version)
		return
	}
	cfg, err := config.Load()
	if err != nil {
		log.Printf("config: %v (using defaults)", err)
	}
	o := ui.Options{Ghost: *ghost, Debug: *debug}
	if flag.NArg() > 0 {
		o.URL = ui.NormalizeURL(flag.Arg(0))
	}
	ui.Run(cfg, o)
}
