package main

import (
	"embed"
	"encoding/json"
	"sort"
	"strings"
)

//go:embed schema*.json
var schemaFS embed.FS

// runSchema prints Wayfire's exclusive settings rows in the shape the Hub's
// shared renderer consumes. schema.json owns the Window Manager controls and
// schema_anim.json owns animation durations and curves. Concatenating those
// topical arrays lets the provider add complete settings pages without a Hub
// edit, the same way the sibling providers do. Each row carries its page tag,
// so Window Manager and Animations receive only their own controls.
func runSchema() error {
	rows, err := loadSchemaRows()
	if err != nil {
		return err
	}
	enc := json.NewEncoder(stdout)
	enc.SetIndent("", "  ")
	return enc.Encode(rows)
}

// loadSchemaRows reads every embedded schema*.json array and concatenates them
// in a stable filename order, so the row set is deterministic across builds.
func loadSchemaRows() ([]json.RawMessage, error) {
	entries, err := schemaFS.ReadDir(".")
	if err != nil {
		return nil, err
	}
	names := make([]string, 0, len(entries))
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		n := e.Name()
		if strings.HasPrefix(n, "schema") && strings.HasSuffix(n, ".json") {
			names = append(names, n)
		}
	}
	sort.Strings(names)

	rows := []json.RawMessage{}
	for _, n := range names {
		b, err := schemaFS.ReadFile(n)
		if err != nil {
			return nil, err
		}
		var arr []json.RawMessage
		if err := json.Unmarshal(b, &arr); err != nil {
			return nil, err
		}
		rows = append(rows, arr...)
	}
	return rows, nil
}
