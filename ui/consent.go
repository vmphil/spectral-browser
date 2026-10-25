package ui

import (
	"encoding/json"
	"regexp"
)

type trigger struct {
	URLFilter    string   `json:"url-filter"`
	UnlessTopURL []string `json:"unless-top-url,omitempty"`
}

type rule struct {
	Trigger trigger           `json:"trigger"`
	Action  map[string]string `json:"action"`
}

// ConsentFilter builds a WebKit content-blocker rule list that strips
// cookies from every request unless the top-level page is on an accepted host.
func ConsentFilter(accepted []string) string {
	r := rule{Trigger: trigger{URLFilter: ".*"}, Action: map[string]string{"type": "block-cookies"}}
	for _, a := range accepted {
		for _, h := range SiteGroup(a) {
			r.Trigger.UnlessTopURL = append(r.Trigger.UnlessTopURL,
				`^https?://([^/]*\.)?`+regexp.QuoteMeta(h)+`[:/]`)
		}
	}
	b, _ := json.Marshal([]rule{r})
	return string(b)
}
