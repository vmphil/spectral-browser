// Command genblocklist converts EasyPrivacy/EasyList plain domain rules
// ("||host^" with simple type options) into a sorted host list.
//
//	go run ./cmd/genblocklist < easyprivacy.txt > blocklist/trackers.txt
package main

import (
	"bufio"
	"fmt"
	"os"
	"regexp"
	"sort"
)

// Whole-host rules, optionally limited to request types the extension can
// block anyway. Rules scoped with domain= or popup are skipped.
var rule = regexp.MustCompile(`^\|\|([a-z0-9.-]+\.[a-z]{2,})\^(\$((third-party|script|xmlhttprequest|image|ping|subdocument|important|all|other|media),?)+)?$`)

func main() {
	seen := map[string]bool{}
	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	for sc.Scan() {
		if m := rule.FindStringSubmatch(sc.Text()); m != nil {
			seen[m[1]] = true
		}
	}
	if err := sc.Err(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	hosts := make([]string, 0, len(seen))
	for h := range seen {
		hosts = append(hosts, h)
	}
	sort.Strings(hosts)
	w := bufio.NewWriter(os.Stdout)
	fmt.Fprintln(w, "# Generated from EasyPrivacy + EasyList (https://easylist.to), GPLv3 / CC BY-SA 3.0.")
	for _, h := range hosts {
		fmt.Fprintln(w, h)
	}
	w.Flush()
}
