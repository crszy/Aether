package main

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// repo.go builds ryoku-repo.md: a pre-indexed map of the Ryoku monorepo
// itself, so agents fixing the system can navigate its source without
// crawling it. The snapshot is generated where the checkout exists (package
// build, deploy.sh) and shipped to the installed target, which has none.

// shippedRepoCandidates lists where a generated snapshot may live, packaged
// first, dev-deploy second. Reindex copies the newest one into the vault.
func shippedRepoCandidates() []string {
	if v := os.Getenv("RYOKU_RASHIN_SHIPPED"); v != "" {
		return []string{v}
	}
	return []string{
		"/usr/share/ryoku/rashin/ryoku-repo.md",
		filepath.Join(xdgState(), "ryoku", "rashin-repo.md"),
	}
}

func xdgState() string {
	if v := os.Getenv("XDG_STATE_HOME"); v != "" {
		return v
	}
	return filepath.Join(home(), ".local", "state")
}

// newestShippedRepoIndex returns the freshest existing snapshot, or "".
func newestShippedRepoIndex() string {
	best, bestT := "", time.Time{}
	for _, p := range shippedRepoCandidates() {
		fi, err := os.Stat(p)
		if err != nil || fi.IsDir() {
			continue
		}
		if fi.ModTime().After(bestT) {
			best, bestT = p, fi.ModTime()
		}
	}
	return best
}

// RepoIndexDoc walks a Ryoku checkout and renders the repo map body (the
// fenced part of ryoku-repo.md). Git details are best effort.
func RepoIndexDoc(root string) (string, error) {
	fi, err := os.Stat(root)
	if err != nil || !fi.IsDir() {
		return "", fmt.Errorf("not a directory: %s", root)
	}
	var b strings.Builder

	if commit, branch, when := gitFacts(root); commit != "" {
		b.WriteString("## Checkout\n\n")
		fmt.Fprintf(&b, "- Commit: %s (%s)\n", commit, branch)
		if when != "" {
			fmt.Fprintf(&b, "- Committed: %s\n", when)
		}
		fmt.Fprintf(&b, "- Indexed: %s\n\n", time.Now().Format("2006-01-02 15:04"))
	}

	b.WriteString("## Layout\n\n")
	b.WriteString("| Path | Files | Purpose |\n|---|---|---|\n")
	for _, row := range repoLayout(root) {
		fmt.Fprintf(&b, "| `%s` | %d | %s |\n", row.path, row.files, row.purpose)
	}

	b.WriteString("\n## Key entry points\n\n")
	b.WriteString("| Concern | Path |\n|---|---|\n")
	for _, e := range [][2]string{
		{"Hyprland config entry (Lua)", "ryoku/hyprland/hyprland.lua"},
		{"Shell daemon (Go)", "ryoku/shell/ipc/"},
		{"Settings app backend", "ryoku/hub/backend/"},
		{"Settings app UI (QML)", "ryoku/hub/quickshell/"},
		{"User CLI", "ryoku/cli/"},
		{"Rashin (this system)", "ryoku/rashin/"},
		{"Dev deploy loop", "ryoku/shell/deploy.sh"},
		{"Installer backend", "installation/backend/"},
		{"Package builds", "release/packages/"},
	} {
		if _, err := os.Stat(filepath.Join(root, e[1])); err == nil {
			fmt.Fprintf(&b, "| %s | `%s` |\n", e[0], e[1])
		}
	}

	if docs := docsList(root); len(docs) > 0 {
		b.WriteString("\n## Docs\n\n")
		for _, d := range docs {
			fmt.Fprintf(&b, "- `docs/%s` %s\n", d[0], d[1])
		}
	}
	return b.String(), nil
}

func gitFacts(root string) (commit, branch, when string) {
	git := func(args ...string) string {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		cmd := exec.CommandContext(ctx, "git", append([]string{"-C", root}, args...)...)
		out, err := cmd.Output()
		if err != nil {
			return ""
		}
		return strings.TrimSpace(string(out))
	}
	commit = git("rev-parse", "--short", "HEAD")
	branch = git("rev-parse", "--abbrev-ref", "HEAD")
	when = git("log", "-1", "--format=%cs")
	return
}

type layoutRow struct {
	path    string
	files   int
	purpose string
}

// repoLayout counts files under the repo's meaningful top paths, two levels
// deep, skipping VCS and build noise.
func repoLayout(root string) []layoutRow {
	purposes := map[string]string{
		"ryoku":                 "the desktop: apps, hyprland (Lua), shell UI, hub, rashin, assets",
		"system":                "machine definition: boot chain, hardware policy, package sets",
		"installation":          "how a machine is built: TUI, backend installer, ISO",
		"release":               "packaging: PKGBUILDs, the [ryoku] repo, keyring",
		"docs":                  "the guides",
		"bin":                   "repo tooling and CI checks",
		"tests":                 "standalone CI check scripts",
		".githooks":             "commit and push gates",
		"ryoku-shell-installer": "no-ISO converter for existing Arch installs",
	}
	skip := map[string]bool{".git": true, "node_modules": true, ".worktrees": true, "local": true}

	entries, err := os.ReadDir(root)
	if err != nil {
		return nil
	}
	var rows []layoutRow
	for _, e := range entries {
		if !e.IsDir() || skip[e.Name()] {
			continue
		}
		purpose, known := purposes[e.Name()]
		if !known {
			continue
		}
		n := 0
		_ = filepath.WalkDir(filepath.Join(root, e.Name()), func(p string, d os.DirEntry, err error) error {
			if err != nil {
				return nil
			}
			if d.IsDir() && skip[d.Name()] {
				return filepath.SkipDir
			}
			if !d.IsDir() {
				n++
			}
			return nil
		})
		rows = append(rows, layoutRow{path: e.Name() + "/", files: n, purpose: purpose})
	}
	sort.Slice(rows, func(i, j int) bool { return rows[i].files > rows[j].files })
	return rows
}

// docsList returns [file, first-heading] pairs for docs/*.md.
func docsList(root string) [][2]string {
	entries, err := os.ReadDir(filepath.Join(root, "docs"))
	if err != nil {
		return nil
	}
	var out [][2]string
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".md") {
			continue
		}
		title := ""
		if b, err := os.ReadFile(filepath.Join(root, "docs", e.Name())); err == nil {
			if line := firstLine(string(b)); strings.HasPrefix(line, "# ") {
				title = strings.TrimPrefix(line, "# ")
			}
		}
		out = append(out, [2]string{e.Name(), title})
	}
	return out
}

// writeRepoVaultDoc lays ryoku-repo.md into the vault: regenerated from a
// live checkout when RYOKU_RASHIN_REPO points at one, else copied from the
// newest shipped snapshot, else a stub explaining how to generate it.
func writeRepoVaultDoc() error {
	const header = "# Ryoku source map\n" +
		"\n" +
		"The Ryoku monorepo that produced this system: where its source lives and\n" +
		"what each part does. Generated: the content between the markers is\n" +
		"overwritten on every reindex."

	body := ""
	if repo := os.Getenv("RYOKU_RASHIN_REPO"); repo != "" {
		if doc, err := RepoIndexDoc(repo); err == nil {
			body = doc
		}
	}
	if body == "" {
		if snap := newestShippedRepoIndex(); snap != "" {
			if b, err := os.ReadFile(snap); err == nil {
				body = string(b)
			}
		}
	}
	if body == "" {
		body = "No repo snapshot found. On a packaged system it ships at\n" +
			"/usr/share/ryoku/rashin/ryoku-repo.md; on a checkout, run\n" +
			"`ryoku-rashin repo-index <checkout>` or `ryoku/shell/deploy.sh`.\n"
	}
	return writeVaultDoc("ryoku-repo.md", header, body)
}

// cmdRepoIndex is the generation verb used by the PKGBUILD and deploy.sh.
func cmdRepoIndex(root, out string) error {
	if root == "" {
		return fmt.Errorf("usage: ryoku-rashin repo-index <checkout-root> [out-file]")
	}
	doc, err := RepoIndexDoc(root)
	if err != nil {
		return err
	}
	if out == "" {
		fmt.Print(doc)
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		return err
	}
	return atomicWrite(out, []byte(doc), 0o644)
}
