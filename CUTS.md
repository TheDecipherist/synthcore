# What was cut from ShellHost.h

`include/core/CoreHost.h` is `synthshell/include/shell/ShellHost.h` at `53e9a9a`
(1,345 lines) with these lines removed: 424 kept, 920 cut. Line numbers are the
source's. `MidiClock.h`, `TempoSync.h` and `QwertyPlayer.h` are whole, with only
their `namespace` lines renamed.

| Source lines | What | Why it is not in the core |
|---|---|---|
| 1-17 | The shell's file header | Replaced by synthcore's own (9 lines) |
| 21-22 | `#include "ProgramInfo.h"`, `"SessionState.h"` | The program browser and the session strip |
| 25-29 | `#include` of `AuditionSource`, `DrumEngine`, `PatternPlayer`, `FxRack`, `ModMatrix` | The audition source, drums, pattern maker, effects rack and mod matrix |
| 33-68 | `defaultCategories()` and the class doc | Program categories; the doc describes the strip. Replaced by a 2-line doc |
| 73-108 | Programs and the user bank | The program browser |
| 111-117 | `isSessionPlaying` ... `sessionPosBeats` | The strip's own pattern transport, used only by the pattern maker |
| 253-258 | `fxRack.setTempo (bpm)` and its comment, in `readTransport` | The effects rack |
| 261-341 | The `readTransport` overload that runs the pattern maker; `publishPattern`, `armPattern`, `armArp` | The pattern maker and the arp |
| 402-410 | `fxRack.setUnitsAllowed (hasFxUnitRack())`, in `prepareClock` | The effects rack |
| 413-437 | `patternPlayer.prepare`, `prepareAudition`, the sync-division defaults, `publishPattern`, in `prepareClock` | Pattern maker, audition, the shell's divisions |
| 452-494 | Scope, session (CHORDS / ARP / CHOPPER), `drumDisplayStep` | The strip's tabs |
| 496-587 | `SyncTarget`, `syncReachable`, `syncTargets` | The shell's per-destination divisions. Tabula has its own rate ladder (its CLAUDE.md, non-negotiable 7) |
| 706-718 | `syncDivision`, `setSyncDivision` | The same |
| 724-775 | `syncPhase`, `syncedMs`, `syncedHz` | The same |
| 803-848 | `resetSyncDefaults`, `setAllSyncDivisionsOff` | The same |
| 870-874 | `kMaxSyncTargets` | The same |
| 880-1126 | Header cells, audition, drums, audition mix | The strip, audition and drums |
| 1131-1132 | `setUiPitchBend`, `setUiModWheel` | The shell's keyboard row |
| 1134-1284 | The KEYBOARD slot, inspector line, routing tab, effects rack | The strip, mod matrix, effects rack |
| 1286-1287 | `brandLabel()` | The program browser's header |
| 1292-1314 | `runPattern`, `AuditionSink`, `fallbackSession` | Pattern maker, audition, session |
| 1319 | `syncDefaulted` | The shell's divisions |
| 1324-1338 | `syncDivs`, audition, pattern, rack and mod-matrix members | All of the above |
| 1340-1342 | `ShellHost::drumDisplayStep` | Drums |

Renamed in kept lines: `namespace shell` to `namespace core`, `ShellHost` to
`CoreHost` (the class, its destructor and three `const_cast`s).

Kept and worth knowing: `offersSyncOff()` still asks `keyboardState()`, so a
unit with a keyboard is offered OFF, INT and EXT, exactly as in the shell.

## Changed after the proof

The copy was proved identical first (above). Changes since, each with its reason:

| Date | File | Change | Why |
|---|---|---|---|
| 2026-10-07 | `include/core/CoreHost.h` `readTransport` | On `syncInternal` the playhead and incoming MIDI clock are not read: our own Tempo drives the clock. Two lines changed, one comment added | Tabula: "Sync is on, set to INT, when the synth first opens, and INT ignores a playing host." The shell let a playing host win in every mode (Tabula bugs.md B-042, found by its row clock probe) |
| 2026-10-07 | `include/core/MidiClock.h` `advance`, `sendTo` | Each MIDI clock pulse sent at the sample where its tick is crossed, rounded, from the block's own ticks per sample (`blockPerSample`), not spread evenly across the block; a pulse that rounds onto the block's end goes out at sample 0 of the next (`carried`, cleared in `prepare`); a `devEvenSpread` flag plants the old way for the test. 17 new lines | Tabula bugs.md B-008: at 140 BPM in 256-sample blocks, pulse 24 to 408 measured a quarter of 428.6667 ms against 428.5714. Proved by tests/clock_out.cpp at 90, 120, 140 and 150 BPM |
