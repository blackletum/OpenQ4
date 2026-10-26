# Audio and voice evidence — 21 October 2026

This is a supporting evidence record for AUD-01 through AUD-07 in the
[roadmap](roadmap.md#audio-and-voice), not a separate work plan. The seven items
remain open until their own acceptance gates are satisfied.

## Tested implementation

Windows x64, MSVC debugoptimized, `builddir`, staged through the project Meson
wrapper into `.install`; base revision `cbe967f2b61935da283a873a731fe975926cf214`
plus the audio changes described here (the engine log reports
`0.14.000-dev+g4085caf9.dirty`, the branch tip before its last test-only and
documentation commits). Every runtime report named below was rerun on
21 October on these final staged binaries:

| Binary | SHA-256 |
| --- | --- |
| `openQ4-client_x64.exe` | `b21f7cd6243a582269d11a60135b45a078891f0af5fabdd750369a278605a1b8` |
| `openQ4-ded_x64.exe` | `dd7e5ef0691b1506be716e55cf8597eac814a6ac3f2d7f44cd51320eaefe3759` |
| `baseoq4/game-mp_x64.dll` | `4cc4c11d3fa510813b2e671d94db9a74d1910e730d3f53814274fd96af3b17fd` |
| `baseoq4/game-sp_x64.dll` | `1773b0729b1701ede1f9db872cc58b248265305d6afde721ec7f5fb4009b96b9` |
| `baseoq4/pak0.pk4` | `de78cd747b9e235e958417a821615d38aa26236514da4136656be45c146a6deb` |
| `OpenAL32.dll` (OpenAL Soft 1.25.2) | `c7905985c3a48cf88b0365879501cbe7b65b3ccf47ff1be3937b0edc079ba6c6` |

Earlier report directories under `.tmp/audio` remain as development history;
only the `final3-*` reports below, and the `final-*` OpenAL loopback reports,
count as evidence for these binaries.

Tests use the installed Steam 1.4.2 PK4s and the user's read-only Awakening
`q4xbase` content under the main checkout's `.home`. Writable profiles, generated
tones, engine screenshots, OpenAL WAV output and logs stay in this worktree's
`.tmp/audio`. No physical microphone, host input injection, desktop capture or fullscreen
launch is used. Another worktree can run concurrently; the harness explicitly
allows multiple instances and never stops a process it did not launch.

## Dialogue volume and pause

`s_dialogueVolume` is archived, defaults to 1 and accepts 0–1. The Audio page has
a slider and numeric value with labels in all twelve shipped language tables.
The player guide describes the setting. The central sound-channel mix applies
it to the sample actually playing, including a leadin/loop transition:

- Explicit `SSF_IS_VO` and `SSF_VO_FOR_PLAYER` speech, including NPC lip-sync.
- Canonical `sound/vo/` and localized `sound/vo_<language>/` samples, independent
  of the calling NPC, script, BSE source or radio channel.
- Music routing has precedence. Neither `SND_CHANNEL_VOICE` (also used by the
  Dark Matter weapon) nor broad `SSF_VO` (also applied to GUI sounds) alone
  classifies an effect as dialogue.

No new sound-save fields are introduced. Master gain, authored distance,
occlusion, reverb and voice priorities keep their existing roles. Mixed speech
and music in a single recording cannot be separated by this control.

`tools/tests/audio_dialogue.py` enters `game/airdefense1` or Awakening's
`game/m01_stranarus_trench1`. Generated 440 Hz speech and 880 Hz effect fixtures
overlap on independent channels. Actual OpenAL source reports show speech gains
1, 0.25, muted, then 0.6 while the effect stays at 1. The 0.6 ratio survives
pause/resume, time scale 0.5 (pitch also 0.5), `s_restart` and save/load. The
shared campaign configuration archives 0.6. English and Turkish text with
English audio fallback pass. OpenAL renders to WAV; the screenshot command
captures the Audio page through its normal semantic menu actions.

Final reports: `.tmp/audio/final3-dialogue-stock/result.json` (28 checks:
compressed fixtures, 5.1 output, 16 MiB decode and 192 MiB aggregate budgets)
and `.tmp/audio/final3-dialogue-awakening/result.json` (25 checks: PCM fixtures,
5.1, the same budgets), plus the Turkish-text/English-audio fallback run
`.tmp/audio/final3-dialogue-turkish/result.json` (23 checks, stereo). The
English Audio-page capture was visually inspected. This proves fixture isolation in running maps, not a
complete listening pass over every authored NPC/BSE/script/cinematic scene.

## Decode admission before streaming

Vorbis admission now bounds compressed input plus pending PCM in bytes, including
synchronous fallback. `s_decodeMemoryMB` defaults to 64 MiB on all platforms;
the existing 48-job cap remains. Admission joins older work before allocating
more. A reservation remains owned until join/cancellation even if its worker
has finished. Failed probes and cancellation release it. Ogg files lacking a
usable length use a fixed 16 KiB counting buffer and rewind before allocating
the final PCM, replacing the unbounded doubling helper.

`listSounds` additionally reports retained CPU sample data, queried OpenAL
buffer sizes, conservatively reserved compressed input, their aggregate and
the admission peak/wait count. At the final gameplay probes:

| Run | Transient peak | Admission limit | CPU PCM | OpenAL PCM |
| --- | ---: | ---: | ---: | ---: |
| Stock map, Turkish text | 30,087 KiB | 64 MiB | 0 KiB | 198,184 KiB |
| Awakening m01 | 47,635 KiB | 64 MiB | 0 KiB | 265,340 KiB |

Both finish with zero outstanding reservation and no budget rejection. A
deliberately lower 16 MiB test exercised backpressure but rejected three long
Awakening tracks; it is not evidence of an intact scene mix. The harness now
requires zero rejections. Header inventory finds no installed Ogg above 64 MiB
input+PCM: the largest stock pair is 40,333,330 bytes and the largest Awakening
pair 47,258,520 bytes. Both exceed 32 MiB, so a 32 MiB mobile default would be a
compatibility regression before streaming exists.

These earlier measurements excluded stb decoder metadata, non-Vorbis loading,
resident CPU/OpenAL data, driver internals and voice queues. The follow-up below
adds fixed decoder arenas and compressed streaming. Android low-memory
qualification remains open. `tools/tests/audio_decode_budget.py` executes production admission,
reservation, join and cancellation methods, including failed decodes and a
runtime budget reduction.

## Compressed streaming

Long Ogg samples now retain their metadata and 60 Hz amplitude envelope, with no
complete compressed file or decoded PCM resident. Above 512 KiB decoded size,
each active voice opens its own VFS/PK4 stream with a 64 KiB read cache, fixed
2 MiB Vorbis arena and three PCM queue buffers (4,096 frames when first
measured; now about a third of a second each, see below). The separate
`s_streamMemoryMB` limit defaults to 32 MiB, reserving 2,304 KiB per voice including
scratch, queued PCM and conservative VFS bookkeeping. Stop and purge release
decoders, file handles and queue buffers. Short-track decode jobs now include
their fixed 2 MiB arenas in `s_decodeMemoryMB` reservations as well.

`tools/tests/audio_streaming.py` generates original mono/stereo Ogg fixtures at
22.05/44.1/48/96 kHz (ffmpeg or `imageio_ffmpeg` required), then compiles the real
bounded reader and production queue transitions. Full and streamed PCM compare
exactly, including sample seeks, independent overlapping cursors, short reads,
EOF, compressed leadin/loop, mixed static/streamed transitions and a self-loop
from a restored offset. Upload failure releases the decoder. A stopped streaming
source requests recovery at the game's sound clock instead of replaying stale
PCM. `audio_stream_budget.py` executes production admission/ownership through
saturation, stop/retry, missing input, decoder failure and a reduced live budget.
`audio_payload_restore.py` also proves compressed streams never restore full PCM.

The final Windows stock and Awakening runs use `audio_dialogue.py --compressed
--decode-memory 16`, enter the same maps as the earlier dialogue tests, and pass
all 24 checks. They exercise mute, pause, time scale 0.5, restart and save/load;
actual rendered 440/880 Hz tone amplitudes exceed 0.05 in each output. The stock
save-game stall also exercises underrun recovery at a nonzero dialogue offset.

| Final run | Decode peak / limit | OpenAL sample PCM | Stream peak / limit | Reported aggregate payload bound |
| --- | ---: | ---: | ---: | ---: |
| Stock airdefense1 | 15,447 / 16,384 KiB | 118,853 KiB | 9,216 / 32,768 KiB | 128,242 KiB |
| Awakening m01 | 16,352 / 16,384 KiB | 148,274 KiB | 4,608 / 32,768 KiB | 153,055 KiB |

Both finish with zero outstanding decode reservation and no rejected streams.
The aggregate includes sample CPU/OpenAL data, envelopes, pending input/arenas
and stream reservations; it excludes driver internals and other engine objects.
Reports: `.tmp/audio/stream-stock-final/result.json` and
`.tmp/audio/stream-awakening-final/result.json`. Client SHA-256:
`01a735014944a4efb233b3b4d57a8de52eabb6d4a36a697f68a16a64b1f7fe81`.
On the final binaries, the compressed stock run
`.tmp/audio/final3-dialogue-stock/result.json` again streams both fixtures with no
rejection (decode peak 14,577 of 16,384 KiB, stream peak 9,792 of 32,768 KiB).
The reports retain runtime and harness identities. The `stream-awakening` run
used a stock map in the expansion game directory; only the final m01 run is
counted as Awakening scene coverage.

The bundled MIT/public-domain stb_vorbis receives a small read/seek/tell callback
extension, with matching canonical/external copies and normal/no-stdio/push-only
compilation checked. No extracted audio is added to Git. Streaming currently
requires usable length metadata and supports tracks up to one hour. Its initial
envelope pass is bounded but synchronous; load-time profiling, mixed-format
leadin/loop behavior, an enforced joint residency limit including WAV/RoQ and
Android/device breadth remain open under AUD-03. The decoder and queue limits
alone are not a total audio-memory guarantee.

### Queue depth across long frames, 22 October 2026

On the merged A+B build (`9255c199c`) `audio_dialogue.py --compressed` failed
4 of 4 runs with about 25 underruns each, and `audio_memory_runtime.py` failed
`retry_and_restarts_play_both`. Earlier passing reports on the same audio code
came from before 23:51 on 21 October. From then on, the laptop sat in Modern
Standby with the display off (Kernel-Power event 506, no 507 afterwards), and
every failing run fell in that period. With `com_speeds 1` the hidden GL window
ran at p50 139 ms with 49 frames over 200 ms and a 2.5 s maximum; the time was
outside the game, front end and back end, in presentation. With
`r_swapInterval 0` the median returned to 17 ms, but periodic ~450 ms present
stalls remained. Vulkan, which renders hidden windows offscreen and never
presents (90ed5029), paced at p50 17 ms with no stalls apart from the first
frame, `s_restart` and save/load, and the same dialogue run passed. CPU load
stayed at 8-30%, with no build processes and over 36 GB of free commit. The
~500 ms CPU p99 in the same day's GL stock baseline has the same cause.

The trigger was environmental, but the failures exposed a real limit. Every
bounded queue (streamed Ogg, CPU-resident PCM without an uploaded buffer,
centre-routed mono) held three 4,096-frame buffers and was refilled once per
main-thread frame. That is 512 ms at 24 kHz and 279 ms at 44.1 kHz. Each
underrun followed a frame of about 450 ms or longer, so a single loading hitch
of that size cut out music or dialogue until the channel restarted it at its
clock. The 64 MiB fallback WAV in `audio_memory_runtime.py` ran dry the same way
before its probe.

Queue buffers are now sized by duration within the bytes the reservations
already count (`src/sound/SoundQueue.h`). A buffer holds about a third of a
second, up to one 4,096-frame 5.1 block (48 KiB). Mono therefore keeps about
1.0 s queued at rates up to 48 kHz, stereo 1.0 s at up to 24 kHz and at least
768 ms at 44.1/48 kHz. Centre-routed mono uploads 5.1 frames, so eight routed
voices at a time take deep slots of up to 16,384 frames per buffer for the
same depth. Further routed voices keep 4,096 frames. The fixed 20 MiB
reservation now also asserts the slots' extra bytes and their routing scratch
(about 19.1 MiB in total). Stream reservations, aggregate admission and the
reported memory numbers are unchanged. The memory runtime still reports
214,246 KiB reserved through both restarts. `audio_streaming.py` now proves the
one-second depth, the per-buffer byte bounds, continuity across multi-read
buffers, and that deep slots are bounded, reused and returned. Against the
previous voice code it fails.

The post-fix runs were made in the same display-off state, with machine load
recorded for each run (`.tmp/mrt/*.jsonl` in that worktree):

| Suite | Before | After |
| --- | --- | --- |
| `audio_dialogue.py --compressed --speakers 6 --decode-memory 16 --audio-memory 192` | 0/2 (25 and 30 underruns) | 3/3 |
| `audio_dialogue.py --compressed` (2 speakers) | 0/4 | 3/3 |
| `audio_memory_runtime.py` | 0/1 | 3/3 |

A frame-timed run of the fixed build had the same stall profile as the failing
one (p50 172 ms, 15 frames over 300 ms, two over 1 s) and logged no underruns,
against 30 before. A hitch longer than the queue still recovers at the channel's
sound clock, as before. Refilling from a worker thread was not attempted: voice
queue state has no locking and is touched only on the main thread.
On the final build (branch tip `ec89ff31c`, client SHA-256 recorded in each
report) the native Meson suite passed 276 of 276, and so did the audio,
sound-settings, recovery, payload-restore, portability and validation-hardening
static tests.

## Mod room rolloff

The voice now sets `AL_REFERENCE_DISTANCE` from the authored minimum distance,
in the same map units as the source. This operates independently of the optional
source-radius extension. A zero/invalid minimum has a small positive reference;
the base setter already sanitizes nonfinite/negative input. The dry path still
uses engine attenuation and `AL_ROLLOFF_FACTOR 0`.

`tools/tests/audio_mod_reverb.py` renders deterministic noise with the staged
OpenAL provider through `ALC_SOFT_loopback`, measuring PCM rather than merely
checking API calls. With minimum 100 and distance 200, room rolloff 1 gives wet
RMS 0.0068158555, exactly half the near-reference RMS 0.0136317110. The old
one-unit reference gives 0.0000681586. Scaling distance and reference together
leaves the result unchanged. Stock room rolloff 0 and dry RMS are unchanged.
Final report: `.tmp/audio/final-mod-reverb/result.json` (6/6).

The [EFX guide](https://openal-soft.org/misc-downloads/EffectsExtensionGuide.pdf)
and the pinned provider's `alc/alu.cpp` define the reference-distance behavior.
The local retail reconstruction also sets the source minimum, but is a
structural reference with known reconstruction errors, not standalone retail
parity evidence. No external implementation was copied. Global distance
defaults, 5.1 centre placement and full retail audible-distance parity were not
changed or qualified by this measurement.

## Aggregate payload admission

`s_audioMemoryMB` now defaults to 256 MiB (range 32–2048). One byte ledger
admits sample CPU data, compressed input, pending decoder arenas, amplitude
envelopes, OpenAL sample uploads and stream reservations together. A fixed
20 MiB conservative allowance covers all 96 hardware voices' three 48 KiB
queue buffers (one 4,096-frame 5.1 block each), the deep centre-routed slots,
voice-chat rings and scratch. Stream reservations also include their
queues; this intentionally overcounts them. This is a managed-payload ceiling,
not a provider-process/RSS limit: driver internals, resource metadata and general
filesystem caches are outside its claim.

Admission joins pending work and may reclaim inactive, unreferenced old-map
samples. Current-map, never-purge and live-channel references remain. Worker
completion can release compressed input; decoder arenas stay reserved until
joined or cancelled. WAV/ADPCM allocations and temporary decode overlap count
before allocation. RoQ now scans chunk headers, then decodes audio directly into
one admitted PCM buffer through 4 KiB input scratch, skipping video data. It no
longer loads the complete video or grows a second PCM container. Envelope
allocations and failed uploads release reservations on all tested exits.

A failed static upload retains CPU PCM for three bounded fallback buffers;
each holds at most one 4,096-frame 5.1 block of bytes (about a third of a second
of mono or stereo PCM since 22 October) for ordinary PCM as well as centre
routing. Failed sample loads stay retryable and never become cached replacement
beeps. Exhaustion can omit new audio when it cannot fit; it does not evict active
sounds. Reducing the budget during playback blocks new admissions without
revoking already admitted data, so existing usage and historical peak can
exceed the newly selected limit until data is released. Restart/map changes
release owned allocations; `listSounds` reports current/peak reservation and
refusal count. Its decoded-size totals now use 64-bit arithmetic.

`audio_memory.py` compiles the production admission/allocator, envelope ownership,
RoQ loader and ADPCM decoder. It checks arithmetic overflow, exact saturation,
concurrent worker release, allocator failure, budget reduction, mono/stereo PCM,
chunk boundaries, truncated/mixed data, failed admission and retry. Existing
production decode-join/cancel, stream lifetime and PCM-queue tests pass, now
including aggregate stream refusal and resident fallback queue continuity.

Loaded-map results at a 192 MiB aggregate budget and a 16 MiB decode budget:

| Scene | Peak managed reservation | Refusals | Scope |
| --- | ---: | ---: | --- |
| Stock `game/airdefense1` | 149,475 KiB | 0 | Compressed dialogue/effect, 5.1, mute, pause, half speed, restart, save/load |
| Awakening `game/m01_stranarus_trench1` | 180,190 KiB | 0 | PCM dialogue/effect, 5.1 and the same lifecycle checks |

Reports: `.tmp/audio/final3-dialogue-stock/result.json` and
`.tmp/audio/final3-dialogue-awakening/result.json` (harness SHA-256
`8fc4b57342564c648d4626e86b4c38326a07f3cdba515c5f0c2fe5167a442b40`); each
identifies its exact engine, modules, staged content and harness. A preceding stock run was interrupted by a
natural campaign radio line and failed its fixture gain checks. The mixer harness
now uses a real saved fixture entity with its own radio channel; the campaign
continues running and cannot replace the fixture on the player's emitter.

`audio_memory_runtime.py` then loads a real map, reduces the budget to 32 MiB and
requests an original 64 MiB WAV. Existing audio keeps playing; the refused sample
remains unloaded. Restoring 256 MiB permits its CPU payload but refuses the extra
full OpenAL copy. Actual rendered output contains both fixture tones through the
bounded fallback. Current reservation is 214,246 KiB before and after two sound
restarts, with no replacement beep or reservation growth. The final report
`.tmp/audio/final3-memory-pressure/result.json` passes all 13 checks, including
that a budget refusal is no longer reported as corrupt ADPCM/PCM media.

These tests do not establish whole-process memory bounds, Android OS low-memory
notification handling, all-map residency, loading-time budgets or physical-device
qualification. AUD-03 remains open for those gates and lengthless streaming.

## Centre-channel placement

`SoundSurround.h` routes eligible mono samples through bounded 5.1 queue
buffers (4,096 frames, or about a third of a second for the eight voices holding
a deep slot since 22 October) only when the provider reports a centre-capable output and supports
multichannel/direct-channel playback. Explicit `SSF_CENTER` uses fraction 1;
global/radio VO uses `s_centerFractionVO` (default 0.75). Centre amplitude is
the fraction; the remaining power is shared equally by FL/FR/SL/SR. LFE stays
silent. Positional NPC speech retains the original mono directional path.
Stereo and active HRTF also retain that path. Streaming reservations now include
the larger 5.1 conversion scratch and queues; the earlier streaming reports
above retain their original binary and reservation measurements.

`tools/tests/audio_center.py` passes 26 checks, including every signed 16-bit
sample at four fractions and real OpenAL loopback PCM on 5.1, 6.1, 7.1, quad,
stereo and HRTF outputs. Its extension gating matches the engine, including
`AL_SOFT_direct_channels_remix`. At fraction 1 on 5.1, centre RMS is 0.08838810
and every other channel is zero. At 0.75, centre RMS is 0.06629362, four
surrounding channels are 0.02923290 and LFE is zero. On 6.1 and 7.1 the centre
stays exclusive for explicit sounds, VO keeps the configured fraction with unit
power and LFE stays silent. Quad (no centre speaker) keeps the original path,
and stereo and active-HRTF comparisons have identical PCM hashes. The production queue
tests cover centre-converted static and streamed leadin/loop combinations,
partial static buffers and underrun recovery.

Loopback report: `.tmp/audio/final-center-layouts/result.json` (26/26). The
gameplay reports `.tmp/audio/final3-dialogue-stock/result.json` (28 checks) and
`.tmp/audio/final3-dialogue-awakening/result.json` (25 checks) pass with six-channel
OpenAL wave output and a 16 MiB decode budget. Both measure stronger 440 Hz
dialogue in FC than the surrounding channels, with an independent 880 Hz effect,
and cover gain/mute, pause, slow motion, restart and save/restore. The harness
compares channels within the windows where the speech fixture is loud: the
median centre-to-surround ratio is 2.27 in Awakening's quiet scene (the
theoretical value at fraction 0.75) and 1.72 in airdefense1, whose own ambience
adds 440 Hz energy to a surround channel. An earlier whole-recording peak
comparison failed a correct mix on a fast run for that reason.

These are rendered-PCM checks, not physical-speaker listening tests. Live output
layout changes, physical devices and explicitly centred positional mod sounds'
wet-send behavior remain unqualified.

## Multiplayer voice

The former disabled voice path is implemented. Engine/game API **53** appends
dedicated voice callbacks; matching SP/MP modules are required. The existing
gameplay protocol stays at 2.41, with explicit `si_voiceProtocol` and
`ui_voiceProtocol` capability opt-in. Unsupported peers do not receive the new
message type. The old reliable VOICEDATA path is discarded.

- **Capture and consent:** SDL's default recording endpoint, native signed
  16-bit mono at 8 kHz. `s_voiceChatCapture` starts off separately from archived
  Send Voice. Only PTT or an explicit local test opens input. Releasing PTT,
  disabling consent, losing the active session, restart and shutdown destroy
  the stream and clear queued capture. One-second retries apply to failures,
  not quick PTT presses; a stream stalled for two seconds is reopened. SDL
  callback draining and main-thread access share the stream lock. Each queue
  holds four 20 ms frames; overflow replaces old audio, never grows a container.
- **Codec:** first-party mu-law companding, 160 samples per independently
  decodable frame, 64 kbit/s speech payload. A version/sequence/capture-time
  header makes each frame 169 bytes. No codec history crosses packet loss;
  gaps remain gaps rather than retransmitted or time-stretched speech.
- **Admission:** exact lengths/version, increasing wrapping sequence numbers,
  a three-frame burst and 50-frame/s sustained limit. Capture older than 120 ms
  is discarded. The server and receiver reject frames more than 120 ms later
  than their learned clock offset; that baseline survives PTT gaps. This is
  an **added-delay bound**, not proof of absolute cross-machine one-way latency.
  The learned offset tracks sender clock drift: the baseline may slew towards a
  later offset by at most 2 ms per second of accepted active traffic, so
  independent monotonic clocks cannot silence a healthy talker after hours,
  while a backlog delayed by a PTT gap is still rejected and rejected frames
  never train the clock. Initial skew and real multi-hour sessions still need
  qualification outside simulation.
- **Transport:** authenticated unreliable messages carry no routing authority
  from the sender. Exact server recipients retain team/arena/spectator and
  managed-match policy, receive preference and mute. No following-spectator
  fan-out, demo or repeater forwarding occurs. A fixed mailbox keeps at most
  one current frame per three talkers per recipient. It is flushed after
  snapshots, expires entries after 100 ms, respects a maximum 20 ms rate burst
  and refuses existing fragments or a reliable backlog that would fragment it.
  Failed sends are discarded; no voice retry queue can block later snapshots.
- **Playback:** three talkers, four OpenAL buffers each, real-time pitch 1 and
  master × receive-volume gain. Speech is independent of dialogue/music gain.
  Generation changes flush the old occupant. Recipient mute/receive changes
  stop already queued speech; connection cleanup clears mute/moderation state.
- **Controls:** both retained Voice and Classic Voice Config expose consent,
  Send/Receive, levels, echo and local-only testing. Localized status reports
  off, ready, recording, input unavailable or session unavailable. Leaving the
  test tab/menu stops testing; revoking access also clears its activation.
  macOS bundles declare microphone usage, and Android declares optional
  microphone hardware plus its recording permission. These declarations are
  implementation coverage, not physical OS-permission qualification.

`tools/tests/voice_chat_runtime.py` launches real hidden clients and either a
listen host or dedicated server on `mp/q4dm1`. It sends a synthetic 400 Hz tone
through the actual codec, net channel, policy and OpenAL playback without
opening microphones. A bounded loopback UDP proxy introduces loss, reorder and
250 ms delay. Both final reports pass all 20 checks:

- `.tmp/audio/final3-voice-listen/result.json`
- `.tmp/audio/final3-voice-dedicated/result.json`

Checks cover both directions, recipient mute/unmute, receive-off, gain 0.125
at master 0.5/receive 0.25, pitch 1 during time scale 0.5, server moderation,
sound restart, packet loss/reorder, late rejection, bandwidth recovery,
reconnect/slot reuse, no spectator leakage, bounded playback and clean exits.
At 2,000 B/s, current voice is dropped while the dedicated server continues
15–25 snapshots/s in steady measurement intervals. Rendered 400 Hz amplitudes
in the dedicated WAVs reach 0.100 (host) and 0.039 (peer), above the harness's
0.02 floor. The proxy reports 74 dropped packets and at most 51 pending
datagrams in its 256-entry bound. Both reports name the final client, dedicated
server and MP module listed above, and harness SHA-256
`ed55dd97f254c26d6f0c7520672e1d8743dba42a400e74e05cca5e55ad9ac950`.

The harness drives each hidden client through a console script it rewrites
between phases. The final listen and dedicated runs passed at the first
attempt, but on the previous build, on this busy shared machine, nine attempts
were needed. Of those, two lost a client when the engine's `exec` of a script failed while Windows was
replacing it (`couldn't exec voice-wait-*.cfg`); an earlier development run
hit the same race once. Two began the reconnect phase during the restarted
match's map restart, which the harness now prevents by waiting for the server
and both talking clients to report a running match after their own restart.
One exceeded the default 360-second budget while a client needed 25 s to load
the map (the final runs pass `--timeout 900`), and one starved the clients of
CPU until too few synthetic frames were captured. The harness also still writes
its report when a child cannot be stopped. These are harness and host-load
limitations, not voice defects; rerun a failed attempt before reading it as a
product failure.

`tools/ui/capture_voice_settings.py` then exercises the real settings adapter,
SDL callbacks and local OpenAL playback using **SDL's explicitly selected
dummy driver**, which supplies silence and never opens a physical microphone.
The vendored SDL Meson build now includes that opt-in driver; SDL never selects
it as a physical-device fallback. The first test captures 59 frames and plays
58, stops on tab exit, then verifies consent revocation. No VOICE_SEND message is
emitted despite echo being enabled. The four English engine-render-target
screenshots were inspected: off, ready, active test and Classic Voice Config.
Reports on the final binaries: `.tmp/audio/final3-voice-ui-english/result.json`
and `.tmp/audio/final3-voice-ui-turkish/result.json` each pass all eleven checks.
The English and Turkish captures were inspected again. In English the retained
"Local microphone test" label wraps onto two lines inside its row, and the
Classic page draws its two new rows without the row plate the older rows use;
both are cosmetic and remain UI-01 polish.

Native `VoicePacketTest` exhaustively tests mu-law error bounds, achieves
37.74 dB fixture SNR and checks malformed packets, wrapping sequence/clock,
idle-delayed backlog, a simulated 24-hour ±1000 ppm clock drift with jitter,
loss and PTT gaps, a rejected backlog that must not train the clock,
burst/flood limits, capture overflow, mailbox freshness and whole-envelope
rejection. `voice_capture_lifecycle.py` compiles production
capture code against fault-injected SDL interfaces; `voice_transport.py`
compiles the production net-channel admission gate. Native retained-settings
tests cover consent, disabled testing, actual peak binding and test shutdown
on tab changes. Focused API, networking, team-policy, macOS packaging and
settings regression checks pass.

These tests qualify Windows process-to-process playback and controlled capture
lifecycle. Physical microphones, default-device migration, permissions/hotplug
on target OSes, real multi-hour sessions (drift is covered only in simulation)
and WAN intelligibility remain AUD-05 qualification work. Narrowband quality
and silence for lost frames are explicit tradeoffs, not an Opus-quality or
voice-processing claim.

## Installed transcript inventory

`tools/tests/audio_asset_inventory.py` reads installed PK4/loose declarations,
with later names overriding earlier ones. It records asset names, hashes,
localization keys and coverage counts, never extracted transcripts. `--sizes`
adds header-based allocation estimates, not a decoder validation.

| Content | Speech shader candidates | Same-name lip-sync/text mappings | Unmapped | Unmapped with a description |
| --- | ---: | ---: | ---: | ---: |
| Retail | 3,672 | 2,268 | 1,404 | 1,298 |
| Awakening | 1,584 | 1,512 | 72 | 8 |

Reports: `.tmp/audio/stock-inventory.json` and
`.tmp/audio/awakening-inventory.json`. Each game directory is inventoried
separately; the expansion report is not a merged runtime filesystem inventory.
Some missing candidates are multiplayer announcer sounds. A description is not
automatically a subtitle; a key existing in any language table does not establish
every locale's coverage. These candidates are broader than the runtime's verified
single-variant/full-path mapping policy below; they are not coverage claims.

## Dialogue subtitles

The shared SP/MP overlay derives cues from actual sound channels and their
integrated sample clock. It uses `lipSync.text` only: a same-name shader mapping
is valid for its single selected entry; otherwise the full sample path without
its extension supplies the mapping. Descriptions, ambiguous random variants,
basename guesses and unresolved localization keys are excluded. Text remains in
installed assets; no extracted transcript or invented alias ships in this repo.
The normal language dictionary supplies English fallback when a selected locale
has no matching text. Quiet exact-key lookup avoids per-frame missing-key spam.

At most the two newest eligible utterances display. Authored volume, fades,
private/anti-private audience and distance still constrain cues; user master and
dialogue mute and hardware voice allocation do not remove captions. The selected
leadin/loop sample owns its text. Stops, replacement, completion and missing text
clear the cue. Speaker labels require a matching live entity/emitter and its
`npc_name`, with a localized radio label where applicable.

The Lowpixel overlay defaults to 17 dp, white text, green speaker labels and a
0.60 black backing. Long text wraps using actual font advances and pages in
proportion to utterance duration; the data does not establish word-aligned times.
It draws through the common HUD path in either UI mode, with persistent localized
size/background/speaker controls in Settings > Audio. Two simultaneous utterances
get one line each. The four controls have labels in all twelve menu languages.

`audio_subtitles.py` executes production cue selection and bounded pagination,
including missing/ambiguous mappings, loops, UTF-8, privacy, distance and overlap.
The `openq4-ui-subtitles` native runtime test validates the document and one/two
line geometry at 1280×720, 1024×768, 3440×1440 and 720×1280. Both pass.

`audio_subtitles_runtime.py` uses original tone/text fixtures plus an existing
installed dialogue shader in actual loaded campaign maps. No microphone or host
input is used. The following reports pass all 16 checks, including selected-text
English fallback, mute, pause, half speed, restart, overlap, interruption,
save/restore, installed transcript selection, archived controls and real Audio
page scrolling. Reports retain executable, module, content and harness hashes.

- `.tmp/audio/final3-subtitles-wide/result.json`: stock, English, 1280×720.
- `.tmp/audio/final3-subtitles-portrait/result.json`: Awakening
  `game/m01_stranarus_trench1`, Turkish, 720×1280.
- `.tmp/audio/final3-subtitles-classic/result.json`: stock, Classic UI, 1024×768.

All three use the final client and harness SHA-256
`fe9a2070f85d66a6339cfb72fb2f8f094cfc945e26c33d8cca5cc40f542176ef`.

Engine-render-target captures were inspected for captions and the new controls.
The final portrait caption is inside the cinematic picture, but Awakening's
existing narrow-layout title card can overlap enlarged captions. The scene also
shows letterbox bands with world pixels outside them. These are retained UI-02/
UI-04 qualification gaps, not a full portrait presentation pass. Arbitrary custom
cinematic HUDs, complete authored scene/locale coverage and physical headset
stereo placement remain unqualified. Missing transcripts stay an explicit AUD-07
gap; this does not provide effect captions or speech recognition for voice chat.

## Long-session voice evidence, 22 October 2026

The interrupted Claude session left a completed dedicated-server/two-client
run in worktree `1388`, `.tmp/soak/soak-2h-final/`. Its `result.json` reports
7,168.9 seconds (1:59:29), not the requested 7,200 seconds. The original harness
accepted a duration one interval short and included shutdown time. Preserve
that report unchanged: its `duration_met: true` is not a strict two-hour pass.
`voice_chat_soak.py` now takes a sample at or beyond the requested endpoint,
stops its duration clock before shutdown, and requires the full live duration.
The user cancelled the repeat long run; no replacement two-hour run is claimed.

All 120 measured intervals pass. Synthetic 400 Hz PCM traverses the real codec,
net channel, server policy and OpenAL wave backend in `mp/q4dm1`; microphone
opens and host input are zero. The server admits 19,390 frames, rejects 1,261
and refuses zero sends. Host/peer playback totals are 8,261/8,983 frames.
Delayed-backlog probes reject 1,168 frames at the peer. Maximum admitted added
delay is 120 ms and the maximum observed playback queue is three frames.
Admission baseline shifts span -48 to +6 ms. A reconnect after interval 60
changes the peer generation from 356 to 359, clears mute/moderation and resumes
admission in 16.8 seconds. No console step runs twice.

Private-memory growth from the warmed first quarter to the final quarter is
-18.2 MiB for the server, -34.3 MiB for the host and +4.1 MiB for the peer
(peer comparison excludes reconnect warmup). The recorded rendered tone remains
present late in the session. Machine CPU load ranges from 27.8% to 100%; free
commit falls from an initial 14.62 GiB to a minimum 1.32 GiB. This is useful
loaded-host stability evidence, not an uncontended performance measurement.
All processes share one machine clock; the native simulated 24-hour drift test
remains the independent-clock drift evidence. Physical capture, WAN peers,
endpoint changes and target-OS breadth remain unqualified.

The saved run binds these SHA-256 identities:

| Artifact | SHA-256 |
| --- | --- |
| Client | `6c3a4dfeaaf5e8e98473d739e1eddc7598de724b2e828d1184bce6fadca724ff` |
| Dedicated server | `14744541d29cc00b6584c89b3fd5080a693b801f3e47f9a34465a6d8d8f4e0d8` |
| MP module | `ea2079fa810e93602757018111c8cfaee012f4447d49e4f43245a21f42e19112` |
| Original harness | `d665d5b8cf3071ce17eba89b1a2f0ea4ef97edda11a7245a4b530a2cf6b89e6f` |

The recovered implementation is in `bf6b50800` (per-sender admission telemetry)
and `c9ca2e315` (session and mixed-release harnesses). Do not transfer the old
runtime result to a newly built executable solely because these commits match.

Worktree `2993` verifies the corrected endpoint with a new 220-second request:
`.tmp/outstanding/voice-short/result.json` records 232.4 seconds, twelve passing
intervals and all thirteen checks passing, including reconnect, delayed backlog,
exactly-once steps and rendered tone. The server admits 2,454 frames and rejects
132; clients play 973/1,043 frames. Maximum admitted added delay is 101 ms and
the queue peaks at four frames. This short run qualifies the harness correction
and current staged runtime, not another long session. The rebuilt native suite
passes 276/276, and transport, capture lifecycle and capability-gating checks pass.

## Mixed-release voice check, 22 October 2026

The saved `.tmp/soak/mixed2/result.json` in worktree `1388` covers published
0.13.0 against the voice-enabled candidate, in both server/client directions,
with `si_pure` both 1 and 0. All fourteen harness checks pass. Pure connections
are explicitly refused for missing packages; non-pure connections are refused
for declaration-checksum mismatch. No unknown unreliable voice messages appear.

No mixed-release peer joins and the talker counters stay at zero. These results
establish explicit refusal for this release pair, not voice interoperability or
runtime capability negotiation with an admitted old peer. The existing
`ui_voiceProtocol`/`si_voiceProtocol` gates and appended message identities are
covered separately by `voice_protocol_gating_contract.py`; production transport
and capture-lifecycle tests pass. The player guide recommends matching releases
and content without asserting that every different release pair is incompatible.

## Output recovery, layout changes and memory pressure, 23 October 2026

Implementation: [audio modernization](audio-modernization.md#output-changes-and-memory-pressure-october-23).
Windows x64, MSVC debugoptimized, built from a detached worktree of `2f6c628a3`
plus these changes and staged into that worktree's own `.install`, so peers'
uncommitted work in the shared checkout is not in the binaries. Client SHA-256
`e20c20f499df2bfdb01215dd34e27e51d24aebae5b06436e59b105b2339379b8`; each
report records the modules, paks and harness hashes. The runs use the installed
Steam 1.4.2 PK4s, generated tone fixtures in an isolated save path and OpenAL
Soft's wave writer. No physical device, microphone or host input is used.

### Output loss and paused resume

`tools/tests/audio_output_recovery_runtime.py` plays a 60 s dialogue fixture
(radio channel of a saved fixture entity) and an effect fixture on stereo
output in `game/airdefense1`, then runs `s_testDeviceInterruption 1500`: every
source stops, as OpenAL Soft does on disconnect, and no voice is allocated
until the outage ends. During the outage `listSoundVoices` reports
`connected=0` and no audible voice. Afterwards both fixtures play again; the
dialogue's own OpenAL position moved 2,635 ms while the scene clock moved
2,633 ms and stands at 5,152 ms, not at its start. Before this change the
static, non-looping voices ended at the outage. The rendered WAV shows the
440 Hz line, a silent gap of more than a second, then the line again. The
dialogue was then paused and the output interrupted again. After the resume it
continues from its paused position (5,173 ms, then 5,845 ms after 700 ms of
scene time) instead of replaying from its first sample.

The same run then plays two 60 s Ogg fixtures on separate channels: one whose
final page lost its end-of-stream flag, which stb_vorbis still measures, and
one whose last two pages carry no position, which the engine counts
("counted; no length metadata" in the log). Both stream through bounded queues
and both play again after a third, 700 ms outage. The run's only stream
underruns are the queues stopped inside the three outages.

### Live layout transitions

The surround run starts the dialogue on 5.1 output, then changes
`s_numberOfSpeakers` 6→2→6 and `s_openALHRTF` 0→2→0 while it plays. Each change
logs "device reset in place" and an output change; no sound restart or sample
reload occurs after the map has loaded. `listSoundVoices` reports the
dialogue routed to the centre on 5.1 and unrouted on stereo and HRTF, playing
in every phase, and the unrouted phases report its position following the scene
clock (3.65 s, then 6.89 s, 3,237 ms apart over 3,233 ms of scene time). OpenAL
reports HRTF enabled in the HRTF phase. The wave writer restarts its file at
each reset, so the final recording is the last 5.1 phase. Its 440 Hz
centre-to-surround ratio, compared within the windows where the line is loud,
is 2.11 (2.27 is the value for `s_centerFractionVO` 0.75). The profile's own
startup layout change, which used to trigger a second full sound restart, now
resets in place too.

### Low-memory trim

`s_trimAudioMemory 0` in the same stereo run freed 1,128 of the level's samples
(96.3 MB) and kept the 95 that channels referenced, including the playing
fixtures and both streams. The blaster's `fire01`, loaded by the level and idle, reads
`(PURGED)` after the trim, plays again on `testSound` and reads loaded. The
operating-system path (`SDL_EVENT_LOW_MEMORY` from the lifecycle watch or the
event loop) is covered by the Android lifecycle native test on the Android and
desktop branches: one notice trims once, on the main thread. Delivery by a real
Android or iOS device is not qualified.

### Streams without length metadata

`tools/tests/audio_streaming.py` derives four fixtures from original Ogg files:
the last two pages without a position, and the file followed by 70,000 other
bytes, each at 22.05 kHz mono and 48 kHz stereo. stb_vorbis reports no length
for any of them. The production reader counts each one, finds its last
positioned page and then decodes and seeks (start, first sample, middle, last
sample, quarter) with exactly the PCM of a complete decode, for two
independent decoders at once. The production queue test then runs leadin/loop,
offset, routing and mixed static/streamed transitions over two of them. A
stream with a length keeps it, and an uncounted one refuses to seek.

### Envelope pass during level loads

The airdefense1 load streams 38 tracks (21 cinematic sounds, five music tracks
and stingers, nine ambience loops and two weapon sounds) and now builds their
envelopes on job workers ("envelope on a job worker" in the log). Offline, a
complete stb_vorbis decode of those 38 files (18.5 million frames) takes
475–510 ms on this machine, the main-thread work that pass previously did
inside the load. This is an offline measurement of the moved work, not an
end-to-end load-time benchmark on an idle machine.

### Native and harness coverage

The new `openq4-sound-output-recovery` (201 checks) and
`openq4-sound-memory-trim` (1,536 checks) tests cover every combination of the
two decisions. The stream-queue test checks that a static voice stopped by a lost
endpoint recovers while one stopped with its output present ends. The
decode-budget harness checks that an envelope job joins without an upload or a
reload and that a damaged one reloads. `sound_settings.py` (35,807 checks, all
35 mutants rejected) and `sound_recovery.py` (42,348 cumulative checks, all 11
mutants rejected), which extract the hardware update and render bodies, still
pass. The Android lifecycle, OpenAL pause, stream budget, payload restore, memory,
subtitle, screen-shake, reverb, macOS provider and Steam Deck harnesses pass.
The full native Meson suite passes 278 of 279 tests on the rebased build. The
remaining failure, the match-control projection contract, looks for the engine
checkout at `../openQ4`, which a worktree under `.tmp` does not have; pointed at
the worktree with `OPENQ4_ENGINE_REPO`, it passes.

On the final build, the stock regression runs that exercise the voice update
path also pass. `audio_dialogue.py --compressed --speakers 6 --decode-memory 16
--audio-memory 192` passes 28 of 28 checks with no stream underrun and a centre
ratio of 1.74. `audio_memory_runtime.py` passes 13 of 13 checks.

## Dialogue routing audit

`tools/tests/audio_dialogue_coverage.py` reads installed declarations in
filesystem order, finds every reference to a speech shader (a same-name
lip-sync declaration, a radio line or a `sound/vo` sample) in entity
definitions, maps, scripts, BSE effects and GUIs, and classifies it with
`SoundIsDialogue` compiled from the engine header. It writes identifiers, kinds
and counts, never transcripts.

| Content | Speech shaders | By sample path | Shader/source-kind pairs | Not dialogue |
| --- | ---: | ---: | ---: | ---: |
| Retail `q4base` | 3,672 | 3,672 | 3,495 | 0 |
| `q4base` + Awakening `q4xbase` | 5,153 | 5,153 | 4,912 | 0 |

Retail pairs: 2,780 NPC lip-sync keys, 146 other definitions, 484 map entities
and 85 scripts; with Awakening layered, 3,830, 146, 533 and 403. No BSE effect
or GUI names a speech shader, and 343
retail and 811 combined speech shaders are not named by any scanned source (some
are reached through computed names). Because every speech sample lives under
`sound/vo`, the Dialogue Volume control reaches it whichever path plays it.
This is a static routing result; audible scene coverage is still open.

### Transcript languages

The asset inventory now reports which installed language tables carry each
mapped line. The Steam install's 2,268 mapped lines exist only in English
tables (its French, Italian and Spanish tables hold three map-pack strings and
no lip-sync text). Awakening's 1,512 mapped lines are literal English in their
declarations. Other menu languages therefore show English captions; the player
guide states this.

## Authored scenes, centred reverb and portrait captions, 23 October 2026

Implementation: [audio modernization](audio-modernization.md#authored-scenes-centred-reverb-and-portrait-captions-october-23).
Windows x64, MSVC debugoptimized, built from a detached worktree of `8cb58aa4a`
plus this round's changes and staged into that worktree's own `.install`. Client
SHA-256 `347e6e15b9f15e48a49d2c176a61cef1bfd54c464b9763c4efd0f2389e68a50d`;
each report records the modules, paks and harness hashes. Runs use the installed
Steam 1.4.2 PK4s (Awakening from its separate content root), isolated save paths
and OpenAL Soft's wave writer. No physical device, microphone or host input is
used, and no report contains transcript text.

### Centred sounds in mod reverbs

`tools/tests/audio_mod_reverb.py` now also renders a centre-routed source, its
noise in FC of a 5.1 buffer through direct channels, on a 5.1 loopback context.
With room rolloff 1, OpenAL alone leaves its reverb send at the near-distance level
at two and four times the minimum distance (ratio 1.0000), where a mono source
falls to 0.5000 and 0.2500. With the engine's send gain the centre-routed source
falls to 0.5000 and 0.2500 too, and to 0.1667 at room rolloff 2.5 and three times
the minimum, matching the mono source to four places. The same holds with EAX
reverb, and a stock preset (no room rolloff) keeps the centre send unchanged.
All 20 checks pass, including the production source pins; the native reverb-core
test checks the gain function's values and edge cases.

### Streams on recycled voices

The first gameplay run of this round found that a streamed track started on a
voice whose source had played before never played. The stereo output run's second
simulated outage left all four streams it was playing (two fixtures and two
airdefense1 ambience loops) starting and stopping 39 times in 1.5 s. Its first
outage had recovered only because those streams restarted on voices that had never
played. An OpenAL loopback probe confirms the cause: a source that has played and
been flushed reports `AL_STOPPED` with every newly queued buffer already processed,
while a rewound one reports `AL_INITIAL` with none processed and then plays. The
production queue test now models that state (a voice is played, flushed, and a
stream queued on it must be playable). It fails without the rewind and passes with
it.

### Output recovery and Dialogue Volume

The rerun of `tools/tests/audio_output_recovery_runtime.py` on this build passes all
19 stereo and 12 surround checks. At Dialogue Volume 0.25 the line keeps that gain
through a 600 ms outage and restarts on the new output serial, and both stream
fixtures play again on the new serial. In the surround run Dialogue Volume 0.5
holds through both in-place resets that follow it: HRTF on, then HRTF off, where
the line moves back to the centre speaker (gain 1.0 before, then 0.5 and 0.5). The dialogue moved 2,645 ms over 2,633 ms of scene time across
the first outage. The paused line moved 704 ms over 699 ms after its resume. The
trim freed 1,128 samples and kept 95, and the only stream underruns (12) are the
four streams' queues inside the three outages.

### Portrait captions

`tools/tests/audio_subtitles_runtime.py` (Awakening `game/m01_stranarus_trench1`,
Turkish menus with English transcripts, 720×1280) passes all 16 checks on this
build. Its captures were inspected. Default and enlarged two-line captions hang
below the intro's lower letterbox band, clear of the "Planet Stroggos" title text
the cinematic draws in its picture; before this change, enlarged captions covered
it. The harness now waits up to 600 s by default, because the map took 110 s to
load on the busy machine in a first attempt. The native placement test checks the
rule at 9:16, 10:16 and 3:4 for four text sizes, and the unchanged placement at
4:3, widescreen, in gameplay and for an unusable pitch.

### Speech in authored scenes

`tools/tests/audio_scene_audit.py` plays the first 30 s of each campaign map after it
loads, chaining maps in one hidden client, with cinematics, AI and scripts running and
god mode so the player survives. There is no other input, so scripted lines that wait
for the player to move are not reached. `s_logSpeech` reports each start that may be
speech. Every 3 s the harness lists the audible voices and subtitle cues, mutes
Dialogue Volume for 150 ms, lists the voices again and restores it. After each opening
it records the audio residency and the voices playing on bounded stream queues.

All 29 stock maps run in three chains and all 13 Awakening maps in one, and every
check passes in each:

| Measure | Stock (29 maps) | Awakening (13 maps) |
| --- | ---: | ---: |
| Starts that may be speech | 256 | 633 |
| Dialogue starts (distinct shaders) | 93 (62) | 55 (45) |
| Declared or flagged speech not following Dialogue Volume | 0 | 0 |
| Probe pairs; muted probes with a dialogue voice audible | 261; 0 | 117; 0 |
| Dialogue starts with a transcript; without one | 26; 67 | 39; 16 |
| Probes catching a transcribed line: cue shown; cue missing with a free slot | 24; 0 | 34; 0 |
| Maps with streams playing after the opening | 27 | 12 |
| Reserved audio after an opening: smallest, median, largest | 65.0, 124.6, 193.8 MiB | 71.7, 151.1, 242.1 MiB |
| Process peak (admission budget 256 MiB); refusals | 255.4 MiB; 0 | 255.7 MiB; 0 |

The largest openings are `mcc_landing`, `building_b` and `mcc_1`, and Awakening's
`m09_valkaryne`. The process peak includes level changes, where the admission
reclaims the previous level's samples as it approaches the budget. The only
radio-channel starts that are not speech are two stock effect shaders. The
voice-over-flagged starts that are not speech are radio static and interface
clicks, 506 of them an NPC typing at a console in `m06_mcc_invasion`. None of them
carries a lipSync declaration. The Strogg sentry's chatter counts as dialogue: it
plays through the actor lipSync path, which flags its sound as voice-over.

The first Awakening attempt did not complete. The machine was saturated that hour:
the first map took 340 s to load where the rerun took 18 s, and OpenAL
Soft's wave writer reported failed writes to its output file. The engine reopened
the device after each one, as it would for a lost endpoint. The chain stalled on the
second map waiting for a first gameplay frame and reached its 45-minute limit. The
rerun on an idle machine, identical except for its output folder, passed in 552 s
with no device loss.

These are openings sampled without player input, not complete playthroughs. They
show that the authored speech they reach routes as dialogue, mutes with Dialogue
Volume and captions only from transcripts, but not that every line in both
campaigns is audible or captioned.

## Linux, voice output loss and long sessions, 24 October 2026

### Linux x64

The audio round's harnesses ran on Linux for the first time. That covers output
recovery, live layout changes, the trim, both stream fixtures, subtitles and the
scene audit. Ubuntu 24.04 ran under WSL2 (kernel 6.6.87.2), with the client built
there from the same commit (debugoptimized, without native tests, the multiplayer
module, Vulkan, OpenXR or GLES, and with a minimal `pak1`). It ran hidden under Xvfb
on Mesa llvmpipe (OpenGL 4.5 compatibility). The system OpenAL Soft 1.23.1, not the
1.25.1 the Windows package bundles, rendered through its wave writer. Retail paks were
copied into RAM first. The two Ogg stream fixtures came from the Windows encoder
(`--write-fixtures`), since that host has no ffmpeg; the fixtures are deterministic.
Client SHA-256 `78fcaf93f69b6a636228f1c29ac414de4ae686bcdcb02c370dac768649cd7744`.

| Harness | Checks | Notes |
| --- | ---: | --- |
| `audio_output_recovery_runtime.py`, stereo | 19 of 19 | dialogue moved 2,636 ms over 2,597 ms of scene time after an outage; the paused line 707 ms over 719 ms; trim freed 1,127 samples and kept 96 |
| `audio_output_recovery_runtime.py`, surround | 12 of 12 | in-place layout and HRTF resets, centre re-routing, Dialogue Volume held; 5.1 centre ratio 2.10 |
| `audio_subtitles_runtime.py`, stock 1280×720 English | 16 of 16 | |
| `audio_scene_audit.py`, `airdefense1`, `hangar2`, `medlabs`, `dispersal` | 10 of 10 | 10 dialogue starts, streams playing in all four, residency 107.7–150.6 MiB |

Building it found a Linux-only break: the dedicated server links the OpenAL sound
code against `src/sys/stub/stub_openal.cpp`, which lacked the `alSourceRewind` that
the recycled-voice fix added on 23 October. Windows links the real library and never
showed it. The stub now provides the call, and the Linux dedicated server links.
These are automated Linux runs on one virtual machine with software rendering, not
physical Linux audio devices or distributions other than Ubuntu.

### Voice chat across output loss

Windows x64, MSVC debugoptimized, built from `35a1e919c` plus this round's changes;
client SHA-256 `de29bfd2fa9b48d94cc2d14da76a356d449957dc9097b6e88d732c4f25e3fcbc`.
`tools/ui/capture_voice_settings.py` now simulates an output loss during the local
microphone test, with SDL's dummy input and an OpenAL wave writer. All 13 checks
pass. Before the outage, 58 frames had played and none were dropped. Halfway through
the 1,000 ms outage, 58 had still played and 26 were dropped. After it, playback
continued: 105 played in all, while capture never stopped. Neither the dummy input,
which delivers frames in bursts, nor any source state is the signal: the harness
reads the frame counters. The first run of the new checks also required a source
in the playing state, which this bursty input rarely shows even without an outage;
that criterion was dropped. Physical capture and playback devices remain
unqualified.

### Long sessions

`audio_scene_audit.py --repeat 3` played `airdefense1`, `hangar2`, `medlabs`,
`dispersal`, `storage1` and `core1` three times each in one client, 18 openings in
671 s. All 11 checks pass. Reserved audio after each opening is flat across the
passes: at most 83 KiB more on the second pass than the first, and identical on the
third. Voices in use and stream reservations do not grow. The process peak is
205.7 MiB. A first attempt labelled the passes with `#`, which the console's `echo`
splits into separate words, so no opening could be told apart. The labels are now
`passN/<map>`.

## Other verification and limitations

- `sound_settings.py --no-mutations`: 35,807 checks pass.
- `sound_recovery.py --no-mutations`: 42,348 cumulative checks pass. These are
  counted API tests, not physical endpoint-loss tests.
- Dialogue classification, settings-menu coverage, audio payload restore,
  screen-shake and retail reverb contract tests pass; client and dedicated
  builds and staging pass.
- At the earlier audio/subtitle checkpoint, the native Meson suite passed
  187 of 187 tests. Its
  imported game regressions caught a voice reset placed above a pinned comment
  in `ClearMap`, fixed before the final runs.
- The audio, voice and subtitle static/native-compile tests, the API pin
  contracts touched by API 53, `build_retained_screens.py --check` and
  `build_subtitles.py --check` pass on Windows.
- The validation script's full Python sweep (`openq4_validate.py push
  --skip-build`, 357 checks) found three test-side gaps, now fixed: unwired
  new tests, a missing subtitle-reset stub in the view-effects harness, and
  the retained gate rejecting the Voice page's deliberate test-off write. The
  three checks and the 84 workflow/contract tests that read the CI files pass
  afterwards. The product-requirements structural audit against
  `cbe967f2b` passes.
- Linux: every changed engine sound, network, menu and language source and the
  touched SP/MP game sources pass `-fsyntax-only` with GCC 13 and Clang 18 in
  WSL Ubuntu, and the production-extraction harnesses (decode, aggregate
  memory, stream budget, payload restore, subtitles, voice transport and
  capture lifecycle) compile with `-Werror` and pass under both. GCC exposed one
  misleading-indentation error in the aggregate-memory harness, now fixed.
  `audio_streaming.py` was not run there (no ffmpeg in that environment), and
  no full Linux, macOS or Android build was made.
- `sound_settings.py` now resolves its own path before recording source hashes,
  fixing a Windows junction/real-path mismatch exposed by this worktree.
- The supplied Awakening scene still reports existing duplicate declarations,
  entities in solid, a missing `engine_righ` effect joint and an unknown
  `walker_trench_unit1_seq_2` script function. These content/gameplay findings
  are outside the current audio repairs and are not counted as clean startup.

Full subtitle coverage, whole-process/mobile memory qualification, physical device
lifecycle and target-OS breadth remain open. Voice and compressed streaming are qualified only
within the automated scope recorded above. The 23 October output recovery, layout and
trim results come from a simulated outage, OpenAL Soft's wave writer and a desktop
console trigger; physical endpoints, real low-memory notices and other operating
systems remain the qualification gates.
