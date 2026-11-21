package main

import (
	unix "ryoku-rashin/aetherunix"
	"os"
	"strconv"
	"strings"
	"unsafe"
)

// termstyle.go: stdlib-only terminal styling for the rashin CLI, mirroring
// the ryoku CLI's approach. Colour is Ryoku vermilion plus a few accents,
// emitted only on a real terminal and never under NO_COLOR or --plain, so
// piped output (and the fish buffer payload on stdout) stays plain text.

func brandc(s string) string { return paintC("38;2;242;86;35", s) } // #F25623
func greenc(s string) string { return paintC("38;2;152;195;121", s) }
func amberc(s string) string { return paintC("38;2;229;181;103", s) }
func redc(s string) string   { return paintC("38;2;224;108;117", s) }
func dimc(s string) string   { return paintC("2", s) }

func paintC(code, s string) string { return "\033[" + code + "m" + s + "\033[0m" }

// termPaint applies a colour function unless --plain is set or the
// presentation channel is not a terminal.
func termPaint(o termOpts, fn func(string) string, s string) string {
	if colorOK(o) {
		return fn(s)
	}
	return s
}

func colorOK(o termOpts) bool {
	return !o.plain && os.Getenv("NO_COLOR") == "" && isTTY(actionChannel(o))
}

// tierBadge renders a command's danger tier as a fixed-width coloured badge.
func tierBadge(o termOpts, tier string) string {
	label := map[string]string{
		"read": "read", "write": "write", "system": "system", "danger": "danger",
	}[tier]
	if label == "" {
		label = "write"
	}
	badge := "◇ " + label
	if !colorOK(o) {
		return "[" + label + "]"
	}
	switch tier {
	case "read":
		return greenc(badge)
	case "system":
		return amberc(badge)
	case "danger":
		return redc(badge)
	default:
		return amberc(badge)
	}
}

// isTTY reports whether f is a real terminal (TCGETS succeeds only on a tty,
// and unlike an os.ModeCharDevice check it is not fooled by /dev/null).
func isTTY(f *os.File) bool {
	var t unix.Termios
	_, _, err := unix.Syscall6(unix.SYS_IOCTL, f.Fd(),
		unix.TCGETS, uintptr(unsafe.Pointer(&t)), 0, 0, 0)
	return err == 0
}

// termCols returns the presentation width: COLUMNS, else the TIOCGWINSZ
// ioctl on stderr, clamped to a readable range, 80 as a floor.
func termCols() int {
	if c := os.Getenv("COLUMNS"); c != "" {
		if n, err := strconv.Atoi(c); err == nil {
			return clampCols(n)
		}
	}
	var ws struct{ row, col, x, y uint16 }
	_, _, err := unix.Syscall(unix.SYS_IOCTL, os.Stderr.Fd(),
		unix.TIOCGWINSZ, uintptr(unsafe.Pointer(&ws)))
	if err == 0 && ws.col > 0 {
		return clampCols(int(ws.col))
	}
	return 80
}

func clampCols(n int) int {
	if n < 40 {
		return 40
	}
	if n > 120 {
		return 120
	}
	return n
}

// wrapText word-wraps s to width, indenting each line. Blank lines stay as
// paragraph breaks.
func wrapText(s string, width int, indent string) string {
	width -= len(indent)
	if width < 20 {
		width = 20
	}
	var out []string
	for _, para := range strings.Split(s, "\n") {
		words := strings.Fields(para)
		if len(words) == 0 {
			out = append(out, "")
			continue
		}
		line := indent + words[0]
		cur := len(words[0])
		for _, w := range words[1:] {
			if cur+1+len(w) > width {
				out = append(out, line)
				line = indent + w
				cur = len(w)
			} else {
				line += " " + w
				cur += 1 + len(w)
			}
		}
		out = append(out, line)
	}
	return strings.Join(out, "\n")
}
