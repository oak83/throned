package main

import (
	"testing"

	"ThroneCore/internal/boxmain"
)

func TestTunPeerGuardConfig(t *testing.T) {
	config := []byte(`{
		"inbounds": [{
			"type": "mixed",
			"tag": "tun-in",
			"listen": "127.0.0.1",
			"listen_port": 2080
		}],
		"outbounds": [{"type": "direct", "tag": "direct"}],
		"route": {
			"rules": [{
				"inbound": ["tun-in"],
				"ip_cidr": ["172.19.0.2/32"],
				"action": "reject",
				"method": "drop"
			}],
			"final": "direct"
		}
	}`)

	if err := boxmain.Check(config); err != nil {
		t.Fatalf("TUN peer guard config is invalid: %v", err)
	}
}
