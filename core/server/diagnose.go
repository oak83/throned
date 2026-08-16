package main

import (
	"ThroneCore/gen"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"time"

	M "github.com/sagernet/sing/common/metadata"
)

// A URL test collapses every failure into one deadline, which says nothing about
// which hop broke. Diagnose walks the same path one stage at a time so the
// failing stage names itself: a dead name server no longer looks like a dead
// server. Steps reuse URLTestResp - the label rides in OutboundTag, so this
// needs no new message on either side of the IPC.
func (s *server) Diagnose(ctx context.Context, in *gen.TestReq) (*gen.TestResp, error) {
	const stageTimeout = 10 * time.Second

	var steps []*gen.URLTestResp
	step := func(label string, started time.Time, err error) bool {
		text := ""
		if err != nil {
			text = err.Error()
		}
		steps = append(steps, &gen.URLTestResp{
			OutboundTag: To(label),
			LatencyMs:   To(int32(time.Since(started).Milliseconds())),
			Error:       To(text),
		})
		return err == nil
	}

	tag := "proxy"
	if len(in.OutboundTags) > 0 {
		tag = in.OutboundTags[0]
	}

	host, port := outboundServer(in.GetConfig(), tag)
	if host == "" {
		return &gen.TestResp{Results: []*gen.URLTestResp{{
			OutboundTag: To("config"),
			LatencyMs:   To(int32(0)),
			Error:       To("no outbound named " + tag + " carries a server address"),
		}}}, nil
	}

	// Stage 1: the name. An address literal has nothing to resolve.
	addr := host
	if net.ParseIP(host) == nil {
		started := time.Now()
		resolveCtx, cancel := context.WithTimeout(ctx, stageTimeout)
		ips, err := net.DefaultResolver.LookupHost(resolveCtx, host)
		cancel()
		if len(ips) > 0 {
			addr = ips[0]
		}
		label := "resolve " + host
		if err == nil && len(ips) > 0 {
			label += " → " + ips[0]
		}
		if !step(label, started, err) {
			return &gen.TestResp{Results: steps}, nil
		}
	}

	// Stage 2: reachability of the endpoint itself, before any protocol runs.
	endpoint := net.JoinHostPort(addr, strconv.Itoa(port))
	started := time.Now()
	conn, err := net.DialTimeout("tcp", endpoint, stageTimeout)
	if conn != nil {
		conn.Close()
	}
	if !step("tcp "+endpoint, started, err) {
		return &gen.TestResp{Results: steps}, nil
	}

	// Stage 3 and 4 need the outbound itself, which is what carries tls, the
	// transport and the proxy handshake.
	env, err := prepareTestEnv(in.GetTestCurrent(), in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound())
	if err != nil {
		step("start core", time.Now(), err)
		return &gen.TestResp{Results: steps}, nil
	}
	defer env.close()

	outbound, exists := env.box.Outbound().Outbound(tag)
	if !exists {
		step("start core", time.Now(), fmt.Errorf("no outbound with tag %s", tag))
		return &gen.TestResp{Results: steps}, nil
	}

	target := in.GetUrl()
	parsed, err := url.Parse(target)
	if err != nil {
		step("target url", time.Now(), err)
		return &gen.TestResp{Results: steps}, nil
	}
	targetPort := parsed.Port()
	if targetPort == "" {
		targetPort = map[string]string{"http": "80", "https": "443"}[parsed.Scheme]
	}
	targetAddr := net.JoinHostPort(parsed.Hostname(), targetPort)

	started = time.Now()
	dialCtx, cancelDial := context.WithTimeout(ctx, stageTimeout)
	proxyConn, err := outbound.DialContext(dialCtx, "tcp", M.ParseSocksaddr(targetAddr))
	cancelDial()
	if !step("proxy handshake to "+targetAddr, started, err) {
		return &gen.TestResp{Results: steps}, nil
	}

	// Reuse the connection the handshake stage opened: dialing again would
	// measure a second handshake instead of the request.
	started = time.Now()
	client := &http.Client{
		Timeout: stageTimeout,
		Transport: &http.Transport{
			DialContext: func(context.Context, string, string) (net.Conn, error) { return proxyConn, nil },
		},
	}
	resp, err := client.Get(target)
	label := "http " + parsed.Hostname()
	if err == nil {
		label += " → " + resp.Status
		io.Copy(io.Discard, io.LimitReader(resp.Body, 4096))
		resp.Body.Close()
	}
	step(label, started, err)

	return &gen.TestResp{Results: steps}, nil
}

// outboundServer digs the server address out of the generated config so the
// early stages can be run without standing a core up first.
func outboundServer(config string, tag string) (string, int) {
	var parsed struct {
		Outbounds []struct {
			Tag        string `json:"tag"`
			Server     string `json:"server"`
			ServerPort int    `json:"server_port"`
		} `json:"outbounds"`
	}
	if json.Unmarshal([]byte(config), &parsed) != nil {
		return "", 0
	}
	for _, outbound := range parsed.Outbounds {
		if outbound.Tag == tag && outbound.Server != "" {
			return outbound.Server, outbound.ServerPort
		}
	}
	// A chain hands its tag to the outermost hop, so fall back to whichever
	// outbound carries an address at all.
	for _, outbound := range parsed.Outbounds {
		if outbound.Server != "" {
			return outbound.Server, outbound.ServerPort
		}
	}
	return "", 0
}
