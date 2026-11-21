package main

// Aether (Windows): Hyprland's IPC sockets are named pipes here. Aether's window manager (the komorebi fork)
// serves them as \\.\pipe\<%TEMP%>\hypr\<signature>\.socket.sock / .socket2.sock, while this program looked for
// unix sockets under $XDG_RUNTIME_DIR/hypr (Ryoku's run folder) - so it never reached the window manager and
// Ryoku's bar showed no workspaces (2026-11-15). Any ".../hypr/<signature>/<socket>" path is dialled as that pipe.
// ryoku-go-port.js rewrites this package's net.Dial("unix", ...) calls to these.

import (
	"errors"
	"io"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"
)

var (
	kernel32          = syscall.NewLazyDLL("kernel32.dll")
	procWaitNamedPipe = kernel32.NewProc("WaitNamedPipeW")
)

// pipePaths: the pipe names a socket path stands for (Aether's %TEMP% layout first)
func pipePaths(path string) []string {
	clean := strings.ReplaceAll(path, "/", `\`)
	parts := strings.Split(clean, `\`)
	var out []string
	if len(parts) >= 3 && parts[len(parts)-3] == "hypr" {
		sig, sock := parts[len(parts)-2], parts[len(parts)-1]
		tmp := os.TempDir()
		out = append(out, `\\.\pipe\`+filepath.Join(tmp, "hypr", sig, sock))
		out = append(out, `\\.\pipe\`+strings.ReplaceAll(filepath.Join(tmp, "hypr", sig, sock), `\`, "/"))
	}
	out = append(out, `\\.\pipe\`+clean, `\\.\pipe\`+strings.ReplaceAll(path, `\`, "/"))
	return out
}

type pipeClient struct {
	h    syscall.Handle
	name string
}

func (c *pipeClient) Read(b []byte) (int, error) {
	var n uint32
	err := syscall.ReadFile(c.h, b, &n, nil)
	if err != nil {
		// the server ends a reply by closing (BROKEN_PIPE) or by DisconnectNamedPipe (PIPE_NOT_CONNECTED 233,
		// NO_DATA 232): all of them are the end of the stream, not a failure
		if errno, ok := err.(syscall.Errno); ok && (errno == 233 || errno == 232) {
			return int(n), io.EOF
		}
		if errors.Is(err, syscall.ERROR_BROKEN_PIPE) {
			return int(n), io.EOF
		}
		return int(n), err
	}
	if n == 0 && len(b) > 0 {
		return 0, io.EOF
	}
	return int(n), nil
}

func (c *pipeClient) Write(b []byte) (int, error) {
	var n uint32
	err := syscall.WriteFile(c.h, b, &n, nil)
	return int(n), err
}

func (c *pipeClient) Close() error                       { return syscall.CloseHandle(c.h) }
func (c *pipeClient) LocalAddr() net.Addr                { return pipeAddr(c.name) }
func (c *pipeClient) RemoteAddr() net.Addr               { return pipeAddr(c.name) }
func (c *pipeClient) SetDeadline(t time.Time) error      { return nil }
func (c *pipeClient) SetReadDeadline(t time.Time) error  { return nil }
func (c *pipeClient) SetWriteDeadline(t time.Time) error { return nil }

type pipeAddr string

func (a pipeAddr) Network() string { return "pipe" }
func (a pipeAddr) String() string  { return string(a) }

func openPipe(name string, timeout time.Duration) (net.Conn, error) {
	p, err := syscall.UTF16PtrFromString(name)
	if err != nil {
		return nil, err
	}
	deadline := time.Now().Add(timeout)
	for {
		h, err := syscall.CreateFile(p, syscall.GENERIC_READ|syscall.GENERIC_WRITE, 0, nil, syscall.OPEN_EXISTING, 0, 0)
		if err == nil {
			return &pipeClient{h: h, name: name}, nil
		}
		// ERROR_PIPE_BUSY (231): every instance is taken - wait for one
		if errno, ok := err.(syscall.Errno); ok && errno == 231 && time.Now().Before(deadline) {
			ms := uint32(time.Until(deadline) / time.Millisecond)
			procWaitNamedPipe.Call(uintptr(unsafe.Pointer(p)), uintptr(ms+1))
			continue
		}
		return nil, err
	}
}

func aetherHyprDialTimeout(path string, timeout time.Duration) (net.Conn, error) {
	var last error = errors.New("no pipe for " + path)
	for _, name := range pipePaths(path) {
		c, err := openPipe(name, timeout)
		if err == nil {
			return &namedConn{Conn: c}, nil
		}
		last = err
	}
	return nil, last
}

func aetherHyprDial(path string) (net.Conn, error) { return aetherHyprDialTimeout(path, 2*time.Second) }

// aetherHyprStat: a socket "file" exists when its pipe does (pipes have no modification time: now)
func aetherHyprStat(path string) (os.FileInfo, error) {
	if fi, err := os.Stat(path); err == nil {
		return fi, nil
	}
	c, err := aetherHyprDialTimeout(path, 100*time.Millisecond)
	if err != nil {
		return nil, err
	}
	_ = c.Close()
	return pipeInfo(filepath.Base(path)), nil
}

type pipeInfo string

func (p pipeInfo) Name() string       { return string(p) }
func (p pipeInfo) Size() int64        { return 0 }
func (p pipeInfo) Mode() os.FileMode  { return os.ModeNamedPipe }
func (p pipeInfo) ModTime() time.Time { return time.Now() }
func (p pipeInfo) IsDir() bool        { return false }
func (p pipeInfo) Sys() any           { return nil }

// ---- output names ----
// The window manager names monitors after their GDI device (DISPLAY5); Quickshell, and so Ryoku's screens, after
// the monitor (CR240CM, the friendly name Windows' display configuration gives). Ryoku matches workspaces to
// screens by that name, so the pipe translates: DISPLAYn -> monitor name on the way in, back on the way out.

var (
	user32                         = syscall.NewLazyDLL("user32.dll")
	procGetDisplayConfigBufferSize = user32.NewProc("GetDisplayConfigBufferSizes")
	procQueryDisplayConfig         = user32.NewProc("QueryDisplayConfig")
	procDisplayConfigGetDeviceInfo = user32.NewProc("DisplayConfigGetDeviceInfo")
	gdiName                        = regexp.MustCompile(`\bDISPLAY\d+\b`)
)

type luid struct{ Low, High uint32 }

type pathInfo struct {
	SourceAdapter luid
	SourceID      uint32
	SourceMode    uint32
	SourceStatus  uint32
	TargetAdapter luid
	TargetID      uint32
	_             [40]byte // the rest of DISPLAYCONFIG_PATH_TARGET_INFO and the path flags (72 bytes in all)
}

type deviceHeader struct {
	Type, Size uint32
	Adapter    luid
	ID         uint32
}

type sourceName struct {
	Header deviceHeader
	Gdi    [32]uint16
}

type targetName struct {
	Header     deviceHeader
	Flags      uint32
	Tech       uint32
	Manuf      uint16
	Product    uint16
	Connector  uint32
	Friendly   [64]uint16
	DevicePath [128]uint16
}

// displayNames: "DISPLAY5" -> "CR240CM" for every active monitor (empty when Windows does not say)
func displayNames() map[string]string {
	names := map[string]string{}
	var nPaths, nModes uint32
	if r, _, _ := procGetDisplayConfigBufferSize.Call(2, uintptr(unsafe.Pointer(&nPaths)), uintptr(unsafe.Pointer(&nModes))); r != 0 || nPaths == 0 {
		return names
	}
	paths := make([]pathInfo, nPaths)
	modes := make([]byte, int(nModes)*64+64)
	if r, _, _ := procQueryDisplayConfig.Call(2, uintptr(unsafe.Pointer(&nPaths)), uintptr(unsafe.Pointer(&paths[0])),
		uintptr(unsafe.Pointer(&nModes)), uintptr(unsafe.Pointer(&modes[0])), 0); r != 0 {
		return names
	}
	for _, p := range paths[:nPaths] {
		src := sourceName{Header: deviceHeader{Type: 1, Size: uint32(unsafe.Sizeof(sourceName{})), Adapter: p.SourceAdapter, ID: p.SourceID}}
		if r, _, _ := procDisplayConfigGetDeviceInfo.Call(uintptr(unsafe.Pointer(&src))); r != 0 {
			continue
		}
		tgt := targetName{Header: deviceHeader{Type: 2, Size: uint32(unsafe.Sizeof(targetName{})), Adapter: p.TargetAdapter, ID: p.TargetID}}
		if r, _, _ := procDisplayConfigGetDeviceInfo.Call(uintptr(unsafe.Pointer(&tgt))); r != 0 {
			continue
		}
		gdi := strings.TrimLeft(syscall.UTF16ToString(src.Gdi[:]), `\.`) // "\\.\DISPLAY5" -> "DISPLAY5"
		friendly := syscall.UTF16ToString(tgt.Friendly[:])
		if gdi != "" && friendly != "" {
			names[gdi] = friendly
		}
	}
	return names
}

var (
	namesMu  sync.Mutex
	namesMap map[string]string
	namesAt  time.Time
)

func outputNames() map[string]string {
	namesMu.Lock()
	defer namesMu.Unlock()
	if namesMap == nil || time.Since(namesAt) > 10*time.Second {
		namesMap, namesAt = displayNames(), time.Now()
	}
	return namesMap
}

func toFriendly(b []byte) []byte {
	names := outputNames()
	if len(names) == 0 {
		return b
	}
	return gdiName.ReplaceAllFunc(b, func(m []byte) []byte {
		if f, ok := names[string(m)]; ok {
			return []byte(f)
		}
		return m
	})
}

func toGdi(b []byte) []byte {
	for gdi, friendly := range outputNames() {
		re := regexp.MustCompile(`\b` + regexp.QuoteMeta(friendly) + `\b`)
		b = re.ReplaceAll(b, []byte(gdi))
	}
	return b
}

// namedConn: the window manager's pipe with output names translated both ways
type namedConn struct {
	net.Conn
	pending []byte // translated bytes that did not fit the caller's buffer yet
	err     error
}

func (c *namedConn) Read(b []byte) (int, error) {
	if len(c.pending) == 0 {
		if c.err != nil {
			return 0, c.err
		}
		raw := make([]byte, len(b))
		n, err := c.Conn.Read(raw)
		c.pending, c.err = toFriendly(raw[:n]), err
		if len(c.pending) == 0 {
			err, c.err = c.err, nil
			return 0, err
		}
	}
	n := copy(b, c.pending)
	c.pending = c.pending[n:]
	if len(c.pending) == 0 && c.err != nil {
		err := c.err
		c.err = nil
		return n, err
	}
	return n, nil
}

func (c *namedConn) Write(b []byte) (int, error) {
	if _, err := c.Conn.Write(toGdi(b)); err != nil {
		return 0, err
	}
	return len(b), nil
}
