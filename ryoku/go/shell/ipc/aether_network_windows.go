package main

// Windows (Aether): Ryoku's "network" topic and its network.* calls from Windows itself instead of NetworkManager
// (D-Bus). Wi-Fi comes from `netsh wlan` (the interface state, Windows' own cached network list and the saved
// profiles - nothing here triggers a scan: Windows refreshes that list itself), wired from the adapters' state.
// Copied into shell/ipc by dotfiles/ryoku-go-port.js; network.go's startNetwork calls aetherStartNetwork first.

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

type aetherNet struct {
	d     *daemon
	topic *stateTopic

	mu       sync.Mutex
	iface    map[string]string   // netsh wlan show interfaces (first adapter)
	aps      []map[string]any    // cached network list
	saved    map[string]bool     // saved profile names
	manual   map[string]bool     // saved profiles set to connect manually
	lastList time.Time
	vpn      *aetherVPN
	last     []byte
}

func netshRun(args ...string) string {
	cmd := exec.Command("netsh", args...)
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: 0x08000000} // CREATE_NO_WINDOW
	out, _ := cmd.Output()
	return strings.ReplaceAll(string(out), "\r\n", "\n")
}

var netshKV = regexp.MustCompile(`^\s*([^:]+?)\s*:\s?(.*)$`)

// "Name : value" lines of the first interface block
func parseInterfaces(s string) map[string]string {
	m := map[string]string{}
	for _, line := range strings.Split(s, "\n") {
		kv := netshKV.FindStringSubmatch(line)
		if kv == nil {
			continue
		}
		k := strings.ToLower(strings.TrimSpace(kv[1]))
		if k == "name" && m["name"] != "" {
			break // a second adapter: the first one is the one shown
		}
		if _, ok := m[k]; !ok {
			m[k] = strings.TrimSpace(kv[2])
		}
	}
	return m
}

func percent(s string) int {
	n, _ := strconv.Atoi(strings.TrimSuffix(strings.TrimSpace(s), "%"))
	return n
}

func securityOf(auth string) string {
	a := strings.ToLower(auth)
	switch {
	case strings.Contains(a, "wpa3"):
		return "Wpa3"
	case strings.Contains(a, "enterprise"):
		return "Enterprise"
	case strings.Contains(a, "wpa2"):
		return "Wpa2"
	case strings.Contains(a, "wpa"):
		return "Wpa"
	case strings.Contains(a, "wep"):
		return "Wep"
	}
	return "None"
}

func freqOf(channel string, band string) int {
	ch, _ := strconv.Atoi(strings.TrimSpace(channel))
	b := strings.ToLower(band)
	switch {
	case strings.HasPrefix(b, "6"):
		return 5950 + ch*5
	case strings.HasPrefix(b, "5") || ch > 14:
		return 5000 + ch*5
	case ch == 14:
		return 2484
	case ch > 0:
		return 2407 + ch*5
	}
	return 0
}

// `netsh wlan show networks mode=bssid`: one entry per SSID with its strongest BSSID
func parseNetworks(s string) []map[string]any {
	type ap struct {
		ssid, auth, bssid, channel, band string
		signal                           int
	}
	var list []*ap
	var cur *ap
	var bss struct {
		bssid, channel, band string
		signal               int
	}
	flush := func() {
		if cur != nil && bss.bssid != "" && bss.signal >= cur.signal {
			cur.bssid, cur.channel, cur.band, cur.signal = bss.bssid, bss.channel, bss.band, bss.signal
		}
		bss.bssid, bss.channel, bss.band, bss.signal = "", "", "", 0
	}
	for _, line := range strings.Split(s, "\n") {
		kv := netshKV.FindStringSubmatch(line)
		if kv == nil {
			continue
		}
		k := strings.ToLower(strings.TrimSpace(kv[1]))
		v := strings.TrimSpace(kv[2])
		switch {
		case strings.HasPrefix(k, "ssid "):
			flush()
			cur = &ap{ssid: v}
			list = append(list, cur)
		case cur == nil:
		case k == "authentication":
			cur.auth = v
		case strings.HasPrefix(k, "bssid"):
			flush()
			bss.bssid = v
		case k == "signal":
			bss.signal = percent(v)
		case k == "channel":
			bss.channel = v
		case k == "band":
			bss.band = v
		}
	}
	flush()
	out := []map[string]any{}
	for _, a := range list {
		if a.ssid == "" {
			continue
		}
		f := freqOf(a.channel, a.band)
		out = append(out, map[string]any{
			"ssid": a.ssid, "strength": a.signal, "security": securityOf(a.auth), "bssid": a.bssid,
			"frequency": f, "band": bandForFrequency(f), "saved": false, "active": false, "autoconnect": false,
		})
	}
	return out
}

func parseProfiles(s string) map[string]bool {
	m := map[string]bool{}
	for _, line := range strings.Split(s, "\n") {
		if kv := netshKV.FindStringSubmatch(line); kv != nil && strings.Contains(strings.ToLower(kv[1]), "profile") {
			if name := strings.TrimSpace(kv[2]); name != "" {
				m[name] = true
			}
		}
	}
	return m
}

func wiredState() (present, connected bool) {
	ifs, _ := net.Interfaces()
	for _, i := range ifs {
		n := strings.ToLower(i.Name)
		if !strings.HasPrefix(n, "ethernet") {
			continue
		}
		present = true
		if i.Flags&net.FlagUp != 0 && i.Flags&net.FlagRunning != 0 {
			connected = true
		}
	}
	return
}

func (a *aetherNet) refresh(full bool) {
	iface := parseInterfaces(netshRun("wlan", "show", "interfaces"))
	var aps []map[string]any
	var saved map[string]bool
	if full {
		aps = parseNetworks(netshRun("wlan", "show", "networks", "mode=bssid"))
		saved = parseProfiles(netshRun("wlan", "show", "profiles"))
	}
	a.mu.Lock()
	a.iface = iface
	if full {
		a.aps, a.saved, a.lastList = aps, saved, time.Now()
	}
	a.mu.Unlock()
	a.publish()
}

func (a *aetherNet) frame() map[string]any {
	a.mu.Lock()
	defer a.mu.Unlock()
	state := strings.ToLower(a.iface["state"])
	present := a.iface["name"] != ""
	// on = the adapter is there and its radio is not switched off (being disconnected is still "on")
	radio := strings.ToLower(a.iface["radio status"] + " " + a.iface["software"] + " " + a.iface["hardware"])
	enabled := present && !strings.Contains(radio, "off")
	conn := "Disconnected"
	switch {
	case state == "connected":
		conn = "Connected"
	case strings.Contains(state, "associat") || strings.Contains(state, "authenticat") || strings.Contains(state, "discover"):
		conn = "Connecting"
	}
	ssid := ""
	strength := 0
	if conn == "Connected" {
		ssid = a.iface["ssid"]
		strength = percent(a.iface["signal"])
	}
	wifi := map[string]any{"present": present, "enabled": enabled || conn != "Disconnected",
		"connectivity": conn, "ssid": ssid, "strength": strength}
	aps := []map[string]any{}
	for _, ap := range a.aps {
		c := map[string]any{}
		for k, v := range ap {
			c[k] = v
		}
		name := c["ssid"].(string)
		c["saved"] = a.saved[name]
		c["autoconnect"] = a.saved[name] && !a.manual[name]
		c["active"] = conn == "Connected" && name == ssid
		aps = append(aps, c)
	}
	wPresent, wUp := wiredState()
	wiredConn := "Disconnected"
	if wUp {
		wiredConn = "Connected"
	}
	kind, online := "none", false
	if conn == "Connected" {
		kind, online = "wifi", true
	}
	if wUp {
		kind, online = "ethernet", true
	}
	connectivity := "none"
	if online {
		connectivity = "full"
	}
	return map[string]any{
		"wifi":         wifi,
		"wired":        map[string]any{"present": wPresent, "connectivity": wiredConn},
		"connection":   networkConnection{Kind: kind, Connectivity: connectivity, Online: online},
		"accessPoints": aps,
		"wireguard":    a.vpnWG(),
		"vpn":          a.vpnList(),
		"dns":          map[string]any{"provider": dnsProviderForServers([]string{}), "servers": []string{}},
	}
}

func (a *aetherNet) publish() {
	b, err := json.Marshal(a.frame())
	if err != nil {
		return
	}
	a.mu.Lock()
	same := string(b) == string(a.last)
	a.last = b
	a.mu.Unlock()
	if !same && a.topic != nil {
		a.topic.publish(b)
	}
}

func xmlEscape(s string) string {
	r := strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;", `"`, "&quot;", "'", "&apos;")
	return r.Replace(s)
}

// a new network: a Windows WLAN profile (WPA2/WPA3-Personal with the password, or open), then connect
func (a *aetherNet) connect(ssid, password string, hidden bool) error {
	a.mu.Lock()
	saved := a.saved[ssid]
	a.mu.Unlock()
	if !saved || password != "" {
		auth, enc, key := "open", "none", ""
		if password != "" {
			auth, enc = "WPA2PSK", "AES"
			key = "<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>" +
				xmlEscape(password) + "</keyMaterial></sharedKey>"
		}
		nonBroadcast := "false"
		if hidden {
			nonBroadcast = "true"
		}
		profile := `<?xml version="1.0"?><WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">` +
			"<name>" + xmlEscape(ssid) + "</name><SSIDConfig><SSID><name>" + xmlEscape(ssid) + "</name></SSID><nonBroadcast>" +
			nonBroadcast + "</nonBroadcast></SSIDConfig><connectionType>ESS</connectionType><connectionMode>auto</connectionMode>" +
			"<MSM><security><authEncryption><authentication>" + auth + "</authentication><encryption>" + enc +
			"</encryption><useOneX>false</useOneX></authEncryption>" + key + "</security></MSM></WLANProfile>"
		f, err := os.CreateTemp("", "aether-wlan-*.xml")
		if err != nil {
			return err
		}
		path := f.Name()
		_, _ = f.WriteString(profile)
		f.Close()
		out := netshRun("wlan", "add", "profile", "filename="+filepath.Clean(path), "user=current")
		_ = os.Remove(path) // the password lives in Windows' profile store now, not in a file
		if !strings.Contains(strings.ToLower(out), "added") {
			return fmt.Errorf("could not save the network: %s", strings.TrimSpace(out))
		}
	}
	out := netshRun("wlan", "connect", "name="+ssid)
	if !strings.Contains(strings.ToLower(out), "success") {
		return fmt.Errorf("%s", strings.TrimSpace(out))
	}
	go func() { time.Sleep(3 * time.Second); a.refresh(true) }()
	return nil
}

// aetherStartNetwork runs the Windows network source; network.go's NetworkManager path is skipped
func aetherStartNetwork(d *daemon) bool {
	a := &aetherNet{d: d, topic: d.registerTopic("network"), manual: map[string]bool{}}
	str := func(raw json.RawMessage, key string) string {
		var m map[string]any
		_ = json.Unmarshal(raw, &m)
		s, _ := m[key].(string)
		return s
	}
	boolean := func(raw json.RawMessage, key string) bool {
		var m map[string]any
		_ = json.Unmarshal(raw, &m)
		b, _ := m[key].(bool)
		return b
	}
	later := func() { go func() { time.Sleep(1500 * time.Millisecond); a.refresh(true) }() }
	// Wi-Fi "off" only disconnects: the adapter itself is never disabled in Windows (that took the user's Wi-Fi
	// driver down once, and turning it back on needs Device Manager / admin). "On" reconnects to the network
	// Windows would pick (the strongest saved one in range).
	d.registerCall("network.wifiSetEnabled", func(raw json.RawMessage) (any, error) {
		if !boolean(raw, "enabled") {
			netshRun("wlan", "disconnect")
		} else {
			a.mu.Lock()
			best, bestStrength := "", -1
			for _, ap := range a.aps {
				name, _ := ap["ssid"].(string)
				s, _ := ap["strength"].(int)
				if a.saved[name] && s > bestStrength {
					best, bestStrength = name, s
				}
			}
			a.mu.Unlock()
			if best != "" {
				netshRun("wlan", "connect", "name="+best)
			}
		}
		later()
		return nil, nil
	})
	d.registerCall("network.wifiScan", func(json.RawMessage) (any, error) { a.refresh(true); return nil, nil })
	d.registerCall("network.wifiConnect", func(raw json.RawMessage) (any, error) {
		return nil, a.connect(str(raw, "ssid"), str(raw, "password"), boolean(raw, "hidden"))
	})
	d.registerCall("network.wifiDisconnect", func(json.RawMessage) (any, error) {
		netshRun("wlan", "disconnect")
		later()
		return nil, nil
	})
	d.registerCall("network.wifiForget", func(raw json.RawMessage) (any, error) {
		netshRun("wlan", "delete", "profile", "name="+str(raw, "ssid"))
		later()
		return nil, nil
	})
	d.registerCall("network.wifiSetAutoconnect", func(raw json.RawMessage) (any, error) {
		ssid := str(raw, "ssid")
		mode := "manual"
		if boolean(raw, "autoconnect") {
			mode = "auto"
		}
		netshRun("wlan", "set", "profileparameter", "name="+ssid, "connectionmode="+mode)
		a.mu.Lock()
		a.manual[ssid] = mode == "manual"
		a.mu.Unlock()
		later()
		return nil, nil
	})
	d.registerCall("network.dnsSet", func(json.RawMessage) (any, error) { return nil, fmt.Errorf("not available on Windows") })
	// the VPN apps on the PC (Mullvad, Windscribe, Proton, WireGuard, Windows VPN profiles): aether_vpn_windows.go
	a.vpn = aetherStartVPN(d, a.publish)
	go func() {
		a.refresh(true)
		for i := 1; ; i++ {
			time.Sleep(5 * time.Second)
			if i%2 == 0 {
				a.vpn.poll() // VPN states every 10 s
			}
			a.refresh(i%4 == 0) // the interface every 5 s, the cached list and profiles every 20 s
		}
	}()
	return true
}

func (a *aetherNet) vpnList() []map[string]any {
	if a.vpn == nil {
		return []map[string]any{}
	}
	return a.vpn.vpnFrames()
}

func (a *aetherNet) vpnWG() []map[string]any {
	if a.vpn == nil {
		return []map[string]any{}
	}
	return a.vpn.wgFrames()
}
