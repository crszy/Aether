package unix

// statfs / uname / termios for Windows: real disk numbers (GetDiskFreeSpaceExW) and system names; terminal ioctls
// answer ENOSYS (callers fall back to "not a terminal", 80 columns).

import (
	"os"
	"runtime"
	"syscall"
	"unsafe"
)

const (
	TCGETS     = 0x5401
	TCSETS     = 0x5402
	TIOCGWINSZ = 0x5413
)

type Termios struct {
	Iflag, Oflag, Cflag, Lflag uint32
	Line                       uint8
	Cc                         [19]uint8
	Ispeed, Ospeed             uint32
}

type Winsize struct {
	Row, Col, Xpixel, Ypixel uint16
}

type Statfs_t struct {
	Type    int64
	Bsize   int64
	Blocks  uint64
	Bfree   uint64
	Bavail  uint64
	Files   uint64
	Ffree   uint64
	Namelen int64
	Frsize  int64
	Flags   int64
}

var procGetDiskFreeSpaceExW = kernel32.NewProc("GetDiskFreeSpaceExW")

func Statfs(path string, st *Statfs_t) error {
	p, err := syscall.UTF16PtrFromString(path)
	if err != nil {
		return EINVAL
	}
	var avail, total, free uint64
	r, _, e := procGetDiskFreeSpaceExW.Call(uintptr(unsafe.Pointer(p)), uintptr(unsafe.Pointer(&avail)),
		uintptr(unsafe.Pointer(&total)), uintptr(unsafe.Pointer(&free)))
	if r == 0 {
		return e
	}
	const bs = 4096
	*st = Statfs_t{Bsize: bs, Frsize: bs, Blocks: total / bs, Bfree: free / bs, Bavail: avail / bs, Namelen: 255}
	return nil
}

type Utsname struct {
	Sysname    [65]int8
	Nodename   [65]int8
	Release    [65]int8
	Version    [65]int8
	Machine    [65]int8
	Domainname [65]int8
}

func put(dst *[65]int8, s string) {
	for i := 0; i < len(s) && i < 64; i++ {
		dst[i] = int8(s[i])
	}
}

func Uname(u *Utsname) error {
	*u = Utsname{}
	host, _ := os.Hostname()
	put(&u.Sysname, "Windows")
	put(&u.Nodename, host)
	v, _ := syscall.GetVersion()
	put(&u.Release, itoa(int(byte(v)))+"."+itoa(int(uint8(v>>8)))+"."+itoa(int(v>>16)))
	put(&u.Version, "Windows")
	m := runtime.GOARCH
	if m == "amd64" {
		m = "x86_64"
	}
	put(&u.Machine, m)
	return nil
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b [20]byte
	i := len(b)
	for n > 0 {
		i--
		b[i] = byte('0' + n%10)
		n /= 10
	}
	return string(b[i:])
}

func Syscall6(trap, a1, a2, a3, a4, a5, a6 uintptr) (r1, r2 uintptr, err syscall.Errno) { return 0, 0, ENOSYS }

const (
	FS_IOC_GETFLAGS = 0x80086601
	FS_IOC_SETFLAGS = 0x40086602
	SOL_SOCKET      = 0x1
	SO_PEERCRED     = 0x11
)

// peer credentials of a Unix socket: not on Windows
func GetsockoptUcred(fd, level, opt int) (*Ucred, error) { return nil, ENOSYS }

// pseudo-terminals and descriptor duplication (Ryoku's Linux package updater): not on Windows
const (
	TIOCSPTLCK = 0x40045431
	TIOCGPTN   = 0x80045430
	TIOCSWINSZ = 0x5414
	TIOCSCTTY  = 0x540e
	TIOCNOTTY  = 0x5422
)

func Dup(fd int) (int, error)                 { return -1, ENOSYS }
func Dup2(oldfd, newfd int) error             { return ENOSYS }
func Dup3(oldfd, newfd, flags int) error      { return ENOSYS }
