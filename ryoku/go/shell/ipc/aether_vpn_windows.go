package main

// Windows (Aether): Ryoku's VPN and WireGuard lists bridged to the VPN software on the PC instead of NetworkManager.
//   - Mullvad (mullvad.exe), Windscribe (windscribe-cli.exe): connect / disconnect / status through their CLIs
//   - Proton VPN (no CLI): shown connected while its network adapter is up; toggling opens its app
//   - WireGuard for Windows: tunnels imported here (.conf) are switched with wireguard.exe /installtunnelservice
//   - Windows' own VPN profiles (rasphone.pbk): rasdial
// With none of them installed, the list offers "Install a VPN": it asks first (a Windows dialog), then installs
// WireGuard with winget. Nothing installs without that yes.

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"
)

type vpnProvider struct {
	id, name, service string
	exe               string // the CLI or app
	kind              string // "cli-mullvad" | "cli-windscribe" | "app-proton" | "ras" | "install"
}

type aetherVPN struct {
	mu        sync.Mutex
	providers []vpnProvider
	state     map[string]string // id -> "on" | "connecting" | "off"
	tunnels   []string          // WireGuard tunnel names (configs in wgDir)
	wgActive  map[string]bool
	installing bool
}

func hiddenCmd(name string, args ...string) *exec.Cmd {
	cmd := exec.Command(name, args...)
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: 0x08000000}
	// a VPN CLI can leave a helper holding the output pipe it inherited: windscribe-cli exits, the pipe stays open,
	// and Wait waited for it forever - the whole daemon with it, before it ever started Ryoku's shell (2026-11-15)
	cmd.WaitDelay = 2 * time.Second
	return cmd
}

// runOut: a status query, never more than 8 s
func runOut(name string, args ...string) string {
	cmd := hiddenCmd(name, args...)
	var buf strings.Builder
	cmd.Stdout = &buf
	cmd.Stderr = &buf
	if cmd.Start() != nil {
		return ""
	}
	done := make(chan struct{})
	go func() { _ = cmd.Wait(); close(done) }()
	select {
	case <-done:
	case <-time.After(8 * time.Second):
		_ = cmd.Process.Kill()
		<-done
	}
	return strings.ReplaceAll(buf.String(), "\r\n", "\n")
}

func firstFile(paths ...string) string {
	for _, p := range paths {
		if p == "" {
			continue
		}
		if st, err := os.Stat(p); err == nil && !st.IsDir() {
			return p
		}
	}
	return ""
}

func programFiles() []string {
	return []string{os.Getenv("ProgramFiles"), os.Getenv("ProgramFiles(x86)"), os.Getenv("LOCALAPPDATA") + `\Programs`}
}

func findIn(rel ...string) string {
	var cands []string
	for _, base := range programFiles() {
		if base == "" {
			continue
		}
		for _, r := range rel {
			matches, _ := filepath.Glob(filepath.Join(base, r))
			cands = append(cands, matches...)
		}
	}
	return firstFile(cands...)
}

func wgExe() string    { return findIn(`WireGuard\wireguard.exe`) }
func wgDir() string    { return filepath.Join(os.Getenv("LOCALAPPDATA"), "AetherShell", "wireguard") }
func rasPhonebook() string {
	return filepath.Join(os.Getenv("APPDATA"), `Microsoft\Network\Connections\Pbk\rasphone.pbk`)
}

func (v *aetherVPN) detect() {
	var ps []vpnProvider
	if exe := firstFile(lookPath("mullvad"), findIn(`Mullvad VPN\resources\mullvad.exe`)); exe != "" {
		ps = append(ps, vpnProvider{"aether-vpn-mullvad", "Mullvad VPN", "mullvad", exe, "cli-mullvad"})
	}
	if exe := firstFile(lookPath("windscribe-cli"), findIn(`Windscribe\windscribe-cli.exe`)); exe != "" {
		ps = append(ps, vpnProvider{"aether-vpn-windscribe", "Windscribe", "windscribe", exe, "cli-windscribe"})
	}
	if exe := findIn(`Proton\VPN\ProtonVPN.Launcher.exe`, `Proton\VPN\v*\ProtonVPN.Client.exe`); exe != "" {
		ps = append(ps, vpnProvider{"aether-vpn-proton", "Proton VPN", "protonvpn", exe, "app-proton"})
	}
	if f, err := os.Open(rasPhonebook()); err == nil {
		sc := bufio.NewScanner(f)
		for sc.Scan() {
			line := strings.TrimSpace(sc.Text())
			if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
				name := line[1 : len(line)-1]
				ps = append(ps, vpnProvider{"aether-vpn-ras-" + name, name, "windows", name, "ras"})
			}
		}
		f.Close()
	}
	var tunnels []string
	if wgExe() != "" {
		entries, _ := os.ReadDir(wgDir())
		for _, e := range entries {
			if strings.HasSuffix(strings.ToLower(e.Name()), ".conf") {
				tunnels = append(tunnels, strings.TrimSuffix(e.Name(), filepath.Ext(e.Name())))
			}
		}
	}
	if len(ps) == 0 && wgExe() == "" {
		ps = append(ps, vpnProvider{"aether-vpn-install", "Install a VPN (WireGuard)", "install", "", "install"})
	}
	v.mu.Lock()
	v.providers, v.tunnels = ps, tunnels
	v.mu.Unlock()
}

func lookPath(name string) string {
	p, err := exec.LookPath(name)
	if err != nil {
		return ""
	}
	return p
}

func adapterUp(match string) bool {
	ifs, _ := net.Interfaces()
	for _, i := range ifs {
		if strings.Contains(strings.ToLower(i.Name), match) && i.Flags&net.FlagUp != 0 && i.Flags&net.FlagRunning != 0 {
			return true
		}
	}
	return false
}

func (v *aetherVPN) poll() {
	v.detect()
	v.mu.Lock()
	ps := append([]vpnProvider(nil), v.providers...)
	tunnels := append([]string(nil), v.tunnels...)
	v.mu.Unlock()
	state := map[string]string{}
	rasUp := ""
	for _, p := range ps {
		st := "off"
		switch p.kind {
		case "cli-mullvad":
			out := strings.ToLower(runOut(p.exe, "status"))
			if strings.HasPrefix(strings.TrimSpace(out), "connected") {
				st = "on"
			} else if strings.Contains(out, "connecting") {
				st = "connecting"
			}
		case "cli-windscribe":
			// windscribe-cli starts the Windscribe app when it is not running: polling it every 10 s reopened the
			// app each time the user closed it (2026-11-15). Closed app = off, and nothing is asked.
			if !aetherProcessRunning("windscribe.exe") {
				break
			}
			for _, line := range strings.Split(runOut(p.exe, "status"), "\n") {
				if strings.HasPrefix(strings.TrimSpace(line), "Connection:") {
					l := strings.ToLower(line)
					if strings.Contains(l, "disconnected") {
						st = "off"
					} else if strings.Contains(l, "connecting") {
						st = "connecting"
					} else if strings.Contains(l, "connected") {
						st = "on"
					}
				}
			}
		case "app-proton":
			if adapterUp("proton") {
				st = "on"
			}
		case "ras":
			if rasUp == "" {
				rasUp = runOut("rasdial")
			}
			if strings.Contains(rasUp, p.exe) {
				st = "on"
			}
		}
		state[p.id] = st
	}
	wg := map[string]bool{}
	for _, t := range tunnels {
		wg[t] = strings.Contains(runOut("sc", "query", "WireGuardTunnel$"+t), "RUNNING")
	}
	v.mu.Lock()
	v.state, v.wgActive = state, wg
	v.mu.Unlock()
}

func (v *aetherVPN) vpnFrames() []map[string]any {
	v.mu.Lock()
	defer v.mu.Unlock()
	out := []map[string]any{}
	for _, p := range v.providers {
		st := v.state[p.id]
		if p.kind == "install" && v.installing {
			st = "connecting"
		}
		out = append(out, map[string]any{
			"uuid": p.id, "name": p.name, "service": p.service,
			"active": st == "on" || st == "connecting", "connecting": st == "connecting", "ip4": "",
		})
	}
	return out
}

func (v *aetherVPN) wgFrames() []map[string]any {
	v.mu.Lock()
	defer v.mu.Unlock()
	out := []map[string]any{}
	for _, t := range v.tunnels {
		out = append(out, map[string]any{"uuid": "aether-wg-" + t, "name": t, "active": v.wgActive[t]})
	}
	return out
}

func (v *aetherVPN) provider(id string) (vpnProvider, bool) {
	v.mu.Lock()
	defer v.mu.Unlock()
	for _, p := range v.providers {
		if p.id == id {
			return p, true
		}
	}
	return vpnProvider{}, false
}

func (v *aetherVPN) setActive(id string, on bool) error {
	p, ok := v.provider(id)
	if !ok {
		return fmt.Errorf("unknown VPN %q", id)
	}
	switch p.kind {
	case "cli-mullvad":
		verb := "disconnect"
		if on {
			verb = "connect"
		}
		return hiddenCmd(p.exe, verb).Start()
	case "cli-windscribe":
		verb := "disconnect"
		if on {
			verb = "connect"
		}
		return hiddenCmd(p.exe, verb).Start()
	case "app-proton":
		// no CLI: its own window does it (connect / disconnect there)
		return exec.Command(p.exe).Start()
	case "ras":
		if on {
			return hiddenCmd("rasdial", p.exe).Start()
		}
		return hiddenCmd("rasdial", p.exe, "/disconnect").Start()
	case "install":
		if on {
			go v.offerInstall()
		}
		return nil
	}
	return nil
}

// asks with a Windows dialog first; only a "Yes" installs WireGuard (winget, the user's own package manager)
func (v *aetherVPN) offerInstall() {
	v.mu.Lock()
	if v.installing {
		v.mu.Unlock()
		return
	}
	v.installing = true
	v.mu.Unlock()
	defer func() {
		v.mu.Lock()
		v.installing = false
		v.mu.Unlock()
	}()
	ask := `Add-Type -AssemblyName PresentationFramework; ` +
		`$r = [System.Windows.MessageBox]::Show('No VPN app was found on this PC. Install WireGuard (free, from wireguard.com through winget) so Aether can switch VPN tunnels?' + [Environment]::NewLine + [Environment]::NewLine + 'You can then import a tunnel (.conf) from your VPN provider - Proton VPN, Mullvad and others offer WireGuard configs.', 'Aether - install a VPN?', 'YesNo', 'Question'); ` +
		`if ($r -eq 'Yes') { exit 0 } else { exit 1 }`
	if err := exec.Command("powershell", "-NoProfile", "-WindowStyle", "Hidden", "-Command", ask).Run(); err != nil {
		return // "No" or the dialog closed
	}
	_ = exec.Command("winget", "install", "--id", "WireGuard.WireGuard", "-e", "--silent",
		"--accept-package-agreements", "--accept-source-agreements").Run()
	v.poll()
}

func (v *aetherVPN) wgSet(uuid string, on bool) error {
	exe := wgExe()
	if exe == "" {
		return fmt.Errorf("WireGuard is not installed")
	}
	name := strings.TrimPrefix(uuid, "aether-wg-")
	conf := filepath.Join(wgDir(), name+".conf")
	if on {
		return hiddenCmd(exe, "/installtunnelservice", conf).Run()
	}
	return hiddenCmd(exe, "/uninstalltunnelservice", name).Run()
}

func (v *aetherVPN) wgImport(path string) error {
	if wgExe() == "" {
		go v.offerInstall()
		return fmt.Errorf("WireGuard is not installed yet")
	}
	b, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(wgDir(), 0o700); err != nil {
		return err
	}
	name := strings.TrimSuffix(filepath.Base(path), filepath.Ext(path))
	return os.WriteFile(filepath.Join(wgDir(), name+".conf"), b, 0o600)
}

func (v *aetherVPN) wgDelete(uuid string) error {
	_ = v.wgSet(uuid, false)
	return os.Remove(filepath.Join(wgDir(), strings.TrimPrefix(uuid, "aether-wg-")+".conf"))
}

// registers the network.vpn* / network.wg* calls; changed() republishes the network frame
func aetherStartVPN(d *daemon, changed func()) *aetherVPN {
	v := &aetherVPN{state: map[string]string{}, wgActive: map[string]bool{}}
	uuid := func(raw json.RawMessage) string {
		var m map[string]any
		_ = json.Unmarshal(raw, &m)
		s, _ := m["uuid"].(string)
		if s == "" {
			s, _ = m["path"].(string)
		}
		return s
	}
	after := func(err error) (any, error) {
		go func() {
			for i := 0; i < 6; i++ {
				time.Sleep(1500 * time.Millisecond)
				v.poll()
				changed()
			}
		}()
		return nil, err
	}
	d.registerCall("network.vpnActivate", func(raw json.RawMessage) (any, error) { return after(v.setActive(uuid(raw), true)) })
	d.registerCall("network.vpnDeactivate", func(raw json.RawMessage) (any, error) { return after(v.setActive(uuid(raw), false)) })
	d.registerCall("network.wgActivate", func(raw json.RawMessage) (any, error) { return after(v.wgSet(uuid(raw), true)) })
	d.registerCall("network.wgDeactivate", func(raw json.RawMessage) (any, error) { return after(v.wgSet(uuid(raw), false)) })
	d.registerCall("network.wgImport", func(raw json.RawMessage) (any, error) { return after(v.wgImport(uuid(raw))) })
	d.registerCall("network.wgDelete", func(raw json.RawMessage) (any, error) { return after(v.wgDelete(uuid(raw))) })
	// the first look runs beside the startup, not in it: a slow VPN CLI must not hold up the shell
	go func() {
		v.poll()
		changed()
	}()
	return v
}
