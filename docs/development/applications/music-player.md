<p align="right">
  <a href="music-player.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# musicdl Music Player Application

An online music player derived application on the `feature/music-player`
branch. It ports the Kuwo platform protocol from the open-source
[musicdl](https://github.com/CharlesPikachu/musicdl) Python tool onto the
device: chart browsing, keyword search, and MP3 streaming playback over
plain HTTP, with no PC relay. Provisioning is SoftAP + web page (see
[softap-web-provisioning.md](../engineering/softap-web-provisioning.md));
once online, the same HTTP service serves a LAN remote-control page because
the device itself has no text input.

## Screens and controls

| Screen | Content | Keys |
| --- | --- | --- |
| Boot | App name, ~1 s | auto-advance |
| Provisioning | AP name and `192.168.4.1` steps | OK long-press = home |
| Home | Five Kuwo charts (U+70ED/U+65B0/U+534E/U+98D9/U+77ED charts) | UP/DOWN move, OK open |
| Songs | 8 rows/page: index, title, artist, page indicator | UP/DOWN move/page, OK play |
| Playing | Marquee title, artist, EQ animation, progress, state line, volume bar | OK pause, UP/DOWN prev/next (long = volume), OK long = back |
| Settings | Volume / re-provision / about | list semantics |

List navigation clamps and does not wrap. Network state shows as a status
icon; a failed or empty fetch shows a per-state hint line instead of a
blank list.

## Architecture

Pure-logic modules in `main/` carry all decisions and are host-tested via
`./tools/validate.sh --static` (no ESP-IDF needed): `music_json.c` (SAX
parse of search/chart/resolve responses), `music_kuwo.c` (endpoint URLs and
scheme downgrade), `music_gbk.c` (UTF-8 ↔ GBK for the search endpoint),
`music_queue.c` (16-slot static queue), `music_player_sm.c` (playback
state machine), `music_net_sm.c` (network lifecycle), and
`music_ui_nav.c` (screen-key routing and paging).

ESP-bound modules: `music_wifi.c` (Wi-Fi lifecycle + fetch execution),
`music_http.c` (streaming GET, 2 KB chunks, reentrant), `music_decoder.c`
(esp_audio_codec MP3 glue, stereo downmix), `music_player.c` (single
player task: resolve → stream → decode → `bsp_audio_write`),
`music_httpd.c` (provisioning and remote pages), `ui_music.c` /
`music_fonts.c` (screens and the 16 px CJK font; see
[lvgl-chinese-fonts.md](../engineering/lvgl-chinese-fonts.md)).

Failure semantics live in the state machines, not the task: a resolve
failure returns to idle with one outer retry; stream/decode failures
re-resolve the same track and skip it after two consecutive misses; pause
keeps pulling the stream and discards PCM so resume is instant (bandwidth
is spent while paused — by design for v1).

## Audio pipeline and RAM

One synchronous task drives the whole pipeline; the I2S blocking write is
the pacing clock, and its DMA depth absorbs network jitter. v1 ships
without the planned 20 KB compressed ring buffer — add it only if device
testing shows underruns. Audio initializes lazily on first play so
provisioning and browsing never pay decoder or I2S memory. The app runs on
a no-PSRAM part: Wi-Fi ~50 KB + MP3 decoder ~30 KB + LVGL pool 48 KB +
task stacks; if the pool starves, the kill list is EQ/marquee animation,
then ring buffer, then chart page size.

`sdkconfig.defaults` keeps only the MP3 decoder from esp_audio_codec (all
other codec families are off; this cut the app by ~550 KB) and disables
Bluetooth. Note ESP-IDF regenerates a project-root `sdkconfig` from the
defaults only when absent; a stale root file silently overrides the
defaults — delete it after changing decoder options.

## Validation

- `./tools/validate.sh --static` — repository checks plus the host tests
  above (including the font-coverage gate over every string literal).
- `./tools/validate.sh` — adds the isolated firmware build and merged
  image check.
- Emulator smoke: boot, AP provisioning page, provisioning POST, online
  chart fetch, and the remote page all run under `esp-emu` with seeded or
  form-submitted credentials. I2S is not modeled: audio playback and
  `bsp_audio_init()` are device-only checks.

Known unverified on hardware: playback quality and underrun behavior,
captive-portal behavior on real phones, Kuwo reachability from home
networks, esp_audio_codec CPU/RAM on this part at 160 MHz, httpd while
streaming, GBK-edge song titles, and power draw.
