//go:build windows

// Blocks everything but this process while no sing-box instance is up.
//
// sing-tun installs its own WFP filters for strict_route, but they belong to
// the tun device: stopping a profile takes them down with it, and the gap
// before the next one starts leaks straight out of the physical interface.
// These filters belong to the core process instead, which outlives Stop/Start,
// so the gap stays covered.
package killswitch

import (
	"sync"

	"golang.zx2c4.com/wireguard/windows/tunnel/firewall"
)

var mu sync.Mutex
var enabled bool

// The session WireGuard opens is FWPM_SESSION_FLAG_DYNAMIC, so a crashed or
// killed core takes the filters with it and cannot leave the machine offline.
func Enable() error {
	mu.Lock()
	defer mu.Unlock()
	if enabled {
		return nil
	}
	// luid 0: there is no tunnel to permit during a transition, which is the
	// point - only this process, loopback, DHCP and NDP get through.
	if err := firewall.EnableFirewall(0, false, nil); err != nil {
		return err
	}
	enabled = true
	return nil
}

func Disable() {
	mu.Lock()
	defer mu.Unlock()
	if !enabled {
		return
	}
	firewall.DisableFirewall()
	enabled = false
}

func Enabled() bool {
	mu.Lock()
	defer mu.Unlock()
	return enabled
}
