//go:build !windows

package killswitch

import "errors"

// Linux and macOS route around the tun with ip rules rather than a packet
// filter, so the same trick needs an nftables/pf equivalent that does not exist
// here yet. Reporting it plainly beats pretending the guard is up.
func Enable() error {
	return errors.New("transition guard is only implemented on windows")
}

func Disable() {}

func Enabled() bool { return false }
