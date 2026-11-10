// synthcore: the parts of synthshell that Tabula links, and nothing else.
//
// CoreHost is synthshell's ShellHost (include/shell/ShellHost.h @ 53e9a9a)
// cut down to the clock and the transport: the tempo, readTransport,
// prepareClock, the INT / EXT sync mode and clock out. Every line below is
// that file's own line, unchanged, except the names (ShellHost -> CoreHost,
// namespace shell -> core) and this header. What was cut, and why, is in
// CUTS.md. synthshell itself is never changed for Tabula's sake.
#pragma once
#include <algorithm>
#include <juce_audio_processors/juce_audio_processors.h>
#include "TempoSync.h"
#include "MidiClock.h"

namespace core {

/** What the clock and the transport need from the instrument above them:
    a keyboard state (or none) and the parameter tree holding "bpm". */
class CoreHost
{
public:
    virtual ~CoreHost() = default;

    // ---- the transport, and the one master clock -------------------------
    /** THE tempo, resolved once, in the shell. Everything that follows the clock
        reads it from here: the audition source, syncedMs/syncedHz, the strip.

        This exists because handing each unit's own `bpm` variable to
        renderAudition was a bug waiting to happen and duly happened - a unit
        whose variable was not the one the BPM field drives rendered drums at
        the wrong tempo while the header said 120. One clock means ONE place
        that answers "what is the tempo", not a value passed in by whoever
        happens to be calling. */
    double hostTempo() const
    {
        // A running transport outranks the knob. That is the rule the whole
        // shell is built on and it was quietly missing: hostTempo read the BPM
        // parameter and nothing else, so a synced delay measured 500 ms at
        // 120 BPM, and still 500 ms at 90 BPM. The divisions were right and the
        // clock never arrived - which is precisely the failure this package
        // exists to make impossible.
        if (transportPlaying && transportBpm >= 20.0)
            return transportBpm;
        if (auto* p = const_cast<CoreHost*> (this)->parameters().getParameter ("bpm"))
            return juce::jmax (20.0, (double) p->convertFrom0to1 (p->getValue()));
        return juce::jmax (20.0, sessionBpm());
    }

    /** The one entry point for an external clock, whichever way it arrived.
        Called from processBlock, so it must stay allocation-free.

        Two things reach it and they are the same thing. In a host, the playhead
        hands us a tempo and a playing flag. Standing alone on a MIDI cable there
        is no such luxury: MIDI has NO tempo message, only 24 Timing Clock bytes
        per quarter note, and shell::MidiClock counts them and works the tempo
        out from their rate. Both end here, so nothing downstream has to know or
        care which kind of master it is following. */
    void setTransportTempo (double bpm, bool playing) noexcept
    {
        transportBpm = bpm;
        transportPlaying = playing && bpm >= 20.0;
    }
    /** True while something outside is driving the clock, for the header to say
        so - the difference between INT and EXT on the front of a real box. */
    bool followingExternalClock() const noexcept { return transportPlaying; }

    /** THE clock, read once, in the shell. A unit's entire transport code is
        this one line at the top of processBlock:

            host.readTransport (getPlayHead(), midi, buffer.getNumSamples());

        and after it, hostTempo() and syncedMs()/syncedHz() are correct.

        It takes both masters because a unit has two and must not care which:
        a DAW hands us a playhead, while a standalone on a MIDI cable gets
        nothing but 0xF8 pulses and has to count them. The playhead wins when
        it is playing (a DAW's tempo is exact and free); MIDI clock takes over
        otherwise, so a plugin sitting in a stopped host still follows a drum
        machine plugged into it.

        Written here rather than in each unit because the per-unit version of
        this - "read the playhead, prefer it while playing, else the knob" - was
        being copied into every processor with a private tempo variable that the
        shell could not see, and that is exactly how the maths came out right
        while the sound came out wrong. */
    void readTransport (juce::AudioPlayHead* playHead,
                        const juce::MidiBuffer& midi, int numSamples) noexcept
    {
        midiClock.process (midi, numSamples);

        // Learn the tempo from whichever master is there. THREE ways of
        // learning one number - and that is all this step is.
        double tempo = 0.0;
        bool   running = false;
        bool   located = false;
        double locateTo = 0.0;

        // INT IGNORES A PLAYING HOST, AND MIDI CLOCK (Tabula, bugs.md B-042): only our own Tempo.
        if (playHead != nullptr && mode != syncInternal)
            if (const auto pos = playHead->getPosition())
                if (pos->getIsPlaying())
                    if (const auto b = pos->getBpm(); b && *b >= 20.0)
                    {
                        tempo = *b; running = true;
                        // A DAW knows where it is, so follow its position - but
                        // only when it JUMPS. Locating every block pins the
                        // clock to whatever the host says, and a playhead that
                        // reports a static position (every stub playhead in
                        // every test rig, and some real hosts while stopped)
                        // then freezes every synced modulator solid while a
                        // synced delay stays perfect, because a delay wants a
                        // length and a modulator wants a position. That failure
                        // looks exactly like a clock bug and is not one.
                        //
                        // So: free-run, and re-anchor only on a discontinuity -
                        // a loop wrap, a locate, the user dragging the
                        // playhead. Which is also what makes looping work.
                        if (const auto ppq = pos->getPpqPosition())
                        {
                            const double drift = std::abs (*ppq - master.ppqPosition());
                            if (drift > kLocateThresholdQuarters)
                            { located = true; locateTo = *ppq; }
                        }
                    }

        if (! running && mode != syncInternal && midiClock.hasClock() && midiClock.isRunning())
        { tempo = midiClock.bpm(); running = true; }

        setTransportTempo (tempo, running);

        // Then ONE clock comes out. Whether the tempo arrived from a DAW, from
        // 0xF8 bytes on a cable, or from our own BPM field, what everything
        // downstream sees from here is identical: a position advancing at 24
        // pulses per quarter note. The author's point exactly - the master sync is
        // always the same thing, and what a unit does with it is its own
        // business.
        if (located) master.locate (locateTo);

        // OUR BAR IS THE MASTER, and its clock always runs. The author: "so when we
        // enable sync in our tempo bar. it should literally tell the actual
        // unit. you are now a slave to our clock."
        //
        // That is the hierarchy, and it took me two passes to get right. There
        // are TWO levels here and they are easy to collapse into one:
        //
        //   1. Where OUR clock comes from - our own BPM field, or a DAW
        //      playhead, or 0xF8 bytes arriving. That is resolved above, and
        //      the result is one pulse stream that never stops.
        //   2. Whether the UNIT is slaved to it. That is syncOn, and what it
        //      disables is the unit's OWN internal timing - its preset delay
        //      time, its rate knobs - which syncedMs/syncedHz override while it
        //      is on. THAT is "external disables internal completely", and it
        //      is about the unit, not about our clock.
        //
        // I briefly stopped the master clock when slaved with nothing external
        // sending, which was the first level collapsed into the second: it made
        // a standalone with no DAW go silent the moment you slaved a unit to
        // our own bar. The bar is the master; it does not need permission.
        const double bpm = hostTempo();
        master.advance (numSamples, bpm, transportPlaying || internalClockRuns());
    }

    /** Whether the machine's OWN clock free-runs while it is not slaved. True by
        default, so a unit jamming with no host still has a tempo and nothing
        stalls waiting for a transport that is not coming.

        Only consulted while sync is OFF. Once the machine is slaved its internal
        clock is off entirely, which is the whole meaning of the switch. */
    virtual bool internalClockRuns() const { return true; }

    /** True when the unit is slaved to our clock AND our clock is itself
        following something outside - a DAW transport, or 0xF8 on a cable. The
        difference matters only for what the header prints; the unit follows the
        same stream either way. */
    bool slavedToExternal() const noexcept { return syncMode() == syncExternal; }
    /** Set to follow an outside master with none sending: nothing is moving and
        it is not the unit's fault. The header shows SYNC ---. */
    bool waitingForClock() const noexcept
    { return syncMode() == syncExternal && ! transportPlaying; }

    /** What THIS machine's panel calls the two states. Every manufacturer named
        the same idea differently and a user is following the unit's manual, not
        ours, so override with the machine's own words - the same principle as
        SyncTarget::ownNames.

        The vocabulary actually found across the surveyed machines:

            INT / EXT              Nord Lead 3's Master Clock menu, and the most
                                   common pair by far
            MASTER / SLAVE         and note the trap below
            Internal / MIDI Clock  Lexicon's PCM 91 Tempo Source
            MIDI CLK               shortened on small displays
            SOURCE                 the parameter's name rather than its values
            Disable / Auto / Send  the Virus, which folds clock OUT into the
                                   same parameter
            HOST                   ours and other plugins', for a DAW transport

        THE TRAP: "MASTER" appears on both sides of this. Some panels label the
        SOURCE they follow as the master ("sync to master"), others label the
        machine's own role. Read which one a manual means before copying its
        word, or a unit will end up saying MASTER while it is the slave.

        THE INVARIANT: our transport bar is ALWAYS the master. It may itself be
        following a DAW or a cable, but the unit is never our master and these
        names never describe our bar - only what the unit is doing about it. */
    virtual juce::String internalClockName() const { return "INT"; }
    virtual juce::String externalClockName() const { return "EXT"; }

    /** THE clock, for anything that wants the pulse rather than the tempo: the
        position, the phase within a division, the pulses in this block. A
        synced LFO should take its phase from here rather than accumulating a
        rate, because a derived phase cannot drift from the master. */
    const MasterClock& clock() const noexcept { return master; }

    /** The incoming side specifically, for a unit that wants the transport
        messages themselves - a looper needing Start to mean bar one, a
        sequencer needing Song Position to jump. Most units want readTransport
        and clock() and nothing else. */
    const MidiClock& incomingClock() const noexcept { return midiClock; }

    void prepareClock (double sampleRate) noexcept
    {
        midiClock.prepare (sampleRate);
        master.prepare (sampleRate);
    }

    /** Send our clock out as real MIDI, so a standalone can master actual
        hardware off the same bar that drives the plugin. The stream a drum
        machine receives from us is the stream we receive from one. */
    void emitClock (juce::MidiBuffer& out, int numSamples) const
    { master.sendTo (out, numSamples); }

    /** Only for a unit whose tempo is NOT an APVTS "bpm" parameter. When the
        parameter exists the strip attaches its BPM field to it instead, which is
        what keeps host automation and the knob on the same one master clock. */
    virtual double sessionBpm() const { return 120.0; }
    virtual void   setSessionBpm (double) {}

    // ---- tempo sync -------------------------------------------------------
    /** Where a synced thing gets its clock. Three states, because that is what
        the panels have and it is not an on/off. The author: "SYNC off literally means
        its not syncing to a CLOCK. sync INT usually means sync to a units
        internal clock. SYNC Ext means sync to another clock, a master clock."

          off       Nothing is synced, and the point is not merely that there is
                    no tempo - it is that nothing in the machine follows
                    anything else. The author: "sync off literally means that
                    lfos/osc/effects are not following each other at all." Each
                    modulator runs at its own stored rate, free, drifting
                    against the others exactly as analogue hardware does. That
                    is a legitimate and often desirable sound; it is not a
                    degraded version of being synced.
          internal  Everything in the unit follows ONE clock generated here, so
                    they are locked to each other in whole musical ratios - an
                    LFO at 1/4 and a delay at 1/8 stay in a fixed relationship
                    forever, whatever the tempo. That mutual coherence is the
                    musical reason sync exists, and it holds with no DAW and no
                    cable anywhere near the thing. The clock is our transport
                    bar's BPM field, or a machine with a tempo source of its own
                    (the Supernova's Arp Speed knob IS its internal clock,
                    64-191 BPM, stored per program).
          external  Synced to a clock from OUTSIDE: a DAW transport, or 0xF8
                    bytes on a cable. If no master is sending, there is nothing
                    to follow and the clock holds - which is what the hardware
                    does and why the header can read SYNC ---.

        Both synced states use the same divisions and the same pulse stream; the
        difference is only which clock is driving it. */
    enum SyncMode { syncOff = 0, syncInternal, syncExternal };

    /** Whether this machine offers the OFF state at all. The author: "almost all
        synths follow this 3 part rule. most effects have the
        internal/external method."

        That split is real and it follows from what the machines are. A synth's
        LFOs and envelopes have their own rates and running them free against
        each other is a sound in its own right, so OFF has to be reachable. An
        effect's timing is always SOMETHING - a delay has a delay time whether
        or not it is following a clock - so its panel offers only the source:
        my own clock, or yours. There is no third thing for it to be.

        Defaults to the machine's shape: a unit with a keyboard gets three
        states, one without gets two. Override where a machine disagrees, which
        some will. */
    virtual bool offersSyncOff() const
    { return const_cast<CoreHost*> (this)->keyboardState() != nullptr; }

    /** The modes this machine offers, in switch order. Two or three. */
    std::vector<int> syncModes() const
    {
        if (offersSyncOff()) return { syncOff, syncInternal, syncExternal };
        return { syncInternal, syncExternal };
    }

    /** The mode, clamped to what this machine actually offers.

        A unit whose panel has no OFF position still starts with the stored mode
        at syncOff, because the default cannot ask a virtual what the machine is.
        Reporting that would put every effects unit into a state its own
        `syncModes()` says does not exist - the header printing SYNC OFF on a
        PCM 91 whose Tempo Source only offers Internal or MIDI Clock, and the
        first click skipping INTERNAL entirely because the cycle could not find
        where it was. So resolve it here, once, rather than in the strip. */
    int syncMode() const noexcept
    {
        if (mode != syncOff) return mode;
        if (offersSyncOff()) return syncOff;

        // A machine with no OFF position has to be on SOME clock, and in a DAW
        // that clock is the DAW. The author: "every synth can sync to an external
        // device. any user that has a daw always sets up their daw to be the
        // master clock. else effects wont be in sync."
        //
        // This defaulted to INTERNAL, which meant an effects plugin dropped
        // into a session ran on its own BPM field while the host played at some
        // other tempo. I had let the wrong question frame it: the survey asked
        // what the 1988 hardware could do, and "only six of eighteen had a MIDI
        // clock" became the default. But that is the mimicry question. Every
        // one of these runs as a plugin, and a host transport is always the
        // master when there is one.
        //
        // So: follow the transport when there is one, fall back to our own
        // clock when there is not. Which is what the Virus calls Auto, so it is
        // a setting real hardware ships rather than an invention.
        return transportPlaying ? syncExternal : syncInternal;
    }
    void setSyncMode (int m)
    {
        int v = juce::jlimit ((int) syncOff, (int) syncExternal, m);
        if (v == syncOff && ! offersSyncOff()) v = syncInternal;
        if (mode == v) return;
        mode = v;
        syncChanged();
    }

    /** True while the machine is slaved. Off by default on every unit: the
        standing rule is sync everything that moves and default it OFF, so
        nothing starts behaving differently from how it did before.

        WHAT IS GLOBAL AND WHAT IS IN THE PROGRAM. The hardware splits these and
        so do we. The SWITCH - internal clock or external - is a system setting
        on every machine that has one: it belongs to the installation, not to the
        sound, and it stays put across a program change. The DIVISIONS are
        musical content and the machines store them in the program (the Virus
        keeps LFO Clock in the patch, Lexicon keeps the rhythmic value in the
        preset), so a unit whose format has somewhere to put them should call
        setSyncDivision as part of loading a program. A unit that does not simply
        keeps the divisions the user set, which is the more useful default for a
        mimic and never surprises anyone. */
    bool syncEnabled() const noexcept { return syncMode() != syncOff; }
    /** Convenience for the common case. Switching on picks EXTERNAL when a
        master is actually sending and INTERNAL otherwise - which is exactly
        what the Virus's Auto setting does, so there is precedent for it being
        the sensible default rather than a fudge. */
    void setSyncEnabled (bool on)
    { setSyncMode (! on ? syncOff : (transportPlaying ? syncExternal : syncInternal)); }

    /** Called whenever the switch or any division moves. Override with the one
        line that marks the unit's parameters dirty; the recompute then happens
        where the unit already recomputes, off the audio thread. */
    virtual void syncChanged() {}

    /** The tempo the SYNCED destinations follow, or 0 when nothing is synced.
        Distinct from hostTempo(), which is what the bar shows and what the
        audition plays at: selecting EXTERNAL with no master sending gives 0
        here - there is genuinely nothing to follow - while the bar carries on
        showing its own BPM. */
    double syncTempo() const noexcept
    {
        switch (syncMode())
        {
            case syncInternal: return internalTempo();
            case syncExternal: return transportPlaying ? transportBpm : 0.0;
            case syncOff:
            default:           return 0.0;
        }
    }

    /** The clock this machine generates itself. Defaults to the transport bar's
        BPM field, which is the internal clock of a plugin. A machine with a
        tempo source of its own overrides it - the Supernova's Arp Speed knob is
        the canonical case, and it is stored per program. */
    virtual double internalTempo() const
    {
        if (auto* p = const_cast<CoreHost*> (this)->parameters().getParameter ("bpm"))
            return juce::jmax (20.0, (double) p->convertFrom0to1 (p->getValue()));
        return juce::jmax (20.0, sessionBpm());
    }

    /** Whether this machine actually offers a mode, so a caller can find out
        BEFORE asking instead of being quietly given a different one. The three
        probes above would all have failed loudly on this rather than measuring
        the wrong clock for a day. */
    bool offersSyncMode (int m) const
    {
        const auto modes = syncModes();
        return std::find (modes.begin(), modes.end(), m) != modes.end();
    }

    /** setSyncMode, but it reports whether the machine honoured the request.
        Returns false when the mode was resolved to another one, in which case
        nothing was changed at all - the caller decides what to do rather than
        silently measuring a state it did not ask for. */
    [[nodiscard]] bool trySetSyncMode (int m)
    {
        if (! offersSyncMode (m)) return false;
        setSyncMode (m);
        return true;
    }

    /** How far the host's position may differ from ours before we re-anchor to
        it. A quarter of a beat: comfortably more than block quantisation and
        rounding, comfortably less than any real jump. */
    static constexpr double kLocateThresholdQuarters = 0.25;

    // ---- the keyboard row --------------------------------------------------
    /** The state the on-screen keys and the QWERTY player drive. Null on a unit
        with no keyboard, which is how the shell knows not to build the row. */
    virtual juce::MidiKeyboardState* keyboardState() { return nullptr; }

    // ---- identity ---------------------------------------------------------
    /** The parameter tree, for the controls the shell owns (BPM, transpose). */
    virtual juce::AudioProcessorValueTreeState& parameters() = 0;

private:
    // Sync state lives HERE, not in the unit. Thirty machines each storing a
    // division their own way is exactly the duplication this package exists to
    // end, and it is why nine effects units shipped with no clock at all.
    int    mode = syncOff;   // an effects unit is put into syncInternal on first use
    double transportBpm = 0.0;
    MidiClock   midiClock;
    MasterClock master;
    bool   transportPlaying = false;
};

} // namespace core
