## ⚠️ Experimental Fork — mt32-pi-rt

**mt32-pi-rt** is a personal, **highly experimental fork** of the original project.

- It may be unstable, incomplete, or broken.
- Features are prototypes and may contain bugs.
- **Only tested on Raspberry Pi 3 (64-bit / AArch64)**. Other boards may or may not work.
- I do **not guarantee** that anything works as expected.
- I do **not provide support** for this fork.

If you are looking for a stable and fully functional version, please use the original project:
👉 https://github.com/dwhinham/mt32-pi

All credit goes to the original author, who did an outstanding job creating and maintaining this project.

### Purpose of this fork

This fork exists purely for experimentation, testing new ideas, and personal learning.
Expect things to break — that's part of the process.

---

## 🔀 mt32-pi-rt — Extended Features

mt32-pi-rt adds a full web-based control interface, a three-engine MIDI mixer/router system, a built-in MIDI file sequencer, real-time audio/MIDI monitoring, and a new Yamaha FM synth path based on ymfm (OPL3/OPL2) on top of the original mt32-pi.


### System architecture

#### Core assignment

| Core | Responsibility | Cadence |
|------|---------------|----------|
| 0 | MIDI polling, parsing, routing, sequencer tick, network, control | Continuous |
| 1 | LCD / MiSTer status display | ~16 ms |
| 2 | Audio render (hot path — synth + mixer) | ~11.6 ms (256 frames @ 48 kHz) |
| 3 | currently available | — |

No mutexes on the audio hot path. Inter-core communication uses `volatile` flags and lock-free ring buffers.

#### Component overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              CORE 0  (main loop)                        │
│                                                                         │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐                │
│  │  MIDI Input  │   │  Sequencer   │   │  Web Keyboard│                │
│  │  Serial/USB/ │   │  (SMF Type   │   │  (REST API)  │                │
│  │  GPIO/Apple  │   │   0 & 1)     │   │              │                │
│  └──────┬───────┘   └──────┬───────┘   └──────┬───────┘                │
│         │                  │                  │                         │
│         └──────────────────┴──────────────────┘                        │
│                            │                                            │
│                    ┌───────▼────────┐                                   │
│                    │  CMIDIParser   │  running status, SysEx            │
│                    └───────┬────────┘                                   │
│                            │                                            │
│                    ┌───────▼────────┐                                   │
│                    │  CMIDIRouter   │  per-channel route, remap,        │
│                    │                │  CC filter, layering              │
│                    └──┬─────────┬──┘                                   │
│                       │         │                                       │
│             ┌─────────┬───────────────┬─────────┐                     │
│             │         │               │         │                     │
│    ┌────────▼───────┐ │   ┌──────────▼──────┐  │  ┌───────────────┐  │
│    │  CMT32Synth    │ │   │ CSoundFontSynth │  │  │  CYmfmSynth   │  │
│    │  (munt / MT-32)│ │   │  (FluidSynth)   │  │  │ (ymfm OPL3/2) │  │
│    └────────┬───────┘ │   └──────────┬──────┘  │  └───────┬───────┘  │
│             │         │              │         │          │          │
│             └─────────┴──────┬───────┴─────────┴──────────┘          │
│                               │                                        │
│                      ┌────────▼─────────┐                              │
│                      │    CAudioMixer    │  volume, pan, solo,         │
│                      │                   │  per-channel & per-engine   │
│                      └────────┬─────────┘                              │
└───────────────────────────│───────────────────────────────────────────┘
                            │  (shared buffer, written Core 0 / read Core 2)
┌───────────────────────────│───────────────────────────────────────────┐
│                    CORE 2 (audio render)                               │
│               ┌───────────▼───────────┐                               │
│               │   HAL audio output     │  PWM / I²S / HDMI @ 48 kHz  │
│               └───────────────────────┘                               │
└───────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                        NETWORK SERVICES  (Core 0)                       │
│                                                                         │
│  ┌────────────┐  ┌─────────────────┐  ┌────────────┐  ┌────────────┐  │
│  │ CWebDaemon │  │CWebSocketDaemon │  │ AppleMIDI  │  │  UDP MIDI  │  │
│  │ HTTP:80    │  │  WS:8765        │  │  RTP MIDI  │  │  (opt.)    │  │
│  │ 6 pages +  │  │  JSON status    │  │            │  │            │  │
│  │ REST API   │  │  ~250 ms push   │  │            │  │            │  │
│  └────────────┘  └─────────────────┘  └────────────┘  └────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

#### MIDI data flow (Core 0)

```
Serial / USB / GPIO IRQ
        │
        ▼
  Ring buffer (lock-free)
        │
        ▼
  CMIDIParser ──► OnSysExMessage ──► SysEx handler (synth params, custom)
        │
        ▼
      CMIDIRouter
            ├── channel route table  (MT-32 | FluidSynth | OPL3 | Layered | Off)
            ├── channel remap        (source ch → target ch per engine)
            ├── CC filter            (block CC per engine)
            └── layering flag        (duplicate to multiple engines)
                        │              │               │
                        ▼              ▼               ▼
      CMT32Synth     CSoundFontSynth   CYmfmSynth
      (munt)         (FluidSynth)      (ymfm OPL3/OPL2)
```

#### Key source files

| File | ~LOC | Role |
|------|------|------|
| `src/mt32pi.cpp` | 2 500 | Main orchestrator: init, loop, MIDI, sequencer, network, state |
| `src/net/webdaemon.cpp` | 3 000 | HTTP server — 6 pages + full REST API |
| `src/net/websocketdaemon.cpp` | 400 | WebSocket — JSON status push every 250 ms (configurable) |
| `src/audiomixer.cpp` | 600 | Multi-engine mix: volume, pan, solo, per-channel gain |
| `src/midirouter.cpp` | 500 | Per-channel routing across MT-32, FluidSynth, and OPL3; remapping, CC filtering, layering |
| `src/synth/ymfmsynth.cpp` | 700 | Yamaha FM engine wrapper for ymfm with OPL3/OPL2 bank support |
| `src/fluidsequencer.cpp` | 800 | SMF player wrapping `fluid_player_t` |
| `include/config.def` | — | Single-source config: one `CFG()` line generates INI parser + default |
| `tests/` | — | doctest suite — 253 tests / 1 689 assertions, host-native |

### Web interface

Served on port 80. All pages share a persistent WebSocket connection (port 8765) for live status updates.

| Page | URL | Functionality |
|------|-----|---------------|
| **Status** | `/` | Active synth engine, current SoundFont / ROM, sequencer state, MIDI activity summary, reboot button |
| **Sound** | `/sound` | Switch synth engine; browse and load SoundFont / ROM files; mark favourites; audition SoundFonts before applying; MT-32 / OPL3 runtime parameters (reverb, chorus, partial count, chip mode) |
| **Sequencer** | `/sequencer` | Browse and play SMF files; transport controls (play, stop, pause, prev, next); seek bar with tick position; loop and auto-next toggles; tempo multiplier; playlist queue with shuffle and repeat modes |
| **Mixer** | `/mixer` | Per-engine volume (0–100%) and pan sliders; solo; VU meters; MIDI router matrix (per-channel routing to MT-32 / FluidSynth / OPL3, channel remap, CC filter, layering); save/load router preset |
| **Monitor** | `/monitor` | Real-time 16-channel MIDI activity meters; piano-roll keyboard with live note highlighting; virtual keyboard for sending note-on/off; raw MIDI sender; SysEx log with clear |
| **Config** | `/config` | Edit all `mt32-pi.cfg` keys in a form and save to SD card; WiFi SSID/country configuration |

**REST API** — every UI action maps to an `POST /api/…` or `GET /api/…` endpoint usable from scripts or external tools:

```
GET  /api/status                  — full system status (JSON)
GET  /api/runtime/status          — live synth parameters
POST /api/runtime/set             — change a synth parameter at runtime
POST /api/sequencer/{play,stop,pause,resume,next,prev,seek,loop,autonext,tempo}
GET  /api/sequencer/{status,files}
POST /api/mixer/{set,preset}      — set volume/pan/routing, save/load preset
GET  /api/mixer/status
POST /api/router/{save,load}
POST /api/midi/{note,raw}         — send MIDI note or arbitrary bytes
GET  /api/midi/log                — SysEx / MIDI event log
POST /api/playlist/{add,remove,clear,up,down,shuffle,repeat,play,add-all}
POST /api/recorder/{start,stop}   — MIDI recording to .mid on SD card
GET  /api/soundfont/info          — metadata for a SoundFont by index
POST /api/config/save             — persist config to SD
GET  /api/wifi/read               — read WiFi credentials
POST /api/wifi/save               — update WiFi credentials
POST /api/system/reboot           — reboot the Pi
GET  /health                      — liveness probe (returns 200)
```

### New features summary

| Feature | Description |
|---------|-------------|
| **Web UI** | 6-page browser interface: Status, Sound, Config, Sequencer, Mixer, Monitor — dark/light mode, toast notifications, responsive CSS |
| **ymfm / OPL3 engine** | Third synth engine based on ymfm, selectable as OPL3 or OPL2, with runtime status, mixer integration, and web UI controls |
| **MIDI Router** | Per-channel routing to MT-32, FluidSynth, and/or OPL3, with remapping, CC filtering, and visual routing diagram |
| **Audio Mixer** | Independent volume (0–100%) and pan for each synth engine, including OPL3; card-grid UI with smart VU meters |
| **Sequencer** | Built-in SMF Type 0/1 player with loop, auto-next, pause/resume, prev/next, seek memory, and loading indicator |
| **Playlist** | Queue with shuffle and repeat modes |
| **MIDI Monitor** | Real-time 16-channel meters, piano roll, virtual keyboard, SysEx viewer |
| **MIDI Recording** | Record incoming MIDI to `.mid` files on the SD card |
| **MIDI Thru** | Universal MIDI Thru — all physical inputs (Serial/USB/GPIO) forwarded to UART TX |
| **OSC Control** | UDP Open Sound Control receiver with per-engine volume/pan and web UI integration |
| **Audio Effects** | Post-mix effects chain: parametric EQ, reverb, and limiter |
| **Audio Profiling** | Per-component timing stats streamed via WebSocket for performance monitoring |
| **NEON SIMD** | ARM NEON-optimized audio mixer hot path and float→int conversion in `AudioTask` |
| **WebSocket** | Live JSON status stream on port 8765 (configurable port and push interval); skips unchanged frames |
| **HTTP Keep-Alive** | Persistent connections via Circle httpdaemon patch for faster page loads |
| **Live control** | Change all synth parameters at runtime via web UI, REST API, or OSC messages |
| **SF3 support** | Ogg Vorbis-compressed SoundFonts via stb_vorbis (no external libs) |
| **DLS support** | Native DLS soundbank loader (e.g. Windows `gm.dls`, `RLNDGM2.DLS`) |
| **SoundFont Favorites** | Mark preferred SoundFonts via localStorage in the browser UI |
| **SoundFont Preview** | Audition SoundFonts from the web UI before applying |
| **Unit tests** | doctest suite covering MIDI router, audio mixer, MIDI parser, config parser, AppleMIDI, OSC, playlist, and sequencer (253 tests, 1 689 assertions) |

### Synth engines

| Engine | Backend | Typical use | Notes |
|--------|---------|-------------|-------|
| **MT-32** | Munt 2.7.3 / `CMT32Synth` | Roland LA synthesis, MT-32/CM-32L content | Requires MT-32 ROMs |
| **SoundFont** | FluidSynth 2.5.3 / `CSoundFontSynth` | General MIDI / GS playback | Supports SF2, SF3, and native DLS in this fork |
| **OPL3** | ymfm / `CYmfmSynth` | AdLib / Sound Blaster FM style playback | Can run in OPL3 or OPL2 mode; bank file optional, built-in GM fallback available |

#### ymfm configuration

The ymfm engine is configured in the `[ymfm]` section of `mt32-pi.cfg`:

```ini
[ymfm]
chip=opl3
bank_file=
```

- `chip=opl3` enables 18-voice stereo OPL3 mode.
- `chip=opl2` forces 9-voice OPL2 compatibility mode.
- `bank_file=` can point to a `.wopl` or `.op2` bank relative to the SD root.
- Leaving `bank_file` empty uses the built-in GM fallback bank.

### Building

> ⚠️ **Only tested on Raspberry Pi 3 (64-bit AArch64).** Other boards are untested and may not work with this fork.

```bash
# Set cross-compiler path (adjust to your toolchain location)
export PATH="$HOME/arm-gnu-toolchain-14.3.rel1-x86_64-aarch64-none-elf/bin:$PATH"

# Build for Raspberry Pi 3 (64-bit)
make BOARD=pi3-64 -j$(nproc)

# Deploy via FTP (once the Pi is running)
curl -T kernel8.img ftp://<pi-ip>/kernel8.img --user mt32-pi:mt32-pi
```

> Pre-built releases are available on the [Releases page](../../releases). Tag format: `vX.Y.Z-rt`.

### Running the test suite

```bash
cd tests && make clean && make run
```

The test suite uses [doctest](https://github.com/doctest/doctest) and covers `CMIDIRouter`, `CAudioMixer`, `CMIDIParser`, config parser, `CAppleMIDI`, OSC daemon, playlist, and FluidSynth sequencer (253 tests, 1 689 assertions). Tests compile natively — no cross-compiler needed.

### Branch conventions

- `main` — stable, always CI-green; never push directly.
- `feat/<short-name>` — one branch per feature or fix; open a PR to merge.

---

## 🔧 Recent improvements

**Security:** Blocked FTP PORT bounce attacks; JSON injection fix in playlist REST responses.

**Correctness:** Fixed buffer underflow in FTP, off-by-one in WebSocket framing, undefined behaviour in `optional.h`, and a misleading operator-precedence bug in the 24-bit audio clamp constant.

**Thread safety:** Cross-core shared mixer state (`volatile float`) replaced with `__atomic_*` builtins; race condition on sequencer load flag between Core 0 and Core 2 eliminated.

**Performance:** Sequencer seek history rewritten as a lock-free ring buffer (removes `memmove` on every seek); MIDI recording buffer pre-allocated at init; NEON SIMD helpers extracted for reuse across the audio hot path.

**OPL3:** Pitch bend implemented — `UpdateVoiceFNumber()` recalculates F-Number with fractional semitone interpolation, preserving KEY ON bit on register writes.

**Test suite:** Expanded from 125 to **253 tests · 1 689 assertions**, adding coverage for AppleMIDI, OSC, playlist, sequencer, and edge cases across all existing modules.

---
