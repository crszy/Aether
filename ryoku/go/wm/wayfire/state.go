package main

import (
	"encoding/json"
	"fmt"
	"os/exec"
	"sort"

	wm "ryoku-wm"
)

// runState is one snapshot for callers that ask once and exit. It queries
// rather than opening the stream, so a one-shot caller pays one round trip
// per list instead of waiting for a replay to finish.
func runState() error {
	s, err := readStage()
	if err != nil {
		return err
	}
	current, all := s.keyboard.keyboardPair()
	outputs := s.outputsFrame()
	if detailed, err := readOutputDetails(); err == nil {
		outputs = enrichOutputs(outputs, detailed)
	}
	snap := wm.Snapshot{
		FocusedOutput:   s.focused,
		Outputs:         outputs,
		Workspaces:      s.workspaceFrame(),
		Windows:         s.windowFrame(),
		KeyboardLayout:  current,
		KeyboardLayouts: all,
	}
	enc := json.NewEncoder(stdout)
	enc.SetIndent("", "  ")
	return enc.Encode(snap)
}

var runWlrRandr = func() ([]byte, error) {
	return exec.Command("wlr-randr", "--json").Output()
}

type randrMode struct {
	Width     int     `json:"width"`
	Height    int     `json:"height"`
	Refresh   float64 `json:"refresh"`
	Preferred bool    `json:"preferred"`
	Current   bool    `json:"current"`
}

type randrOutput struct {
	Name         string `json:"name"`
	Make         string `json:"make"`
	Model        string `json:"model"`
	Enabled      bool   `json:"enabled"`
	PhysicalSize struct {
		Width int `json:"width"`
	} `json:"physical_size"`
	Modes    []randrMode `json:"modes"`
	Position struct {
		X int `json:"x"`
		Y int `json:"y"`
	} `json:"position"`
	Transform    string  `json:"transform"`
	Scale        float64 `json:"scale"`
	AdaptiveSync bool    `json:"adaptive_sync"`
}

func readOutputDetails() ([]randrOutput, error) {
	body, err := runWlrRandr()
	if err != nil {
		return nil, err
	}
	var outputs []randrOutput
	if err := json.Unmarshal(body, &outputs); err != nil {
		return nil, fmt.Errorf("wlr-randr: bad JSON: %w", err)
	}
	return outputs, nil
}

func enrichOutputs(base []wm.Output, details []randrOutput) []wm.Output {
	byName := make(map[string]int, len(base))
	for i := range base {
		byName[base[i].Name] = i
	}
	for _, detail := range details {
		at, ok := byName[detail.Name]
		if !ok {
			base = append(base, wm.Output{Name: detail.Name})
			at = len(base) - 1
			byName[detail.Name] = at
		}
		out := &base[at]
		out.Make = detail.Make
		out.Model = detail.Model
		out.PhysicalWidth = detail.PhysicalSize.Width
		out.Disabled = !detail.Enabled
		out.X = detail.Position.X
		out.Y = detail.Position.Y
		if detail.Scale > 0 {
			out.Scale = detail.Scale
		}
		out.Transform = randrTransform(detail.Transform)
		out.VRR = detail.AdaptiveSync
		out.Modes = out.Modes[:0]
		for _, mode := range detail.Modes {
			value := fmt.Sprintf("%dx%d@%g", mode.Width, mode.Height, mode.Refresh)
			out.Modes = append(out.Modes, value)
			if mode.Current {
				out.Mode = value
				if out.Width == 0 && detail.Scale > 0 {
					out.Width = int(float64(mode.Width) / detail.Scale)
					out.Height = int(float64(mode.Height) / detail.Scale)
				}
			}
		}
	}
	sort.Slice(base, func(i, j int) bool { return base[i].Name < base[j].Name })
	return base
}

func randrTransform(value string) int {
	switch value {
	case "flipped-90":
		value = "90_flipped"
	case "flipped-180":
		value = "180_flipped"
	case "flipped-270":
		value = "270_flipped"
	}
	return transformFromWayfire(value)
}
