# Multiplayer network qualification

Evidence for MP-07 (coupled prediction and rewind) and MP-08 (network
interoperability) gathered on one machine, and the simulated network conditions
that make that possible. Player-facing guidance is in the
[networking guide](../user/multiplayer-networking.md).

## Simulated network conditions

`src/sys/NetworkImpairment.h` is a delay line for UDP datagrams that both socket
layers (`win_net.cpp`, `posix_net.cpp`) use. Each bound `idPort` owns two lines:
received datagrams wait in one before the engine reads them, sent datagrams wait
in the other before they reach the socket.

| CVar | Meaning |
| --- | --- |
| `net_forceLatency` | Fixed milliseconds added to every datagram, each way (0-2000). |
| `net_forceJitter` | Further delay drawn uniformly from 0 to this many milliseconds per copy (0-1000). |
| `net_forceDrop` | Percentage of datagrams lost. |
| `net_forceDuplicate` | Percentage of datagrams delivered twice; each copy has its own jitter. |
| `net_forceSeed` | Seed for the random choices when a port opens; `0` derives one from the clock and port. |

Datagrams leave in the order they fall due, with ties in arrival order, so jitter
wider than their spacing reorders them as a real link does. Comparisons use the
millisecond clock wrap-safely. Payloads are stored at their real size (up to the
65,535-byte UDP limit); a due datagram larger than the reader's buffer is
discarded and counted, never truncated. A line holds at most 8,192 datagrams or
32 MiB and counts refusals beyond that. Lines keep draining after the settings
return to zero, so turning impairment off never reorders what is still queued.
Due sends are flushed whenever the port is polled, not only when the next send
arrives.

`netImpairment` prints the settings and, per open port and direction, the
datagrams offered, dropped, duplicated, released, reordered, refused and
discarded, and the deepest queue so far (`NET_IMPAIR` lines).

The CVars are unarchived and not `CVAR_NOCHEAT`, so like the original two they
count as cheats: hosting or joining a server without `net_allowCheats` or
`sv_cheats` resets them to 0.

This replaced the inherited Windows-only lag queue (POSIX had none). That queue
stored at most 1,400 bytes per datagram and silently dropped anything larger,
so with `net_forceLatency` set before connecting the handshake looped ("packet
exceeds latency queue capacity", "Bad challenge"). It released queued sends only
when another send came along, and had no jitter, reordering or duplication.

Tests: `tools/tests/native/NetworkImpairmentCases.h` drives the header
directly: fixed delay and order, jitter bounds and reordering, loss and
duplication rates, seed repeatability, oversize discards, a 9,000-byte
datagram, clock wrap and capacity. The native core safety test runs the cases,
and `tools/tests/network_impairment_state.py` also runs them against six
single-reversal copies of the header, each of which must fail.
`tools/tests/network_impairment_contract.py`
checks that both platforms define the same CVars, open and release the lines with
the port, queue receives and sends, flush due sends while polling and report;
it carries five single-reversal controls of its own.

## Prediction and lag compensation under impairment (24 October 2026)

`.tmp/mp-round2/impair_probe.py` runs a loopback listen server with three bots
and one remote client on the candidate build of this round. The client steps
through six phases, settling 4 s before each 6 s measurement with `net_verbose 2`
(controller state per snapshot) and `net_mpPredictDebug 1` (remote pose ages);
the server samples `lagCompStatus` every 3 s. Impairment is set on the client's
port, so both directions of its link are affected. `net_forceSeed 4242` makes the
runs repeatable. Reports: `.tmp/mp-round2/runtime/{legacy,enhanced,handshake}/report.json`.

| Phase | Settings (client port) | Resyncs | Client lead median | Server lead / RTT | Duplicated-command reports |
| --- | --- | ---: | ---: | --- | ---: |
| clean | none | 0 | 48 ms | 40 / 16 ms | 0 |
| latency50 | 50 ms | 0 | 144 ms | 140-144 / 117 ms | 0 |
| jitter50x40 | 50 ms + 0-40 ms | 0 | 192 ms | 181-188 / 117-150 ms | 2 |
| loss10 | 50 ms, 10% loss | 0 | 144 ms | 140-151 / 117-150 ms | 4 |
| duplicate20 | 50 ms, 20% duplication | 0 | 144 ms | 146-153 / 117-136 ms | 0 |
| combined | 60 ms + 0-40 ms, 5% loss, 5% duplication | 0 | 208 ms | 199-214 / 150-201 ms | 6 |

Legacy prediction (`net_mpPredictMode 0`), from the `legacy` run; the server
columns are the `lagCompStatus` samples taken during each phase. The
`enhanced` run (`net_mpPredictMode 1`) matches it phase for phase: no resyncs,
the same leads, and at most four duplicated-command reports in a phase.

- The delay lines did what was asked: in `loss10` the client's receive line
  dropped 35 of 361 datagrams (9.7%); in `duplicate20` it duplicated 66 of 362
  (18%); jitter reordered 13 received and 133 sent datagrams in its phase.
- The prediction controller held through every phase. The lead follows the
  round trip plus about one frame, the target settles within the 4 s, and
  commands arrive early (median 5-20 ms ahead). Duplicated commands appear
  only in a handful of snapshots under jitter and loss, where a late datagram
  arrives after the server needed it.
- The channel discards the reordered and duplicated datagrams
  (`net_channelShowDrop` "out of order packet"); usercmd redundancy covers the
  lost ones. No malformed, truncated or overflowing messages were logged.
- Lag compensation follows the real lead. Legacy rewinds rise from 56 ms clean
  to 156-169 ms at 50 ms and reach the 200 ms cap with jitter or the combined
  conditions. Enhanced prediction's advertised 16-tic extrapolation covers the
  whole lead, so its pose age is 0 and its rewinds stay at 0-16 ms (the frame
  bias), and its remote pose error is the deliberate forward extrapolation.
- A client that connected with 80 ms + 0-40 ms, 10% loss and 5% duplication
  already set (`handshake` run) joined on its first challenge, with no "Bad
  challenge" and no capacity refusals, then behaved as above.

## High player count (24 October 2026)

`impair_probe.py --dedicated --bots 20 --max-players 24 --quiet-client` hosts 20
bots on `openQ4-ded_x64.exe` with one remote client. The client records frame
times (`com_speeds`) instead of verbose logs, and the server runs `net_verbose 1`,
which reports a snapshot deferred behind a reliable backlog. Reports:
`.tmp/mp-round2/runtime/crowd-quiet/` and, with three bots for comparison,
`few-quiet/`.

| Phase | Client frame median | Prediction target the server sees | Round trip |
| --- | ---: | ---: | ---: |
| clean | 16 ms | 34-39 ms | 2-5 ms |
| latency50 | 17 ms | 142-146 ms | 104-122 ms |
| loss10 | 17 ms | 140-143 ms | 103-136 ms |
| duplicate20 | 16 ms | 142-148 ms | 103-121 ms |

No snapshot was deferred and no message overflowed; the three-bot run matches.

Earlier 20-bot runs had bursts of resyncs and commands up to 1.6 s late. Each
time the client process was starved of CPU, not the network path. The first
hosted the 20 bots on a listen server that also rendered, on the same machine
as the client. The second logged `net_verbose 2` and `net_mpPredictDebug` with
a flush per line, about 900 lines a second with 20 remote players. A third
overlapped another session's full-core build, which reproduced the symptom on
demand: client frames rose to a 38-57 ms median and the prediction target
climbed steadily toward its 1 s cap (`crowd-quiet-overlapped-build/`). A client
that cannot hold the 60 Hz tic rate falls behind the server in the same way;
this was measured, not changed. The one reliable-message overflow logged came
at server shutdown, after the client had already quit.

## Still open

Real remote peers (WAN routes, NAT, other operating systems and architectures),
sustained congestion and rate limiting, and the defaults decision for enhanced
prediction and lag compensation remain open under MP-07/MP-08. Simulated
conditions are uniform and independent per datagram; bursty loss and route
changes are not modelled.
