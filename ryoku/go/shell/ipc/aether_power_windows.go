package main

// Windows (Aether): Ryoku's "powerprofiles" topic from Windows power plans (powercfg) instead of
// power-profiles-daemon. power-saver / balanced map to Windows' plans of those names; performance is the last
// performance-type plan that was active (the user's own, e.g. a custom "Ultra Performance"), else High
// performance. Unlike Linux, nothing is seeded or restored at start: the plan only changes when the user picks one.

import (
	"encoding/json"
	"fmt"
	"os/exec"
	"regexp"
	"strings"
	"sync"
	"syscall"
	"time"
)

type aetherPlan struct{ guid, name string }

type aetherPower struct {
	topic    *stateTopic
	mu       sync.Mutex
	perfGuid string // the performance-type plan to go back to
	last     []byte
}

var planLine = regexp.MustCompile(`([0-9a-fA-F-]{36})\s+\((.*)\)(\s*\*)?`)

func powercfg(args ...string) string {
	cmd := exec.Command("powercfg", args...)
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: 0x08000000}
	out, _ := cmd.Output()
	return string(out)
}

func plans() (all []aetherPlan, active string) {
	seen := map[string]bool{}
	for _, m := range planLine.FindAllStringSubmatch(powercfg("-list"), -1) {
		g := strings.ToLower(m[1])
		if m[3] != "" {
			active = g
		}
		if !seen[g] {
			seen[g] = true
			all = append(all, aetherPlan{g, m[2]})
		}
	}
	return
}

func kindOf(name string) string {
	n := strings.ToLower(name)
	switch {
	case strings.Contains(n, "saver") || strings.Contains(n, "eco"):
		return "power-saver"
	case n == "balanced":
		return "balanced"
	}
	return "performance"
}

func (p *aetherPower) frame() powerProfilesFrame {
	all, active := plans()
	cur := "balanced"
	for _, pl := range all {
		if pl.guid == active {
			cur = kindOf(pl.name)
			if cur == "performance" {
				p.mu.Lock()
				p.perfGuid = pl.guid
				p.mu.Unlock()
			}
		}
	}
	return powerProfilesFrame{ActiveProfile: cur, Profiles: []string{"power-saver", "balanced", "performance"}}
}

func (p *aetherPower) publish() {
	b, err := json.Marshal(p.frame())
	if err != nil {
		return
	}
	p.mu.Lock()
	same := string(b) == string(p.last)
	p.last = b
	p.mu.Unlock()
	if !same && p.topic != nil {
		p.topic.publish(b)
	}
}

func (p *aetherPower) set(profile string) error {
	all, _ := plans()
	target := ""
	switch profile {
	case "performance":
		p.mu.Lock()
		target = p.perfGuid
		p.mu.Unlock()
		if target == "" {
			for _, pl := range all {
				if strings.EqualFold(pl.name, "High performance") {
					target = pl.guid
				}
			}
		}
	default:
		for _, pl := range all {
			if kindOf(pl.name) == profile {
				target = pl.guid
				break
			}
		}
	}
	if target == "" {
		return fmt.Errorf("no Windows power plan for %q", profile)
	}
	powercfg("-setactive", target)
	p.publish()
	return nil
}

func aetherStartPowerProfiles(d *daemon) bool {
	p := &aetherPower{topic: d.registerTopic("powerprofiles")}
	d.registerCall("powerprofiles.setProfile", func(raw json.RawMessage) (any, error) {
		var a struct {
			Profile string `json:"profile"`
		}
		if err := json.Unmarshal(raw, &a); err != nil {
			return nil, err
		}
		return nil, p.set(a.Profile)
	})
	d.registerCall("powerprofiles.state", func(json.RawMessage) (any, error) { return p.frame(), nil })
	go func() {
		for {
			p.publish()
			time.Sleep(10 * time.Second) // a plan changed in Windows' own settings shows up here
		}
	}()
	return true
}
