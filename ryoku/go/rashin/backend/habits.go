package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

// habits.go builds the vault's user-habits layer: the real names of this
// user's directories, the tool substitutions in force, shell rhythms mined
// from fish history (counts only, secret-filtered, opt-out), recent rashin
// usage with corrections, and saved recipes. habits.md rides into the quick
// and terminal patterns so the model speaks this user's dialect, and every
// wired agent reads it from the vault for free.

const habitsHeader = "# Habits\n" +
	"\n" +
	"Generated: how this user actually works, mined locally (nothing leaves the\n" +
	"machine). Agents: prefer these names and tools; the content between the\n" +
	"markers is overwritten on every reindex. Disable the history mining with\n" +
	"`habits: {\"history\": false}` in ~/.config/ryoku/rashin.json."

// WriteHabits regenerates habits.md; each section is best effort.
func WriteHabits() error {
	return writeVaultDoc("habits.md", habitsHeader, habitsBody(LoadConfig()))
}

func habitsBody(cfg Config) string {
	var b strings.Builder

	b.WriteString("## Environment\n\n")
	b.WriteString("- Shell: " + userShell() + " · Terminal: kitty (Ryoku default) · Editor: " + editorName() + "\n")
	if dirs := xdgUserDirs(); len(dirs) > 0 {
		var parts []string
		for _, d := range dirs {
			parts = append(parts, d.name+" -> "+d.path)
		}
		b.WriteString("- XDG directories: " + strings.Join(parts, ", ") + "\n")
	}
	if tops := homeTopDirs(); len(tops) > 0 {
		b.WriteString("- Home directories: " + strings.Join(tops, ", ") + "\n")
	}

	b.WriteString("\n## Stack\n\n")
	b.WriteString("- " + strings.Join(stackNotes(), "\n- ") + "\n")

	if cfg.HabitsHistoryEnabled() {
		if rhythms := shellRhythms(); len(rhythms) > 0 {
			b.WriteString("\n## Shell rhythms\n\nTop commands from fish history (counts only):\n\n")
			b.WriteString("- " + strings.Join(rhythms, "\n- ") + "\n")
		}
	}

	if usage := rashinUsage(); usage != "" {
		b.WriteString("\n## Rashin usage\n\n" + usage)
	}

	if rs := LoadRecipes(); len(rs) > 0 {
		b.WriteString("\n## Recipes\n\nSaved shortcuts; suggest the abbreviation instead of re-deriving the pipeline:\n\n")
		for _, r := range rs {
			fmt.Fprintf(&b, "- `rr-%s`: `%s`\n", r.Name, r.Run)
		}
	}
	return b.String()
}

type xdgDir struct{ name, path string }

// xdgUserDirs parses ~/.config/user-dirs.dirs, the authority on what this
// user's Pictures/Documents/... are really called (localized names included).
func xdgUserDirs() []xdgDir {
	b, err := os.ReadFile(filepath.Join(configHome(), "user-dirs.dirs"))
	if err != nil {
		return nil
	}
	var out []xdgDir
	for _, line := range strings.Split(string(b), "\n") {
		line = strings.TrimSpace(line)
		if !strings.HasPrefix(line, "XDG_") || !strings.Contains(line, "_DIR=") {
			continue
		}
		key, val, _ := strings.Cut(line, "=")
		name := strings.TrimSuffix(strings.TrimPrefix(key, "XDG_"), "_DIR")
		name = strings.ToUpper(name[:1]) + strings.ToLower(name[1:])
		val = strings.Trim(val, `"`)
		val = strings.ReplaceAll(val, "$HOME", "~")
		if val == "~/" || val == "~" {
			continue // disabled dir (points at $HOME itself)
		}
		out = append(out, xdgDir{name, val})
	}
	return out
}

// homeTopDirs lists the visible directories in $HOME, so the model proposes
// paths that exist instead of guessing conventions.
func homeTopDirs() []string {
	entries, err := os.ReadDir(home())
	if err != nil {
		return nil
	}
	var out []string
	for _, e := range entries {
		if e.IsDir() && !strings.HasPrefix(e.Name(), ".") {
			out = append(out, "~/"+e.Name())
		}
	}
	sort.Strings(out)
	if len(out) > 20 {
		out = out[:20]
	}
	return out
}

func userShell() string {
	if sh := os.Getenv("SHELL"); sh != "" {
		return filepath.Base(sh)
	}
	return "fish (Ryoku default)"
}

func editorName() string {
	ed := filepath.Base(os.Getenv("EDITOR"))
	// "" -> ".", and true/false are no-op sentinels (automation, editor
	// suppression), never real editors: fall back to the Ryoku default.
	if ed == "" || ed == "." || ed == "true" || ed == "false" {
		return "nvim (Ryoku default)"
	}
	return ed
}

// stackNotes names the modern-tool substitutions in force, so proposals use
// what the user's prompt actually runs.
func stackNotes() []string {
	pairs := []struct{ classic, modern, note string }{
		{"ls", "eza", "`ls` is aliased to eza"},
		{"cd", "zoxide", "`cd` is zoxide: it jumps to frecent dirs by fragment"},
		{"find", "fd", "prefer fd over find"},
		{"grep", "rg", "prefer rg over grep"},
		{"cat", "bat", "bat is available for paged, highlighted output"},
		{"top", "btop", "btop is the monitor"},
	}
	var out []string
	for _, p := range pairs {
		if _, err := exec.LookPath(p.modern); err == nil {
			out = append(out, p.note)
		}
	}
	if len(out) == 0 {
		out = append(out, "standard coreutils only")
	}
	return out
}

// secretish drops history lines that may carry credentials before any
// counting; only argv0/subcommand counts ever leave the parse anyway.
var secretish = regexp.MustCompile(`(?i)(password|passwd|secret|token|api[_-]?key|bearer |authorization)`)

// subcommandTools get their first subcommand counted alongside argv0.
var subcommandTools = map[string]bool{
	"git": true, "pacman": true, "systemctl": true, "docker": true, "cargo": true,
	"go": true, "npm": true, "pnpm": true, "yarn": true, "mise": true, "ryoku": true,
	"hermes": true, "kubectl": true, "make": true, "yay": true, "rashin": true,
}

// shellRhythms mines fish history for the top commands. The file is read
// from the tail only, capped, and never quoted verbatim.
func shellRhythms() []string {
	hist := filepath.Join(dataHome(), "fish", "fish_history")
	counts := historyCounts(hist, 256*1024)
	if len(counts) == 0 {
		return nil
	}
	type kv struct {
		k string
		n int
	}
	var all []kv
	for k, n := range counts {
		all = append(all, kv{k, n})
	}
	sort.Slice(all, func(i, j int) bool {
		if all[i].n != all[j].n {
			return all[i].n > all[j].n
		}
		return all[i].k < all[j].k
	})
	if len(all) > 15 {
		all = all[:15]
	}
	out := make([]string, 0, len(all))
	for _, e := range all {
		out = append(out, fmt.Sprintf("%s (%d)", e.k, e.n))
	}
	return out
}

// historyCounts parses `- cmd:` lines from the tail of a fish history file.
func historyCounts(path string, tailCap int64) map[string]int {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return nil
	}
	if st.Size() > tailCap {
		if _, err := f.Seek(st.Size()-tailCap, 0); err != nil {
			return nil
		}
	}
	buf := make([]byte, tailCap)
	n, _ := f.Read(buf)
	counts := map[string]int{}
	for _, line := range strings.Split(string(buf[:n]), "\n") {
		cmd, ok := strings.CutPrefix(strings.TrimSpace(line), "- cmd: ")
		if !ok || secretish.MatchString(cmd) {
			continue
		}
		key := commandKey(cmd)
		if key != "" {
			counts[key]++
		}
	}
	return counts
}

// commandKey reduces a history line to argv0 (plus subcommand for the tools
// where that is the story: git status vs git push).
func commandKey(cmd string) string {
	argv := stripEnvAssignments(shellFields(cmd))
	if len(argv) == 0 {
		return ""
	}
	name := filepath.Base(argv[0])
	if sudoLike[name] {
		argv = stripEnvAssignments(argv[1:])
		if len(argv) == 0 {
			return name
		}
		name = filepath.Base(argv[0])
	}
	if !subcommandTools[name] {
		return name
	}
	for _, a := range argv[1:] {
		if !strings.HasPrefix(a, "-") {
			return name + " " + a
		}
	}
	return name
}

// rashinUsage summarizes the ask log: repeated asks (recipe candidates) and
// recent corrections (proposed vs actually ran), so the model sees its own
// misses on the next ask.
func rashinUsage() string {
	var b strings.Builder
	asks := RecentAsks(0)
	if len(asks) > 0 {
		if reps := repeatedAsks(asks); len(reps) > 0 {
			b.WriteString("Repeated asks (recipe candidates):\n\n")
			for _, r := range reps {
				fmt.Fprintf(&b, "- %dx \"%s\"\n", r.n, r.q)
			}
			b.WriteString("\n")
		}
	}
	var corr []string
	for _, r := range RecentRuns(0) {
		if r.Proposed == "" || r.Proposed == r.Ran || secretish.MatchString(r.Ran) {
			continue
		}
		corr = append(corr, fmt.Sprintf("proposed `%s` -> user ran `%s` (exit %d)", r.Proposed, r.Ran, r.Status))
		if len(corr) == 5 {
			break
		}
	}
	if len(corr) > 0 {
		b.WriteString("Corrections, learn from these:\n\n- " + strings.Join(corr, "\n- ") + "\n")
	}
	return b.String()
}

type repeatedAsk struct {
	q string
	n int
}

// repeatedAsks groups near-duplicate questions by normalized token overlap.
func repeatedAsks(asks []askRecord) []repeatedAsk {
	var groups []struct {
		tokens map[string]bool
		first  string
		n      int
	}
	for _, a := range asks {
		toks := askTokens(a.Question)
		if len(toks) == 0 {
			continue
		}
		placed := false
		for i := range groups {
			if jaccard(groups[i].tokens, toks) >= 0.6 {
				groups[i].n++
				placed = true
				break
			}
		}
		if !placed {
			groups = append(groups, struct {
				tokens map[string]bool
				first  string
				n      int
			}{toks, a.Question, 1})
		}
	}
	var out []repeatedAsk
	for _, g := range groups {
		if g.n >= 2 {
			out = append(out, repeatedAsk{g.first, g.n})
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].n > out[j].n })
	if len(out) > 5 {
		out = out[:5]
	}
	return out
}

// countSimilarAsks reports how many recorded asks resemble q, the repeat
// signal behind the terminal lane's recipe hint.
func countSimilarAsks(q string) int {
	toks := askTokens(q)
	if len(toks) == 0 {
		return 0
	}
	n := 0
	for _, a := range RecentAsks(0) {
		if jaccard(toks, askTokens(a.Question)) >= 0.6 {
			n++
		}
	}
	return n
}

var askWord = regexp.MustCompile(`[a-z0-9]+`)

// stopWords carry no intent; dropping them keeps jaccard about the task.
var stopWords = map[string]bool{
	"the": true, "a": true, "an": true, "to": true, "of": true, "in": true,
	"on": true, "for": true, "and": true, "my": true, "me": true, "all": true,
	"is": true, "are": true, "it": true, "that": true, "this": true,
	"please": true, "can": true, "you": true, "do": true, "how": true,
}

func askTokens(q string) map[string]bool {
	toks := map[string]bool{}
	for _, w := range askWord.FindAllString(strings.ToLower(q), -1) {
		if !stopWords[w] {
			toks[w] = true
		}
	}
	return toks
}

func jaccard(a, b map[string]bool) float64 {
	if len(a) == 0 || len(b) == 0 {
		return 0
	}
	inter := 0
	for k := range a {
		if b[k] {
			inter++
		}
	}
	union := len(a) + len(b) - inter
	return float64(inter) / float64(union)
}

// habitsFingerprint changes when any habits source moves; the watcher
// rebuilds habits.md only then.
func habitsFingerprint() string {
	var b strings.Builder
	for _, p := range []string{
		filepath.Join(dataHome(), "fish", "fish_history"),
		filepath.Join(configHome(), "user-dirs.dirs"),
		askHistoryPath(),
		runsPath(),
		recipesPath(),
	} {
		if st, err := os.Stat(p); err == nil {
			b.WriteString(p + ":" + strconv.FormatInt(st.Size(), 10) + ":" + strconv.FormatInt(st.ModTime().Unix(), 10) + ";")
		}
	}
	return b.String()
}

func dataHome() string {
	if v := os.Getenv("XDG_DATA_HOME"); v != "" {
		return v
	}
	return filepath.Join(home(), ".local", "share")
}
