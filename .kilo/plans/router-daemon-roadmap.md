# Route-Daemon — Phased Build Plan

A high-performance, C-based software router distributed as a turnkey Docker image.
End goal: a user pulls the image on a laptop, plugs in one uplink NIC, and instantly has
a secure router with firewall, bufferbloat mitigation, WireGuard VPN, and XDP-based DPI.

## Confirmed Decisions
- **Data plane:** Kernel-first (nftables + sysctl) → add XDP/eBPF fast-path later.
- **Management:** CLI tool + minimal embedded Web UI (first-run setup wizard).
- **Build system:** CMake.
- **Topology:** One NIC uplink (WAN) + onboard WiFi as LAN/AP.
- **Language:** C (daemon + control plane); eBPF C for XDP programs.
- **Role split:** User writes most code; AI gives guidance only when asked.
- **Base image:** Alpine (privileged container, `--network=host` for real routing).

## Architecture Overview
```
                ┌──────────────────────────────────────────┐
   Web UI ──┐   │              Route-Daemon (C)             │
   CLI ──────┼──▶│  Control Plane: config, IF mgmt, nft,    │
            │IPC │  WG, SQM, sysctl, lifecycle, metrics      │
            │   │                                            │
            │   │  Data Plane (Phase 1-4): kernel forward +  │
            │   │    nftables NAT/firewall + fq_codel/cake   │
            │   │  Data Plane (Phase 5): XDP/eBPF fast-path  │
            │   └──────────────────────────────────────────┘
   WAN NIC ────────────────────▶ forwarding ───────────────▶ WiFi AP (LAN)
```
- Single daemon process, signal-driven lifecycle, structured logging.
- Local IPC socket (Unix domain) between daemon ↔ CLI ↔ Web UI.
- Config persisted under `/etc/route-daemon/` (bind-mounted volume).

---

## Phase 0 — Foundation & Project Skeleton
**Goal:** A buildable, runnable C daemon skeleton inside the container with logging, config, and lifecycle.

Key components:
- CMake project (`CMakeLists.txt`, `src/`, `include/`, `build/`).
- Daemon skeleton: `main.c` with signal handling (SIGTERM/SIGHUP), PID file, graceful shutdown, daemonize option.
- Structured logging module (levels, timestamps, file + stderr, rotation).
- Config parser (TOML or JSON) loading from `/etc/route-daemon/config.*`.
- Extend `Dockerfile` with build toolchain: `gcc`, `cmake`, `make`, `musl-dev`, `linux-headers`, `pkgconf`, `git`.
- Keep runtime deps minimal (build layer vs runtime separation — finalized in Phase 8).
- Unit-test harness selection (e.g. a tiny framework or `ctest`).

Deliverables: `route-daemon` binary starts, logs, parses config, handles reload/shutdown.
Exit criteria: `docker exec` → binary runs and responds to `SIGHUP` (reload) and `SIGTERM` (clean exit).

---

## Phase 1 — Core Network Stack (Kernel Forwarding)
**Goal:** A functioning router: WAN uplink, WiFi AP LAN, NAT, basic forwarding.

Key components:
- Interface abstraction layer: discover NICs, assign WAN/LAN roles, link state events (via netlink/rtnetlink).
- Enable IP forwarding (`net.ipv4.ip_forward`, `net.ipv6.conf.all.forwarding`) programmatically.
- WAN: DHCP client (integrate `udhcpc`/`dhclient` or call via scripts) + default route tracking.
- LAN: DHCP server (integrate `dnsmasq` or `kea`) handing out addresses on the WiFi subnet.
- WiFi AP: `hostapd` config generation for the onboard WiFi NIC (SSID, WPA2/3, channel).
- NAT/masquerade on WAN via `nftables` (initial rule set).
- Base nftables table chain: forward + postrouting masquerade.

Deliverables: A client on WiFi gets an IP and can reach the internet through the laptop.
Exit criteria: Device connected to AP → `ping`/`curl` to the internet succeeds; traffic masqueraded on WAN.

Dependencies: Phase 0.

---

## Phase 2 — Firewall & Security
**Goal:** Default-deny, hardened firewall with a C-managed nftables abstraction.

Key components:
- nftables rule manager in C: load/replace/flush rulesets atomically (transactional `nft -f` or libnftables).
- Default policy: drop inbound on WAN, allow established/related, allow LAN→WAN.
- Port forwarding / DNAT API (config-driven).
- ICMP/NDP sane defaults (rate-limited, no floods).
- Anti-spoofing (rp_filter, martian logging), bogon drop lists.
- Conntrack tuning baseline (max size, hash size).
- Input hardening: only daemon's management ports exposed on LAN.

Deliverables: Configurable firewall via config file; verified drop/allow behavior.
Exit criteria: External scan of WAN shows no open ports; LAN clients still route; port-forward rule works.

Dependencies: Phase 1.

---

## Phase 3 — Performance & Bufferbloat Mitigation
**Goal:** Low latency under load + high throughput via kernel tuning and AQM.

Key components:
- SQM/AQM: apply `cake` or `fq_codel` on WAN egress; ingress shaping via IFB or `cake` ingress.
- Bufferbloat mitigation verification (flent / RRUL-style test plan).
- sysctl tuning profile: `net.core.{r,w}mem_max`, `netdev_max_backlog`, `tcp_*`, `somaxconn`.
- NIC tuning: ring buffer sizes (`ethtool -G`), offloads (GRO/GSO/TSO), RPS/RFS/XPS, IRQ affinity.
- Conntrack + route cache sizing for expected client counts.
- Benchmarking harness: iperf3 + latency-under-load scripts, results captured to logs/metrics.

Deliverables: A tunable performance profile; documented baseline numbers.
Exit criteria: RRUL/latency-under-load shows flat low latency; throughput meets NIC ceiling.

Dependencies: Phase 1, 2.

---

## Phase 4 — WireGuard VPN
**Goal:** Site-to-site or road-warrior VPN integrated with firewall and routing.

Key components:
- WireGuard integration: ensure `wireguard-tools` + kernel module, manage interface lifecycle in C.
- Key management: generate/persist private key, peer public-key/config storage.
- Peer config API: add/remove peers, allowed IPs, endpoints, keepalive.
- Policy routing: dedicated routing table for VPN, kill-switch (drop non-tunnel traffic), split-tunnel.
- Firewall integration: nft rules for WG interface, forwarding between WG↔LAN.

Deliverables: A peer can connect to the router's WG endpoint and reach LAN/internet (per policy).
Exit criteria: Road-warrior client tunnels in, reaches LAN; kill-switch blocks traffic when WG is down.

Dependencies: Phase 2.

---

## Phase 5 — DPI & Fast-Path with eBPF/XDP
**Goal:** High-speed fast-path + lightweight deep packet inspection on XDP.

Key components:
- Toolchain: `libbpf`, `bpftool`, `clang` (eBPF frontend), CO-RE + BTF, libbpf skeletons via CMake.
- XDP program attached to WAN and/or LAN NICs (attached mode: native > generic).
- BPF maps: flow table, counters, DPI classification state.
- Userspace control: load/attach/detach, read maps, push config (allow/deny lists).
- Per-flow accounting + simple protocol classification (DNS, TLS SNI, QUIC, HTTP) via XDP/TC.
- TC classifier as complement for fields XDP can't reach (e.g. post-defrag).
- Performance: benchmark XDP path vs kernel path from Phase 3; fallback if XDP unsupported.

Deliverables: XDP program loaded with live stats; DPI classification visible via CLI/metrics.
Exit criteria: XDP attached on hardware NIC; flow counters match iperf traffic; latency no worse than kernel path.

Dependencies: Phase 1, 3 (needs measurable baseline to compare against).

---

## Phase 6 — Observability, Logging & Metrics
**Goal:** Operational visibility for a real router deployment.

Key components:
- Structured/audit logging: config changes, peer add/remove, firewall events, errors; rotation + retention.
- Metrics endpoint: Prometheus-style `/metrics` (or stats socket) — throughput, drops, conntrack, WG, XDP flows.
- Flow accounting: top talkers, per-device bytes (from XDP maps and/or nft counters).
- Health checks: WAN up/down, DNS, DHCP pool, WG handshake freshness, XDP attach state.
- Periodic status snapshots to the IPC API.

Deliverables: Live metrics + audit log queryable via CLI and Web UI.
Exit criteria: Metrics scrape returns non-empty series; health reflects real state.

Dependencies: Phases 1-5 (metrics span all subsystems).

---

## Phase 7 — Management Plane (CLI + Web UI)
**Goal:** Turnkey setup and day-to-day management from the laptop.

Key components:
- IPC protocol (Unix socket, JSON lines) shared by CLI and Web UI.
- CLI tool `routed-cli`: status, config get/set, ifaces, firewall, wg peers, perf profile, restart, logs.
- Embedded Web server (e.g. `libmicrohttpd`) serving static UI + JSON API.
- First-run setup wizard (web): set WAN mode, WiFi SSID/password, admin password.
- Auth: session or token for Web UI; bind management to LAN only.
- Config persistence + atomic apply + rollback on failure.

Deliverables: New user reaches web wizard, configures, and is online without touching files.
Exit criteria: Fresh image → browser wizard → working router in minutes; CLI covers all admin tasks.

Dependencies: Phases 1-6 (surfaces everything built so far).

---

## Phase 8 — Packaging & Distribution
**Goal:** A pull-and-run Docker image anyone can use.

Key components:
- Multi-stage `Dockerfile`: build stage (gcc/clang/cmake/libbpf) → slim Alpine runtime stage.
- Runtime image: only daemon binary + needed tools (`nft`, `hostapd`, `dnsmasq`, `wg`, `ip`, `ethtool`, `iw`).
- Entrypoint script: first-run detection, config bootstrap, cap/privilege notes, interface auto-detect.
- Volumes: `/etc/route-daemon` (config), `/var/lib/route-daemon` (state/keys), `/var/log/route-daemon`.
- `docker-compose.yml` example with `network_mode: host`, `cap_add: NET_ADMIN`, devices.
- Healthcheck, image labels, version embedding (`-DDAEMON_VERSION=...`).
- Publish to GHCR/Docker Hub with tags; signed images if desired.
- Docs: README quickstart, supported hardware, troubleshooting, security notes.

Deliverables: `docker pull ... && docker run ...` → instant router.
Exit criteria: A second machine pulls the published image and gets a working router with no manual build.

Dependencies: All prior phases.

---

## Cross-Cutting Concerns (apply throughout)
- **Safety:** atomic config apply with rollback; never leave firewall in an open state on failure.
- **Privilege model:** daemon needs `CAP_NET_ADMIN` + `CAP_NET_RAW` (privileged container acceptable for v1).
- **XDP/eBPF kernel requirements:** surface these clearly; graceful fallback to kernel path if unavailable.
- **Testing:** unit tests for parsers/rule-builders; integration tests via a namespace-based test net (no real NIC needed).
- **Reproducibility:** pin Alpine base + tool versions; deterministic builds where feasible.
- **Security:** no secrets in image; keys generated at first run; management plane LAN-only.

## Suggested Milestones
- **M1 (Phases 0-1):** "It routes." — Working kernel router over WiFi AP. *(most important milestone)*
- **M2 (Phases 2-3):** "It's secure and fast." — Hardened firewall + bufferbloat-free latency.
- **M3 (Phase 4):** "It VPNs." — WireGuard road-warrior + kill switch.
- **M4 (Phase 5):** "It's XDP-fast." — eBPF fast-path + DPI online.
- **M5 (Phases 6-7):** "It's manageable." — CLI + Web wizard.
- **M6 (Phase 8):** "It's shippable." — Published, pull-and-run image.

## Risks / Watch-outs
- XDP native attach may be unsupported on some WiFi drivers (esp. the AP NIC) → plan TC/generic-XDP fallback.
- Running a router inside Docker with `--network=host` blurs host/container boundary; document clearly.
- WiFi AP in container requires the host's `wlan` NIC + `hostapd` + regulatory domain; laptop WiFi chips vary widely in AP support.
- Kernel module availability (WireGuard, eBPF) depends on the host kernel, not the container.
