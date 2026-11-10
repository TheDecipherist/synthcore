// MIDI clock, as the standard actually defines it: a stream of pulses.
//
// There is no tempo message in MIDI. None. The master fires a Timing Clock byte
// (0xF8) twenty-four times per quarter note, forever, and the tempo IS how fast
// they arrive - faster pulses, faster tempo. A slave never receives "120 BPM";
// it measures the gap between pulses and works it out. Everything else in the
// spec is transport: Start (0xFA) jumps to the beginning and runs, Continue
// (0xFB) resumes from where it was, Stop (0xFC) halts, and Song Position Pointer
// (0xF2) says where "where it was" is, counting sixteenth notes.
//
// This matters here because it is the difference between a plugin that follows a
// DAW and a machine that can be slaved to anything. Inside a host we get the
// transport handed to us; a standalone on a MIDI cable from a drum machine gets
// nothing but these bytes, and until it counted them our units could not be
// slaved at all - which is what every one of these boxes does for a living.
//
// Both paths end at the same place: shell::ShellHost::setTransportTempo. One
// clock, whatever it arrived as.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "TempoSync.h"

namespace core {

/** Counts incoming MIDI clock and reports the tempo and transport it implies.
    Fed from processBlock, so every method here is real-time safe: no allocation,
    no locking, no logging. */
class MidiClock
{
public:
    /** The System Real-Time and System Common bytes this cares about. Status
        bytes, so they carry no channel and can appear between the bytes of
        another message - which is why a running-status parser must not eat
        them, and why JUCE hands them over as whole messages. */
    static constexpr int kClock    = 0xF8;
    static constexpr int kStart    = 0xFA;
    static constexpr int kContinue = 0xFB;
    static constexpr int kStop     = 0xFC;
    static constexpr int kSongPos  = 0xF2;

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        samplesSinceClock = 0.0;
        haveTempo = false;
        bpmNow = 0.0;
        running = false;
        pulses = 0;
        startPulse = 0;
        for (auto& g : gaps) g = 0.0;
        gapCount = 0;
        gapAt = 0;
    }

    /** Feed one block. `numSamples` advances the pulse-gap measurement even when
        no clock arrives, so a master that stops sending is noticed rather than
        leaving the last tempo frozen forever. */
    void process (const juce::MidiBuffer& midi, int numSamples) noexcept
    {
        int last = 0;
        for (const auto meta : midi)
        {
            const auto& m = meta.getMessage();
            const int at = juce::jlimit (0, numSamples, meta.samplePosition);
            samplesSinceClock += (double) (at - last);
            last = at;

            if (m.isMidiClock())                 handleClock();
            else if (m.isMidiStart())            { running = true;  pulses = startPulse = 0; }
            else if (m.isMidiContinue())         { running = true; }
            else if (m.isMidiStop())             { running = false; }
            else if (m.isSongPositionPointer())
            {
                // SPP counts MIDI beats - sixteenths - six clocks each. It is
                // sent while stopped and takes effect on the next Continue.
                pulses = m.getSongPositionPointerMidiBeat() * TempoSync::kClocksPerMidiBeat;
                startPulse = pulses;
            }
        }
        samplesSinceClock += (double) (numSamples - last);

        // No pulse for half a second means nobody is driving us any more.
        if (haveTempo && samplesSinceClock > sr * 0.5)
        { haveTempo = false; running = false; }
    }

    /** True when a master really is sending clock, so the caller knows whether
        to prefer this over its own BPM field. */
    bool hasClock() const noexcept { return haveTempo; }
    /** True between Start/Continue and Stop. */
    bool isRunning() const noexcept { return running; }
    /** The tempo the pulse rate implies, 0 when there is no clock. */
    double bpm() const noexcept { return haveTempo ? bpmNow : 0.0; }
    /** Position in quarter notes since the last Start, for a playhead. */
    double ppqPosition() const noexcept
    { return (double) pulses / (double) TempoSync::kPPQN; }

private:
    void handleClock() noexcept
    {
        ++pulses;
        if (samplesSinceClock > 0.0)
        {
            // A median of the last few gaps, not the last one. MIDI over a
            // 31250-baud cable and a block-quantised callback both jitter the
            // arrival time by a sample or two, and a tempo readout that twitches
            // makes every synced delay twitch with it.
            gaps[gapAt] = samplesSinceClock;
            gapAt = (gapAt + 1) % kGaps;
            if (gapCount < kGaps) ++gapCount;

            double sorted[kGaps];
            for (int i = 0; i < gapCount; ++i) sorted[i] = gaps[i];
            for (int i = 1; i < gapCount; ++i)      // insertion sort, <= 8 items
            {
                const double v = sorted[i];
                int j = i - 1;
                for (; j >= 0 && sorted[j] > v; --j) sorted[j + 1] = sorted[j];
                sorted[j + 1] = v;
            }
            const double med = sorted[gapCount / 2];

            const double b = TempoSync::bpmFromClockInterval (med / sr);
            if (b >= 20.0 && b <= 400.0) { bpmNow = b; haveTempo = true; }
        }
        samplesSinceClock = 0.0;
    }

    static constexpr int kGaps = 8;
    double sr = 48000.0;
    double samplesSinceClock = 0.0;
    double bpmNow = 0.0;
    bool   haveTempo = false;
    bool   running = false;
    int    pulses = 0, startPulse = 0;
    double gaps[kGaps] {};
    int    gapCount = 0, gapAt = 0;
};

/** THE master clock: one pulse stream, whatever is driving it.

    This is the part that makes the transport bar a real master rather than a
    number. The author: "what our bar sends is a standard. its midi clock. pure and
    simple. what the units receive is always the exact same thing... And this
    will be the exact same thing when it comes from a DAW as well."

    So the three masters converge HERE, not in each unit. Our own BPM field, a
    DAW's playhead and incoming 0xF8 bytes are three ways of learning a tempo,
    and all three come out of this as the identical thing: a position advancing
    at 24 pulses per quarter note. A unit downstream cannot tell which kind of
    master it is following and has no reason to want to.

    It also runs the other way. A standalone can SEND its clock to real hardware
    with sendTo(), which is the same stream again - our bar mastering a drum
    machine is the same object as our bar following one. */
class MasterClock
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        tickPos = 0.0;
        blockStartTicks = 0.0;
        pulsesThisBlock = 0;
        lastPulse = 0;
        carried = false;
    }

    /** Advance by a block at `bpm`. `running` false holds the position, which
        is what Stop means: the clock stops, it does not rewind. */
    void advance (int numSamples, double bpm, bool running) noexcept
    {
        // Where this block STARTS. Everything a unit reads during the block
        // corresponds to this position, not to where the block ends.
        blockStartTicks = tickPos;
        pulsesThisBlock = 0;
        if (! running || bpm < 20.0) return;

        // Ticks at 192 per quarter, because the division table is: a machine
        // with an internal divider follows 1/64, which is one and a half MIDI
        // clocks, and the position has to be able to express that.
        const double perSample = bpm * (double) TempoSync::kTicksPerQuarter / (60.0 * sr);
        blockPerSample = perSample;
        const int before = (int) (tickPos / kTicksPerPulse);
        tickPos += perSample * (double) numSamples;
        const int after = (int) (tickPos / kTicksPerPulse);
        pulsesThisBlock = juce::jmax (0, after - before);
        lastPulse = after;
    }

    /** Jump the position - Start means bar one, Song Position means somewhere
        specific. Both are "put the clock here", so they are one method. */
    void locate (double quarterNotes) noexcept
    {
        tickPos = juce::jmax (0.0, quarterNotes) * (double) TempoSync::kTicksPerQuarter;
        blockStartTicks = tickPos;
        lastPulse = (int) (tickPos / kTicksPerPulse);
        pulsesThisBlock = 0;
    }

    /** Position in quarter notes, for a playhead, a bar readout, or anything
        that needs to know WHERE rather than how fast. */
    double ppqPosition() const noexcept
    { return tickPos / (double) TempoSync::kTicksPerQuarter; }

    /** Position in ticks at 192 per quarter - the unit the divisions use, so a
        synced LFO's phase is (tickPosition() mod ticks(division)) / ticks. */
    double tickPosition() const noexcept { return tickPos; }

    /** How many 0xF8 boundaries this block crossed, for anything that wants to
        step on the pulse rather than interpolate. */
    int pulses() const noexcept { return pulsesThisBlock; }

    /** Where a synced thing is within its division at the START of this block,
        0..1. This is what a synced LFO wants: give it the division and it comes
        out locked to the master, because the phase is DERIVED from the clock
        position rather than accumulated from a rate.

        BLOCK START, deliberately, and this caught a subtle one. readTransport
        advances the clock before a unit's processBlock runs, so the raw
        position is where the NEXT block begins. Anchoring a modulator to that
        puts every synced LFO one block - around 10 ms - past the beat, forever,
        on every unit. Reporting the block's own start makes the obvious usage
        correct and stops thirty units each subtracting a block by hand. */
    double phase (int division) const noexcept
    {
        const int t = TempoSync::ticks (division);
        if (t <= 0) return 0.0;
        const double p = std::fmod (blockStartTicks, (double) t) / (double) t;
        return p < 0.0 ? p + 1.0 : p;
    }

    /** The phase a given sample offset into this block, for a unit that wants
        sample-accurate modulation rather than one value per block. */
    double phaseAt (int division, int sampleOffset, double bpm) const noexcept
    {
        const int t = TempoSync::ticks (division);
        if (t <= 0) return 0.0;
        const double per = bpm * (double) TempoSync::kTicksPerQuarter / (60.0 * sr);
        const double p = std::fmod (blockStartTicks + per * (double) sampleOffset,
                                    (double) t) / (double) t;
        return p < 0.0 ? p + 1.0 : p;
    }

    /** Position in quarter notes at the start of this block, to pair with
        phase(). ppqPosition() is where the block ENDS. */
    double blockStartPpq() const noexcept
    { return blockStartTicks / (double) TempoSync::kTicksPerQuarter; }

    /** Write this clock out as real MIDI, so a standalone can master actual
        hardware. Pulses are spread across the block at their true positions
        rather than bunched at sample zero, because a drum machine measures the
        gaps between them to work out our tempo - the same way we measure a
        master's. */
    void sendTo (juce::MidiBuffer& out, int numSamples, bool sendStart = false,
                 bool sendStop = false) const
    {
        if (sendStart) out.addEvent (juce::MidiMessage::midiStart(), 0);
        if (sendStop)  out.addEvent (juce::MidiMessage::midiStop(), 0);
        // EACH PULSE AT THE SAMPLE ITS TICK IS CROSSED, ROUNDED (Tabula B-008: spreading them evenly gave a
        // block of jitter). One that rounds to the block's end goes out at sample 0 of the next block
        if (carried && numSamples > 0) out.addEvent (juce::MidiMessage::midiClock(), 0);
        carried = false;
        if (pulsesThisBlock <= 0 || numSamples <= 0) return;
        const int first = lastPulse - pulsesThisBlock + 1;
        for (int i = 0; i < pulsesThisBlock; ++i)
        {
            const double at = devEvenSpread ? (double) numSamples / (double) pulsesThisBlock * (double) i
                                            : ((double) (first + i) * kTicksPerPulse - blockStartTicks) / blockPerSample;
            if (! devEvenSpread && std::llround (at) >= numSamples) { carried = true; continue; }
            out.addEvent (juce::MidiMessage::midiClock(), juce::jlimit (0, numSamples - 1, (int) std::llround (at)));
        }
    }

private:
    static constexpr double kTicksPerPulse
        = (double) TempoSync::kTicksPerQuarter / (double) TempoSync::kPPQN;   // 8

    double sr = 48000.0;
    double tickPos = 0.0;
    double blockStartTicks = 0.0;
    int    pulsesThisBlock = 0;
    int    lastPulse = 0;
    double blockPerSample = 1.0;
    mutable bool carried = false;   // a pulse rounded onto the next block's first sample
public:
    bool devEvenSpread = false;   // a planted fault for tests/clock_out.cpp: the old even spread
};

} // namespace core
