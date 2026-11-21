package unix

// AetherListen("unix", path): Ryoku's daemons serve their socket two ways on Windows - the AF_UNIX socket at path
// (Windows has them; Ryoku's own Go clients dial it) and the named pipe \\.\pipe\<path>, which is what Quickshell's
// Socket {} connects to on Windows (QLocalSocket). Both feed one Accept.

import (
	"errors"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"
)

var (
	kernel32             = syscall.NewLazyDLL("kernel32.dll")
	procCreateNamedPipeW = kernel32.NewProc("CreateNamedPipeW")
	procConnectNamedPipe = kernel32.NewProc("ConnectNamedPipe")
	procCreateEventW     = kernel32.NewProc("CreateEventW")
	procGetOverlapped    = kernel32.NewProc("GetOverlappedResult")
	procCancelIoEx       = kernel32.NewProc("CancelIoEx")
)

const (
	pipeAccessDuplex     = 0x3
	fileFlagOverlapped   = 0x40000000
	pipeTypeByte         = 0x0
	pipeReadmodeByte     = 0x0
	pipeWait             = 0x0
	pipeRejectRemote     = 0x8
	pipeUnlimited        = 255
	errorPipeConnected   = 535
	errorIOPending       = 997
	invalidHandle        = ^uintptr(0)
)

// PipeName is the named pipe Quickshell (Qt's QLocalSocket) opens for a socket path
// (a pipe name cannot hold a backslash: Go's filepath.Join makes them, Quickshell's QML paths use "/")
func PipeName(path string) string { return `\\.\pipe\` + strings.ReplaceAll(path, `\`, "/") }

type pipeAddr string

func (a pipeAddr) Network() string { return "pipe" }
func (a pipeAddr) String() string  { return string(a) }

type pipeConn struct {
	*os.File
	name string
}

func (c *pipeConn) LocalAddr() net.Addr  { return pipeAddr(c.name) }
func (c *pipeConn) RemoteAddr() net.Addr { return pipeAddr(c.name) }

type overlapped struct {
	Internal     uintptr
	InternalHigh uintptr
	Offset       uint32
	OffsetHigh   uint32
	HEvent       syscall.Handle
}

type dualListener struct {
	unix   net.Listener
	path   string
	conns  chan net.Conn
	errs   chan error
	done   chan struct{}
	once   sync.Once
	mu     sync.Mutex
	waitOn syscall.Handle
}

func AetherListen(network, path string) (net.Listener, error) {
	if network != "unix" {
		return net.Listen(network, path)
	}
	_ = os.MkdirAll(filepath.Dir(path), 0o700)
	l := &dualListener{path: path, conns: make(chan net.Conn), errs: make(chan error, 1), done: make(chan struct{})}
	u, err := net.Listen("unix", path)
	if err == nil {
		l.unix = u
		go l.acceptUnix()
	}
	// the pipe is the one Quickshell needs: its failure fails the listen
	first, perr := l.newPipe(true)
	if perr != nil {
		if u != nil {
			u.Close()
		}
		return nil, perr
	}
	go l.acceptPipes(first)
	return l, nil
}

func (l *dualListener) acceptUnix() {
	for {
		c, err := l.unix.Accept()
		if err != nil {
			select {
			case <-l.done:
			case l.errs <- err:
			}
			return
		}
		select {
		case l.conns <- c:
		case <-l.done:
			c.Close()
			return
		}
	}
}

func (l *dualListener) newPipe(first bool) (syscall.Handle, error) {
	name, err := syscall.UTF16PtrFromString(PipeName(l.path))
	if err != nil {
		return 0, err
	}
	mode := uintptr(pipeAccessDuplex | fileFlagOverlapped)
	if first {
		mode |= 0x80000 // FILE_FLAG_FIRST_PIPE_INSTANCE: a second daemon fails like a busy socket would
	}
	h, _, e := procCreateNamedPipeW.Call(uintptr(unsafe.Pointer(name)), mode,
		pipeTypeByte|pipeReadmodeByte|pipeWait|pipeRejectRemote, pipeUnlimited, 65536, 65536, 0, 0)
	if h == invalidHandle {
		if e == syscall.ERROR_ACCESS_DENIED {
			return 0, syscall.EADDRINUSE
		}
		return 0, e
	}
	return syscall.Handle(h), nil
}

// waits for a client on h (overlapped, so Close can cancel it)
func (l *dualListener) connect(h syscall.Handle) error {
	ev, _, e := procCreateEventW.Call(0, 1, 0, 0)
	if ev == 0 {
		return e
	}
	defer syscall.CloseHandle(syscall.Handle(ev))
	ov := &overlapped{HEvent: syscall.Handle(ev)}
	r, _, e := procConnectNamedPipe.Call(uintptr(h), uintptr(unsafe.Pointer(ov)))
	if r != 0 {
		return nil
	}
	switch e.(syscall.Errno) {
	case errorPipeConnected:
		return nil
	case errorIOPending:
		var n uint32
		r, _, e = procGetOverlapped.Call(uintptr(h), uintptr(unsafe.Pointer(ov)), uintptr(unsafe.Pointer(&n)), 1)
		if r == 0 {
			return e
		}
		return nil
	}
	return e
}

func (l *dualListener) acceptPipes(h syscall.Handle) {
	for {
		l.mu.Lock()
		l.waitOn = h
		l.mu.Unlock()
		err := l.connect(h)
		select {
		case <-l.done:
			syscall.CloseHandle(h)
			return
		default:
		}
		if err != nil {
			syscall.CloseHandle(h)
			time.Sleep(50 * time.Millisecond)
		} else {
			c := &pipeConn{File: os.NewFile(uintptr(h), PipeName(l.path)), name: PipeName(l.path)}
			select {
			case l.conns <- c:
			case <-l.done:
				c.Close()
				return
			}
		}
		next, err := l.newPipe(false)
		if err != nil {
			select {
			case <-l.done:
			case l.errs <- err:
			}
			return
		}
		h = next
	}
}

func (l *dualListener) Accept() (net.Conn, error) {
	select {
	case c := <-l.conns:
		return c, nil
	case err := <-l.errs:
		return nil, err
	case <-l.done:
		return nil, net.ErrClosed
	}
}

func (l *dualListener) Close() error {
	l.once.Do(func() {
		close(l.done)
		if l.unix != nil {
			l.unix.Close()
		}
		l.mu.Lock()
		if l.waitOn != 0 {
			procCancelIoEx.Call(uintptr(l.waitOn), 0)
		}
		l.mu.Unlock()
	})
	return nil
}

func (l *dualListener) Addr() net.Addr {
	if l.unix != nil {
		return l.unix.Addr()
	}
	return pipeAddr(PipeName(l.path))
}

var _ = errors.New
