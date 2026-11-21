package main

// Aether debugging: RYOKU_STACKS=<file> writes every goroutine's stack to that file 20 s after start and every
// 30 s after, so a daemon that hangs (no Linux SIGQUIT dump on Windows) shows where it waits.

import (
	"os"
	"runtime/pprof"
	"time"
)

func init() {
	path := os.Getenv("RYOKU_STACKS")
	if path == "" {
		return
	}
	go func() {
		time.Sleep(20 * time.Second)
		for {
			if f, err := os.Create(path); err == nil {
				_ = pprof.Lookup("goroutine").WriteTo(f, 2)
				f.Close()
			}
			time.Sleep(30 * time.Second)
		}
	}()
}
