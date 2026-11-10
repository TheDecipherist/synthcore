// The note divisions a tempo-locked thing follows, and the one place they are
// named. Shared because every machine wants the same vocabulary: a 1/8 dotted
// is a 1/8 dotted on a delay, an LFO, a chopper or an arpeggiator.
//
// Why this exists at all: the effects units were measured on 2026-09-08 and NONE
// of the nine had its engine wired to the master clock. The BPM field drove the
// audition source and the bar readout and nothing else, so a delay ran at
// whatever its preset stored no matter what the transport said. Playbook section
// 19 had required the opposite from the start ("BPM bound to the one master
// clock, because delays and modulation sync to it") and nobody had checked.
#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

namespace core {

// TWO CLOCKS, AND THIS HEADER IS ONLY ABOUT ONE OF THEM.
//
// Everything here is the TEMPO clock: MIDI real-time F8 at 24 per quarter note,
// a DIN pulse, or a tap. It decides where the beat is.
//
// A digital machine also has an AUDIO clock - 44.1/48/88.2/96 kHz, Word Clock
// in, Digital In, AES/EBU, S/PDIF, ADAT, a "Clock Mode" of Normal or Double. It
// decides how fast samples are converted and it does not know what a bar is.
// The two have nothing to do with each other.
//
// This matters when reading a machine's manual to decide what to wire up here,
// because on a 90s digital rack unit nearly every occurrence of the word
// "Clock" is the AUDIO one. The TC Reverb 4000's I/O menu offers Internal,
// Digital In and Word Clock, which reads like three kinds of sync and is three
// kinds of sample clock; that box cannot receive an F8 at all. The SDE-330 went
// the other way: it was wired up here with two destinations and nobody checked
// the hardware, and its ROM turned out to carry MIDICLK DELAY, Tempo Source and
// five tempo algorithms the whole time.
//
// So never grep a manual for "clock". Grep the MIDI implementation chart's
// System Real Time row, and the firmware for Tempo, BPM, SYNC, MIDI CLK and
// note values. A machine with tempo sync always has at least one parameter in
// note values or a named tempo source.
//
// ~/projects/eproms/docs/midi-law.md section 0 carries this in full, including
// why a reverb being built from delay lines is NOT an argument that it syncs.
struct TempoSync
{
    /** Off first, then the original ten longest-to-shortest, then everything a
        real machine turned out to need, APPENDED. Never reordered and never
        renumbered: a unit stores the index, so moving one changes what a saved
        preset means. Use displayOrder() to get them musically sorted for a menu.

        The tail exists because a survey of eighteen machines found our ten were
        not enough for any of them. The Virus wants 1/32, 1/64, 1/2T, 1/1T and
        bars up to 16; the JP-8000 uses eight of ours and needs eight more,
        including 3-8 MEASURES; the Nord Lead 3 goes to 32 bars. A machine whose
        own division is missing cannot be followed from its own manual, which is
        the whole point of naming them. */
    enum Division { off = 0, whole, half, dottedQuarter, quarter, dottedEighth,
                    tripletQuarter, eighth, tripletEighth, sixteenth, tripletSixteenth,
                    // appended 2026-09-08 from the eighteen-machine survey
                    thirtySecond, sixtyFourth, tripletThirtySecond,
                    dottedSixteenth, dottedHalf, dottedWhole,
                    tripletHalf, tripletWhole,
                    twoBars, tripletTwoBars, threeBars, fourBars, fiveBars,
                    sixBars, sevenBars, eightBars, sixteenBars, thirtyTwoBars };
    static constexpr int count = 29;

    static juce::StringArray names()
    { return { "OFF", "1/1", "1/2", "1/4.", "1/4", "1/8.",
               "1/4T", "1/8", "1/8T", "1/16", "1/16T",
               "1/32", "1/64", "1/32T",
               "1/16.", "1/2.", "1/1.",
               "1/2T", "1/1T",
               "2 BAR", "2 BAR T", "3 BAR", "4 BAR", "5 BAR",
               "6 BAR", "7 BAR", "8 BAR", "16 BAR", "32 BAR" }; }

    static constexpr int kPPQN = 24;              // clocks per quarter note [MIDI]
    static constexpr int kClocksPerMidiBeat = 6;  // a "MIDI beat" is a 16th [MIDI]

    /** THE table, in ticks at 192 per quarter note. Not in MIDI clocks, and the
        reason is the survey: the Virus's own division table is in 1/192 units
        and needs 1/64, which is one and a half MIDI clocks. A machine with an
        internal PLL divides between pulses perfectly well, so the table has to
        be finer than the wire. 192 is 8 MIDI clocks and every division any of
        the eighteen machines uses is a whole number of them. */
    static constexpr int kTicksPerQuarter = 192;

    static int ticks (int division) noexcept
    {
        switch (juce::jlimit (0, count - 1, division))
        {
            case sixtyFourth:        return 12;
            case tripletThirtySecond:return 16;
            case thirtySecond:       return 24;
            case tripletSixteenth:   return 32;
            case sixteenth:          return 48;
            case tripletEighth:      return 64;
            case dottedSixteenth:    return 72;
            case eighth:             return 96;
            case tripletQuarter:     return 128;
            case dottedEighth:       return 144;
            case quarter:            return 192;
            case tripletHalf:        return 256;
            case dottedQuarter:      return 288;
            case half:               return 384;
            case tripletWhole:       return 512;
            case dottedHalf:         return 576;
            case whole:              return 768;   // one 4/4 bar
            case tripletTwoBars:     return 1024;
            case dottedWhole:        return 1152;
            case twoBars:            return 1536;
            case threeBars:          return 2304;
            case fourBars:           return 3072;
            case fiveBars:           return 3840;
            case sixBars:            return 4608;
            case sevenBars:          return 5376;
            case eightBars:          return 6144;
            case sixteenBars:        return 12288;
            case thirtyTwoBars:      return 24576;
            case off:
            default:                 return 0;
        }
    }

    /** The division in MIDI Timing Clocks. Fractional for 1/64 and 1/32T, which
        fall BETWEEN pulses - a slave with an internal divider follows them, one
        counting raw pulses cannot. landsOnClockGrid() is the test. */
    static double clocks (int division) noexcept
    { return (double) ticks (division) / 8.0; }

    static bool landsOnClockGrid (int division) noexcept
    { return ticks (division) % 8 == 0; }

    /** Length of one division in beats (quarter notes). 0 when sync is off, so
        `beats(d) > 0` is the "is it synced" test everywhere. */
    static double beats (int division) noexcept
    { return (double) ticks (division) / (double) kTicksPerQuarter; }

    /** The tempo a stream of Timing Clocks is running at, which is the only way
        a slave ever learns the tempo: measure the interval between 0xF8 bytes.
        A host transport hands us a BPM directly, so this is for a standalone
        taking real clock off a MIDI input. */
    static double bpmFromClockInterval (double secondsBetweenClocks) noexcept
    {
        if (secondsBetweenClocks <= 0.0) return 0.0;
        return 60.0 / (secondsBetweenClocks * (double) kPPQN);
    }

    /** The divisions sorted longest to shortest, for a menu. The enum is
        append-only so its order is now the order things were discovered in;
        this is the order a musician expects to read. OFF first. */
    static std::vector<int> displayOrder()
    {
        std::vector<int> v;
        v.reserve ((size_t) count);
        for (int i = 0; i < count; ++i) v.push_back (i);
        std::sort (v.begin() + 1, v.end(),
                   [] (int a, int b) { return ticks (a) > ticks (b); });
        return v;
    }

    /** Song Position Pointer (0xF2) counts 16th notes since the start of the
        song as a 14-bit value, LSB first. Continue (0xFB) resumes from it;
        Start (0xFA) always means bar one, beat one. */
    static double beatsFromSongPosition (int midiBeats) noexcept
    { return (double) midiBeats * (double) kClocksPerMidiBeat / (double) kPPQN; }

    /** The division in milliseconds at a tempo. 0 when off. A quarter at 120 BPM
        is 500 ms, which is the number to check a synced delay against. */
    static double milliseconds (int division, double bpm) noexcept
    {
        const double b = beats (division);
        return b <= 0.0 ? 0.0 : b * 60000.0 / juce::jmax (20.0, bpm);
    }

    /** The division in Hz, for an LFO or a tremolo locked to the clock. */
    static double hertz (int division, double bpm) noexcept
    {
        const double ms = milliseconds (division, bpm);
        return ms <= 0.0 ? 0.0 : 1000.0 / ms;
    }
};

} // namespace core
