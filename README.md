# synthcore

The clock, the transport, QWERTY and the focus rule, and nothing else. Copied from
`synthshell` for Tabula (the author, 2026-10-06), so Tabula can have the shell's timing
without linking the shell's panel, effects, arp library or pattern banks.

**`synthshell` is never changed for synthcore, and synthcore never changes
anything in another unit.** Every unit that links `synthshell` builds exactly as
before.

| File | What | From |
|---|---|---|
| `include/core/MidiClock.h` | `MidiClock` (counts 0xF8, works the tempo out) and `MasterClock` (the one pulse stream) | `synthshell/include/shell/MidiClock.h` |
| `include/core/TempoSync.h` | The shell's division units. Included by the master clock. Not Tabula's rate ladder | `synthshell/include/shell/TempoSync.h` |
| `include/core/QwertyPlayer.h` | QWERTY keys play notes whichever component has focus; nothing takes focus | `synthshell/include/shell/QwertyPlayer.h` |
| `include/core/CoreHost.h` | `ShellHost` cut down to the clock and transport: `hostTempo`, `readTransport`, `prepareClock`, INT / EXT sync mode, `emitClock` | `synthshell/include/shell/ShellHost.h` |

All from `synthshell@53e9a9a`. Namespace `core`, so a test can compile both side by side.

## The two proofs

- `python3 tools/check_copy.py`: every line here is a line of the source at
  `53e9a9a`, in order, apart from the renames (`shell` to `core`, `ShellHost` to
  `CoreHost`) and `CoreHost.h`'s 9-line header. `--selftest` plants a changed
  character and two swapped lines. What was cut is in `CUTS.md`.
- `synthcore_same_as_shell`: the same playheads and MIDI clock go through
  `shell::ShellHost` and `core::CoreHost`, and every number either exposes about
  time is compared bit for bit, every block: our own BPM at 120, 90, 140 and 150;
  a host that plays, jumps, loops and changes tempo; MIDI clock at 90 and 140 with
  start, stop and continue; MIDI clock under a stopped host; at 44.1, 48 and 96 kHz.
  `--selftest` plants one extra sample and one lost clock byte and must see both.

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target synthcore_same_as_shell
ctest --test-dir build --output-on-failure
```

The test builds `synthshell` from `../synthshell`, read only, into `build/`.

## Using it

```
add_subdirectory(<path>/synthcore synthcore EXCLUDE_FROM_ALL)
target_link_libraries(<unit> PRIVATE synthcore)
```

A unit subclasses `core::CoreHost` (two methods: `keyboardState()` and
`parameters()`, which must hold a `"bpm"` parameter), calls `prepareClock` from
`prepareToPlay`, and puts `host.readTransport (getPlayHead(), midi, n);` on the
first line of `processBlock`. The editor owns a `core::QwertyPlayer` attached as a
key listener to the top window.

## No shell panel, and QWERTY without it

A unit on synthcore has no shell panel to hide: the strip, the tabs, the arp and
the keyboard row are not compiled in at all. That replaces the two folds (PANEL
and SHELL) the shell's own documents describe, and it is how Tabula does it.

QWERTY keeps working because `core::QwertyPlayer` is a key LISTENER, not a
focused control:

- The editor owns one and adds it to ITSELF and to the TOP-LEVEL window, in
  `parentHierarchyChanged()`. A key with nothing focused targets the window,
  which sits above the editor; a listener on the editor alone never hears it.
- It re-reads the physical key state on every key event and on the editor's
  timer (`tick()`, about 12 times a second), so a control that does take focus
  delays a note by up to one tick rather than silencing it. That delay is what a
  probe can measure.
- It plays only while the app is in front, and releases every note when it is not.
- No control on the panel may take keyboard focus. JUCE buttons and text editors
  do, by default.

Solera does it differently: a JUCE on-screen keyboard owns the focus, and every
other control is set never to take it. That works only while nothing else ever
takes the focus. The listener works whichever component has it.

Tabula proves this with `TabulaKeysProbe` (real macOS key events into the opened
editor, audio measured, a planted focused text field caught) and `TabulaTempoProbe`
(the clock's quarter note at 90, 120, 140 and 150 BPM). The keys probe needs the
Mac unlocked and the app allowed to the front.

A known property of the clock out, copied unchanged from the shell: `sendTo`
spreads a block's 0xF8 pulses evenly across the block, not at each pulse's true
sample, so the MIDI clock a standalone sends carries up to one block of jitter.
The position (`tickPosition`) is exact. Tabula's bugs.md B-008.

## Changing it

A change here is a change to a copy: measure it against the shell first, and
update `CUTS.md` and `tools/check_copy.py` with it. Fixes found in the shell are
brought over by hand, the same way.
