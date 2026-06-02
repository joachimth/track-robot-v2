# Captive Portal

## Purpose

When you join the robot's **TrackRobot-Setup** Wi-Fi access point, the captive
portal makes the control UI appear automatically — the same "sign-in" page most
public Wi-Fi networks show — instead of requiring you to type `192.168.4.1` into
a browser by hand.

## Context

The HTTP controller (`firmware/components/control/controller_http.c`) always
runs an access point at `192.168.4.1` (see [http-api.md](http-api.md) and
[safety-failsafe.md](safety-failsafe.md)). The captive portal adds two pieces on
top of that AP:

1. **A DNS server** that answers *every* query with `192.168.4.1`.
2. **An HTTP redirect** that sends any unknown path to `http://192.168.4.1/`.

Together these satisfy the connectivity-check probes that phones and laptops
fire on join, so the OS decides "this network has a captive portal" and pops the
web UI.

## Details

### DNS server

A FreeRTOS task (`dns_server_task`) binds a UDP socket to port 53 and replies to
each incoming query with a minimal DNS response containing a single **A** record
pointing at `192.168.4.1`:

- The original question section is echoed back unchanged.
- Header flags are set to `0x8180` (standard query **response**, recursion
  available, no error).
- `ANCOUNT` is set to 1; `NSCOUNT`/`ARCOUNT` are zeroed (any EDNS `OPT` record in
  the request is dropped so the reply stays well-formed).
- The answer uses a name-compression pointer (`0xC00C`) back to the question,
  `TYPE=A`, `CLASS=IN`, `TTL=60 s`, `RDLENGTH=4`, `RDATA=192.168.4.1`.

The task is started from `controller_http_init()` after the web server. It uses
lwIP sockets (`lwip/sockets.h`), so the `control` component now lists `lwip` in
its `REQUIRES`.

### HTTP redirect

The web server registers an `HTTPD_404_NOT_FOUND` error handler
(`captive_redirect_handler`) that returns `302 Found` with
`Location: http://192.168.4.1/` for any unmatched path. This catches the OS
probe URLs, e.g.:

| Platform | Probe URL |
|----------|-----------|
| Android  | `http://connectivitycheck.gstatic.com/generate_204` |
| iOS / macOS | `http://captive.apple.com/hotspot-detect.html` |
| Windows  | `http://www.msftconnecttest.com/connecttest.txt` |

Because DNS resolves those hostnames to `192.168.4.1`, the probe lands on our
server, gets a 302 to the UI, and the OS shows the captive-portal page.

## Example

```text
Phone joins "TrackRobot-Setup"
  -> DNS:  A? connectivitycheck.gstatic.com   ->  192.168.4.1
  -> HTTP: GET /generate_204                   ->  302 Location: http://192.168.4.1/
  -> Phone opens captive-portal view at http://192.168.4.1/  (the Control UI)
```

## Troubleshooting

- **No popup appears** — open `http://192.168.4.1/` manually; the UI still works.
  Some Android builds cache a previous "no internet" verdict; toggle Wi-Fi off/on.
- **Popup view is too small to drive from** — use the "open in browser" option in
  the captive-portal window, or navigate to `http://192.168.4.1/` in a full
  browser tab.
- **DNS task fails to bind port 53** — only one DNS server can own port 53; check
  the serial log for `Captive DNS: failed to bind port 53`. Nothing else in this
  firmware binds 53, so this would indicate a port conflict from custom code.
- **STA (home Wi-Fi) clients** are unaffected — they reach the robot through your
  router's DNS/IP and do not see the captive portal.

*Last updated: 2026-06-01*
