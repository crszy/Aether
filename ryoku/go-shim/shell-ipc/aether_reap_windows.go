package main

// Windows (Aether): Linux ends the daemon's helpers with it (parent-death signal); Windows does not, so watchers
// (ryoku-wm-hyprland watch ...) of a daemon that was killed kept running. A starting daemon ends every such helper
// whose parent is gone.

import (
	"os"
	"strings"
	"syscall"
	"unsafe"
)

func init() {
	if len(os.Args) > 1 && os.Args[1] == "daemon" {
		aetherReapStrays()
	}
}

func aetherReapStrays() {
	snap, err := syscall.CreateToolhelp32Snapshot(syscall.TH32CS_SNAPPROCESS, 0)
	if err != nil {
		return
	}
	defer syscall.CloseHandle(snap)
	type proc struct {
		pid, parent uint32
		name        string
	}
	var all []proc
	var e syscall.ProcessEntry32
	e.Size = uint32(unsafe.Sizeof(e))
	for err = syscall.Process32First(snap, &e); err == nil; err = syscall.Process32Next(snap, &e) {
		all = append(all, proc{e.ProcessID, e.ParentProcessID, strings.ToLower(syscall.UTF16ToString(e.ExeFile[:]))})
	}
	alive := map[uint32]bool{}
	for _, p := range all {
		alive[p.pid] = true
	}
	for _, p := range all {
		if p.name != "ryoku-wm-hyprland.exe" || alive[p.parent] {
			continue
		}
		if h, err := syscall.OpenProcess(syscall.PROCESS_TERMINATE, false, p.pid); err == nil {
			_ = syscall.TerminateProcess(h, 0)
			syscall.CloseHandle(h)
		}
	}
}

// aetherProcessRunning: a process with this exe name (lower case, eg. "windscribe.exe") is running
func aetherProcessRunning(name string) bool {
	snap, err := syscall.CreateToolhelp32Snapshot(syscall.TH32CS_SNAPPROCESS, 0)
	if err != nil {
		return false
	}
	defer syscall.CloseHandle(snap)
	var e syscall.ProcessEntry32
	e.Size = uint32(unsafe.Sizeof(e))
	for err = syscall.Process32First(snap, &e); err == nil; err = syscall.Process32Next(snap, &e) {
		if strings.ToLower(syscall.UTF16ToString(e.ExeFile[:])) == name {
			return true
		}
	}
	return false
}
