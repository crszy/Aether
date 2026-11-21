package main

import (
	"os"
	"path/filepath"
)

func home() string {
	h, err := os.UserHomeDir()
	if err != nil {
		return "."
	}
	return h
}

// VaultDir is the knowledge base every agent reads and writes.
func VaultDir() string {
	if d := os.Getenv("RYOKU_RASHIN_VAULT"); d != "" {
		return d
	}
	data := os.Getenv("XDG_DATA_HOME")
	if data == "" {
		data = filepath.Join(home(), ".local", "share")
	}
	return filepath.Join(data, "ryoku", "rashin")
}

// ConfigPath holds the enabled gate and the port.
func ConfigPath() string {
	cfg := os.Getenv("XDG_CONFIG_HOME")
	if cfg == "" {
		cfg = filepath.Join(home(), ".config")
	}
	return filepath.Join(cfg, "ryoku", "rashin.json")
}

// RuntimeDir holds the pidfile and the setup progress report.
func RuntimeDir() string {
	rt := os.Getenv("XDG_RUNTIME_DIR")
	if rt == "" {
		rt = os.TempDir()
	}
	return filepath.Join(rt, "ryoku-rashin")
}
