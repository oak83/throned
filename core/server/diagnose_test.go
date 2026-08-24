package main

import "testing"

func TestOutboundServerFindsOutboundsAndEndpoints(t *testing.T) {
	tests := []struct {
		name     string
		config   string
		tag      string
		wantHost string
		wantPort int
	}{
		{
			name:     "ordinary outbound",
			config:   `{"outbounds":[{"tag":"proxy","type":"vless","server":"proxy.example","server_port":8443}]}`,
			tag:      "proxy",
			wantHost: "proxy.example",
			wantPort: 8443,
		},
		{
			name:     "openconnect endpoint with path and implicit port",
			config:   `{"endpoints":[{"tag":"proxy","type":"openconnect","server":"vpn.example/group"}]}`,
			tag:      "proxy",
			wantHost: "vpn.example",
			wantPort: 443,
		},
		{
			name:     "openconnect endpoint with explicit URL port",
			config:   `{"endpoints":[{"tag":"proxy","type":"openconnect","server":"https://vpn.example:4443/group"}]}`,
			tag:      "proxy",
			wantHost: "vpn.example",
			wantPort: 4443,
		},
		{
			name:     "openvpn endpoint remote list",
			config:   `{"endpoints":[{"tag":"proxy","type":"openvpn-client","servers":[{"server":"vpn.example"}]}]}`,
			tag:      "proxy",
			wantHost: "vpn.example",
			wantPort: 1194,
		},
		{
			name:     "chain fallback reaches endpoint",
			config:   `{"outbounds":[{"tag":"proxy","type":"direct"}],"endpoints":[{"tag":"config-1","type":"openconnect","server":"vpn.example/team"}]}`,
			tag:      "proxy",
			wantHost: "vpn.example",
			wantPort: 443,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			host, port := outboundServer(test.config, test.tag)
			if host != test.wantHost || port != test.wantPort {
				t.Fatalf("outboundServer() = %q:%d, want %q:%d", host, port, test.wantHost, test.wantPort)
			}
		})
	}
}

func TestOutboundServerRejectsMalformedConfig(t *testing.T) {
	host, port := outboundServer(`{"endpoints":[`, "proxy")
	if host != "" || port != 0 {
		t.Fatalf("outboundServer() = %q:%d for malformed config", host, port)
	}
}

func TestOutboundEndpointUsesOpenVPNTransport(t *testing.T) {
	tests := []struct {
		name        string
		config      string
		wantNetwork string
	}{
		{
			name:        "openvpn defaults to udp",
			config:      `{"endpoints":[{"tag":"proxy","type":"openvpn-client","servers":[{"server":"vpn.example"}]}]}`,
			wantNetwork: "udp",
		},
		{
			name:        "nested remote overrides transport",
			config:      `{"endpoints":[{"tag":"proxy","type":"openvpn-client","servers":[{"server":"vpn.example","network":"tcp"}]}]}`,
			wantNetwork: "tcp",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			_, _, network := outboundEndpoint(test.config, "proxy")
			if network != test.wantNetwork {
				t.Fatalf("outboundEndpoint() network = %q, want %q", network, test.wantNetwork)
			}
		})
	}
}

func TestOutboundEndpointsKeepsFallbacksAndMultiPort(t *testing.T) {
	openvpn := `{"endpoints":[{"tag":"proxy","type":"openvpn-client","network":"tcp","servers":[{"server":"one.example"},{"server":"two.example","server_port":443}]}]}`
	endpoints := outboundEndpoints(openvpn, "proxy")
	if len(endpoints) != 2 || endpoints[0].host != "one.example" || endpoints[0].port != 1194 ||
		endpoints[1].host != "two.example" || endpoints[1].port != 443 {
		t.Fatalf("outboundEndpoints() = %#v, want both OpenVPN remotes", endpoints)
	}

	host, port, network := outboundEndpoint(
		`{"outbounds":[{"tag":"proxy","type":"hysteria2","server":"hy.example","server_ports":["8443-8450","9443"]}]}`,
		"proxy")
	if host != "hy.example" || port != 8443 || network != "udp" {
		t.Fatalf("outboundEndpoint() = %q:%d/%s, want hy.example:8443/udp", host, port, network)
	}
}
