# Throned

Throned is a practical fork of [Throne](https://github.com/throneproj/Throne), a Qt desktop proxy client powered by sing-box and Xray.

The fork exists to ship focused networking fixes quickly while staying close enough to upstream to keep receiving protocol, security, and compatibility updates.

## Why Throned?

The first Throned release addresses an intermittent Windows TUN self-loop. After a network-interface change, traffic to sing-tun's internal DNS peer could fall through to `direct`, get captured by the same TUN again, and repeat indefinitely. Typical symptoms included:

- Discord or in-game voice chat suddenly stopping;
- repeated `ThroneCore.exe -> 172.19.0.2:53` log entries;
- increased CPU or memory use;
- temporarily recovering after restarting TUN mode.

Throned updates sing-tun with the upstream interface-monitor fix and adds an early DNS guard calculated from the configured TUN subnet, so custom TUN ranges are protected too.

## Downloads

Stable builds are published on the [Releases](https://github.com/troshkindm/throned/releases) page.

| Platform | Package | Status |
| --- | --- | --- |
| Windows x64 | Portable ZIP | Supported |
| Linux x64 | Portable ZIP | Supported |
| Windows ARM64 / legacy Windows | Not currently published | Planned |
| Linux ARM64 | Not currently published | Planned |
| macOS | Build from source | Upstream-compatible, not CI-tested here |

TUN mode requires administrator privileges on Windows and elevated network capabilities on Linux.

## Versioning and releases

Throned uses ordinary numeric [Semantic Versioning](https://semver.org/):

- patch fixes: `1.0.1`, `1.0.2`;
- backward-compatible features: `1.1.0`;
- incompatible changes: `2.0.0`.

Each release note states which Throne version or commit it is based on. A release is published only after the Windows x64 and Linux x64 portable builds complete successfully. Development builds remain available as GitHub Actions artifacts.

## Building

The authoritative build recipes live in [.github/workflows/throned-windows.yml](.github/workflows/throned-windows.yml). They build `ThroneCore`, the Qt application, and portable packages in clean GitHub runners.

The project currently expects:

- Go 1.26.x;
- CMake and Ninja;
- Qt 6.11.x;
- Protobuf 31.x;
- MSVC on Windows or GCC on Linux.

## Upstream policy

Upstream changes are periodically merged from `throneproj/Throne`. Throned-specific patches are kept small and documented so they can be rebased, retired when upstream supersedes them, or proposed upstream where appropriate.

Bug reports should include the Throned version, operating system, TUN settings, routing profile, and the log section from when the problem occurs.

## Supported protocols

Throned inherits Throne's sing-box and Xray protocol support, including VLESS, VMess, Trojan, Shadowsocks, SOCKS, HTTP(S), TUIC, Hysteria, Hysteria2, AnyTLS, WireGuard/AmneziaWG, SSH, Mieru, NaiveProxy, Juicity, TrustTunnel, custom outbounds, custom configs, chains, and extra cores.

## Credits and license

Throned is built on the work of [Throne](https://github.com/throneproj/Throne), [sing-box](https://github.com/SagerNet/sing-box), [Xray-core](https://github.com/XTLS/Xray-core), Qt, and the other projects listed in the source tree.

Licensed under [GPL-3.0](LICENSE).
