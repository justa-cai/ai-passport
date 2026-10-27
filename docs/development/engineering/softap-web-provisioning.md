<p align="right">
  <a href="softap-web-provisioning.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# SoftAP Web Provisioning

Use this pattern when an application needs Wi-Fi credentials from the user
and Bluetooth provisioning (see [wifi-provisioning.md](wifi-provisioning.md))
does not fit — for example when Bluetooth is disabled for RAM. The device
opens an open SoftAP, the phone joins it and submits credentials to a web
page, and the device tests them before committing. The reference
implementation lives in `main/music_wifi.c` + `main/music_httpd.c` on the
`feature/music-player` branch.

## Lifecycle

The host-testable state machine `main/music_net_sm.c` decides; the net task
executes. States: `AP_WAIT` (SoftAP up, provisioning page served) →
`STA_TESTING` (candidate credentials connecting) → `ONLINE` (has IP). Three
consecutive failures or a test timeout falls back to `AP_WAIT`; a boot with
stored credentials skips the AP entirely.

**Required:** treat submitted credentials as *candidates* until the STA
test yields an IP; only then overwrite the committed pair in NVS
(namespace `musicdl`, keys `ssid`/`pass`). **Prohibited:** writing
form-submitted credentials to NVS before the connection test succeeds — a
typo would strand the device on a broken boot loop.

## HTTP service

One `esp_http_server` instance swaps its whole handler set between modes;
the provisioning and remote pages never coexist. In AP mode (`192.168.4.1`
on the default `WIFI_AP` netif): `GET /` form page, `POST /api/wifi`
(urlencoded, body capped at 512 bytes, `%00` rejected), `GET /api/status`
polled by the page until `online`. The POST response may arrive torn down —
the AP→STA mode switch drops the phone's link mid-reply; the page must rely
on polling, not on the POST reply, to learn the outcome.

Form parsing must not mutate the body in place: terminating one field's
value by overwriting the following `&` orphans every later field (this bug
shipped once; the fixed `form_field` copies the decoded value out and keeps
the body `const`).

## Security scope

The SoftAP is open (the page carries no secrets) and the remote page is
unauthenticated on the LAN. Both are documented v1 trade-offs: anyone on
the LAN can control playback. Adding HTTP basic auth or a PIN shown on the
device display is the follow-up if a deployment needs it.

## Validation

Host tests cover the state machine transition table, including
candidate-does-not-overwrite-committed and fallback-to-AP
(`tests/test_music_net_sm.c`). Emulator validation covers the full flow:
AP up → page served → real POST → station joins the emulator AP → IP →
credentials committed → service swapped to the remote page.
