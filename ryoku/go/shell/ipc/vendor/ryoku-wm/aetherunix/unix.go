// Package unix is Aether's Windows stand-in for golang.org/x/sys/unix (and the Linux-only parts of syscall) in
// Ryoku's Go programs (dotfiles/ryoku-go-port.js points their imports here).
//
// File calls (Open, Openat, Read, Write, Pread, Close, Unlinkat, Renameat, Stat, Pipe2) do the real thing through
// Go's os package, keeping Linux-style int descriptors in a table. Linux-only kernel features (inotify, netlink,
// eventfd, pidfd, ioctl) answer ENOSYS, so the code around them takes its "not available" path. Poll reports
// files and pipes readable at once and otherwise waits out its timeout.
package unix

import (
	"errors"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"sync"
	"syscall"
	"time"
)

// ---------------------------------------------------------------- errors (Windows' syscall has the same Errno values)
const (
	EINTR   = syscall.EINTR
	EAGAIN  = syscall.EAGAIN
	ENOENT  = syscall.ENOENT
	EEXIST  = syscall.EEXIST
	ESRCH   = syscall.ESRCH
	ENOBUFS = syscall.ENOBUFS
	ENOSYS  = syscall.ENOSYS
	EBADF   = syscall.EBADF
	ENOTDIR = syscall.ENOTDIR
	EINVAL  = syscall.EINVAL
	ELOOP   = syscall.ELOOP
	EISDIR  = syscall.EISDIR
)

// ---------------------------------------------------------------- constants
const (
	O_RDONLY    = 0x0
	O_WRONLY    = 0x1
	O_RDWR      = 0x2
	O_CREAT     = 0x40
	O_EXCL      = 0x80
	O_NOCTTY    = 0x100
	O_TRUNC     = 0x200
	O_APPEND    = 0x400
	O_NONBLOCK  = 0x800
	O_DIRECTORY = 0x10000
	O_NOFOLLOW  = 0x20000
	O_CLOEXEC   = 0x80000
	O_PATH      = 0x200000

	AT_FDCWD     = -100
	AT_REMOVEDIR = 0x200

	POLLIN   = 0x1
	POLLPRI  = 0x2
	POLLOUT  = 0x4
	POLLERR  = 0x8
	POLLHUP  = 0x10
	POLLNVAL = 0x20

	IN_ACCESS        = 0x1
	IN_MODIFY        = 0x2
	IN_ATTRIB        = 0x4
	IN_CLOSE_WRITE   = 0x8
	IN_CLOSE_NOWRITE = 0x10
	IN_OPEN          = 0x20
	IN_MOVED_FROM    = 0x40
	IN_MOVED_TO      = 0x80
	IN_CREATE        = 0x100
	IN_DELETE        = 0x200
	IN_DELETE_SELF   = 0x400
	IN_MOVE_SELF     = 0x800
	IN_UNMOUNT       = 0x2000
	IN_Q_OVERFLOW    = 0x4000
	IN_IGNORED       = 0x8000
	IN_ONLYDIR       = 0x1000000
	IN_ISDIR         = 0x40000000
	IN_NONBLOCK      = 0x800
	IN_CLOEXEC       = 0x80000

	SizeofInotifyEvent = 0x10

	EFD_NONBLOCK  = 0x800
	EFD_CLOEXEC   = 0x80000
	EFD_SEMAPHORE = 0x1

	CLOCK_REALTIME  = 0
	CLOCK_MONOTONIC = 1
	CLOCK_BOOTTIME  = 7

	AF_UNIX                = 0x1
	AF_NETLINK             = 0x10
	SOCK_STREAM            = 0x1
	SOCK_DGRAM             = 0x2
	SOCK_NONBLOCK          = 0x800
	SOCK_CLOEXEC           = 0x80000
	NETLINK_KOBJECT_UEVENT = 0xf

	SYS_IOCTL = 16

	F_GETLK  = 5
	F_SETLK  = 6
	F_SETLKW = 7
	F_RDLCK  = 0
	F_WRLCK  = 1
	F_UNLCK  = 2

	LOCK_SH = 1
	LOCK_EX = 2
	LOCK_NB = 4
	LOCK_UN = 8

	S_IFMT   = 0xf000
	S_IFDIR  = 0x4000
	S_IFREG  = 0x8000
	S_IFLNK  = 0xa000
	S_IFIFO  = 0x1000
	S_IFCHR  = 0x2000
	S_IFSOCK = 0xc000
)

// signals Windows' syscall package does not define
const (
	SIGHUP  = syscall.Signal(0x1)
	SIGINT  = syscall.SIGINT
	SIGKILL = syscall.SIGKILL
	SIGTERM = syscall.SIGTERM
	SIGUSR1 = syscall.Signal(0xa)
	SIGUSR2 = syscall.Signal(0xc)
	SIGCHLD = syscall.Signal(0x11)
	SIGCONT = syscall.Signal(0x12)
	SIGSTOP = syscall.Signal(0x13)
	SIGTSTP = syscall.Signal(0x14)
)

// ---------------------------------------------------------------- types
type Signal = syscall.Signal

type Timespec struct {
	Sec  int64
	Nsec int64
}

func (ts *Timespec) Unix() (sec int64, nsec int64) { return ts.Sec, ts.Nsec }
func (ts *Timespec) Nano() int64                   { return ts.Sec*1e9 + ts.Nsec }
func NsecToTimespec(n int64) Timespec              { return Timespec{Sec: n / 1e9, Nsec: n % 1e9} }

type Stat_t struct {
	Dev     uint64
	Ino     uint64
	Nlink   uint64
	Mode    uint32
	Uid     uint32
	Gid     uint32
	Rdev    uint64
	Size    int64
	Blksize int64
	Blocks  int64
	Atim    Timespec
	Mtim    Timespec
	Ctim    Timespec
}

type PollFd struct {
	Fd      int32
	Events  int16
	Revents int16
}

type Flock_t struct {
	Type   int16
	Whence int16
	Start  int64
	Len    int64
	Pid    int32
}

type Sockaddr interface{}

type SockaddrNetlink struct {
	Family uint16
	Pad    uint16
	Pid    uint32
	Groups uint32
}

type InotifyEvent struct {
	Wd     int32
	Mask   uint32
	Cookie uint32
	Len    uint32
}

type Ucred struct {
	Pid int32
	Uid uint32
	Gid uint32
}

// ---------------------------------------------------------------- the descriptor table
type entry struct {
	file *os.File
	path string
	dir  bool
}

var (
	mu     sync.Mutex
	nextFd = 1000
	fds    = map[int]*entry{}
	start  = time.Now()
)

func add(e *entry) int {
	mu.Lock()
	defer mu.Unlock()
	fd := nextFd
	nextFd++
	fds[fd] = e
	return fd
}

func get(fd int) *entry {
	mu.Lock()
	defer mu.Unlock()
	return fds[fd]
}

func errno(err error) error {
	if err == nil {
		return nil
	}
	switch {
	case errors.Is(err, fs.ErrNotExist):
		return ENOENT
	case errors.Is(err, fs.ErrExist):
		return EEXIST
	case errors.Is(err, fs.ErrPermission):
		return syscall.EACCES
	}
	var e syscall.Errno
	if errors.As(err, &e) {
		return e
	}
	return syscall.EIO
}

func resolve(dirfd int, name string) string {
	if filepath.IsAbs(name) || len(name) > 0 && (name[0] == '/' || name[0] == '\\') || dirfd == AT_FDCWD {
		return name
	}
	if e := get(dirfd); e != nil {
		return filepath.Join(e.path, name)
	}
	return name
}

func Open(path string, mode int, perm uint32) (int, error) { return Openat(AT_FDCWD, path, mode, perm) }

func Openat(dirfd int, name string, mode int, perm uint32) (int, error) {
	p := resolve(dirfd, name)
	if mode&O_DIRECTORY != 0 {
		st, err := os.Stat(p)
		if err != nil {
			return -1, errno(err)
		}
		if !st.IsDir() {
			return -1, ENOTDIR
		}
		return add(&entry{path: p, dir: true}), nil
	}
	flags := 0
	switch mode & 3 {
	case O_WRONLY:
		flags = os.O_WRONLY
	case O_RDWR:
		flags = os.O_RDWR
	default:
		flags = os.O_RDONLY
	}
	if mode&O_CREAT != 0 {
		flags |= os.O_CREATE
	}
	if mode&O_EXCL != 0 {
		flags |= os.O_EXCL
	}
	if mode&O_TRUNC != 0 {
		flags |= os.O_TRUNC
	}
	if mode&O_APPEND != 0 {
		flags |= os.O_APPEND
	}
	f, err := os.OpenFile(p, flags, fs.FileMode(perm&0o777))
	if err != nil {
		return -1, errno(err)
	}
	return add(&entry{file: f, path: p}), nil
}

func Close(fd int) error {
	mu.Lock()
	e := fds[fd]
	delete(fds, fd)
	mu.Unlock()
	if e == nil {
		return EBADF
	}
	if e.file != nil {
		return errno(e.file.Close())
	}
	return nil
}

func CloseOnExec(fd int)                         {}
func SetNonblock(fd int, nonblocking bool) error { return nil }

func Read(fd int, p []byte) (int, error) {
	e := get(fd)
	if e == nil || e.file == nil {
		return -1, EBADF
	}
	n, err := e.file.Read(p)
	if err == io.EOF {
		return n, nil
	}
	return n, errno(err)
}

func Write(fd int, p []byte) (int, error) {
	e := get(fd)
	if e == nil || e.file == nil {
		return -1, EBADF
	}
	n, err := e.file.Write(p)
	return n, errno(err)
}

func Pread(fd int, p []byte, offset int64) (int, error) {
	e := get(fd)
	if e == nil || e.file == nil {
		return -1, EBADF
	}
	n, err := e.file.ReadAt(p, offset)
	if err == io.EOF {
		return n, nil
	}
	return n, errno(err)
}

func Pwrite(fd int, p []byte, offset int64) (int, error) {
	e := get(fd)
	if e == nil || e.file == nil {
		return -1, EBADF
	}
	n, err := e.file.WriteAt(p, offset)
	return n, errno(err)
}

func Fsync(fd int) error {
	if e := get(fd); e != nil && e.file != nil {
		return errno(e.file.Sync())
	}
	return nil
}

func Unlinkat(dirfd int, name string, flags int) error { return errno(os.Remove(resolve(dirfd, name))) }
func Unlink(path string) error                         { return errno(os.Remove(path)) }
func Rmdir(path string) error                          { return errno(os.Remove(path)) }
func Mkdir(path string, mode uint32) error             { return errno(os.Mkdir(path, fs.FileMode(mode&0o777))) }
func Mkdirat(dirfd int, name string, mode uint32) error {
	return errno(os.Mkdir(resolve(dirfd, name), fs.FileMode(mode&0o777)))
}
func Renameat(olddirfd int, oldpath string, newdirfd int, newpath string) error {
	return errno(os.Rename(resolve(olddirfd, oldpath), resolve(newdirfd, newpath)))
}
func Rename(oldpath, newpath string) error { return errno(os.Rename(oldpath, newpath)) }

func fill(st fs.FileInfo, out *Stat_t) {
	*out = Stat_t{Size: st.Size(), Nlink: 1, Blksize: 4096}
	mode := uint32(st.Mode().Perm())
	switch {
	case st.IsDir():
		mode |= S_IFDIR
	case st.Mode()&fs.ModeSymlink != 0:
		mode |= S_IFLNK
	case st.Mode()&fs.ModeNamedPipe != 0:
		mode |= S_IFIFO
	default:
		mode |= S_IFREG
	}
	out.Mode = mode
	out.Blocks = (st.Size() + 511) / 512
	t := NsecToTimespec(st.ModTime().UnixNano())
	out.Mtim, out.Atim, out.Ctim = t, t, t
}

func Stat(path string, st *Stat_t) error {
	info, err := os.Stat(path)
	if err != nil {
		return errno(err)
	}
	fill(info, st)
	return nil
}

func Lstat(path string, st *Stat_t) error {
	info, err := os.Lstat(path)
	if err != nil {
		return errno(err)
	}
	fill(info, st)
	return nil
}

func Fstat(fd int, st *Stat_t) error {
	e := get(fd)
	if e == nil {
		return EBADF
	}
	return Stat(e.path, st)
}

func Fstatat(dirfd int, name string, st *Stat_t, flags int) error { return Stat(resolve(dirfd, name), st) }

func Pipe2(p []int, flags int) error {
	if len(p) != 2 {
		return EINVAL
	}
	r, w, err := os.Pipe()
	if err != nil {
		return errno(err)
	}
	p[0] = add(&entry{file: r, path: "pipe"})
	p[1] = add(&entry{file: w, path: "pipe"})
	return nil
}
func Pipe(p []int) error { return Pipe2(p, 0) }

// Poll: files and pipes count as readable; anything else (inotify, netlink, eventfd - never opened here) waits
func Poll(fds []PollFd, timeout int) (int, error) {
	ready := 0
	for i := range fds {
		fds[i].Revents = 0
		if e := get(int(fds[i].Fd)); e != nil && e.file != nil && fds[i].Events&POLLIN != 0 {
			fds[i].Revents = POLLIN
			ready++
		}
	}
	if ready > 0 {
		return ready, nil
	}
	if timeout < 0 {
		select {} // nothing on Windows ever wakes a Linux kernel watch: this watcher goroutine just stays parked
	}
	time.Sleep(time.Duration(timeout) * time.Millisecond)
	return 0, nil
}

func ClockGettime(clockid int32, ts *Timespec) error {
	if clockid == CLOCK_REALTIME {
		*ts = NsecToTimespec(time.Now().UnixNano())
	} else {
		*ts = NsecToTimespec(int64(time.Since(start)) + int64(time.Hour)) // monotonic, starts well above zero
	}
	return nil
}

// ---------------------------------------------------------------- Linux-only kernel features: not available
func InotifyInit() (int, error)                                     { return -1, ENOSYS }
func InotifyInit1(flags int) (int, error)                           { return -1, ENOSYS }
func InotifyAddWatch(fd int, path string, mask uint32) (int, error) { return -1, ENOSYS }
func InotifyRmWatch(fd int, watchdesc uint32) (int, error)          { return -1, ENOSYS }
func Eventfd(initval uint, flags int) (int, error)                  { return -1, ENOSYS }
func PidfdOpen(pid int, flags int) (int, error)                     { return -1, ENOSYS }
func PidfdSendSignal(pidfd int, sig Signal, info *struct{}, flags int) error {
	return ENOSYS
}
func Socket(domain, typ, proto int) (int, error)                      { return -1, ENOSYS }
func Bind(fd int, sa Sockaddr) error                                   { return ENOSYS }
func Recvfrom(fd int, p []byte, flags int) (int, Sockaddr, error)      { return 0, nil, ENOSYS }
func Sendto(fd int, p []byte, flags int, to Sockaddr) error            { return ENOSYS }
func IoctlSetInt(fd int, req uint, value int) error                    { return ENOSYS }
func IoctlSetPointerInt(fd int, req uint, value int) error             { return ENOSYS }
func IoctlGetInt(fd int, req uint) (int, error)                        { return 0, ENOSYS }
func Syscall(trap, a1, a2, a3 uintptr) (r1, r2 uintptr, err syscall.Errno) { return 0, 0, ENOSYS }
func UnixRights(fds ...int) []byte                                     { return nil }
func ParseUnixRights(m *struct{}) ([]int, error)                       { return nil, ENOSYS }
func CloseWrite(fd int) error                                          { return nil }
func FcntlFlock(fd uintptr, cmd int, lk *Flock_t) error                { return nil }
func Flock(fd int, how int) error                                      { return nil }
func Umask(mask int) int                                               { return 0o022 }
func Getpgrp() int                                                     { return os.Getpid() }
func Getpgid(pid int) (int, error)                                     { return pid, nil }
func Setpgid(pid, pgid int) error                                      { return nil }
func Setsid() (int, error)                                             { return os.Getpid(), nil }
func Getuid() int                                                      { return 1000 }
func Geteuid() int                                                     { return 1000 }
func Getgid() int                                                      { return 1000 }
func Mkfifo(path string, mode uint32) error                            { return ENOSYS }

// Kill(pid, 0) asks whether the process is alive; other signals end it (a negative pid is a group: its leader)
func Kill(pid int, sig Signal) error {
	if pid < 0 {
		pid = -pid
	}
	p, err := os.FindProcess(pid)
	if err != nil {
		return ESRCH
	}
	if sig == 0 {
		return nil
	}
	if err := p.Kill(); err != nil {
		return ESRCH
	}
	return nil
}

// a Linux SysProcAttr (process group, parent-death signal): on Windows the child just gets its own process
func AetherProcAttr() *syscall.SysProcAttr { return &syscall.SysProcAttr{} }
