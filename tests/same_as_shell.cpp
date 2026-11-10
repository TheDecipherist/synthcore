// Does core::CoreHost keep time exactly as shell::ShellHost does?
//
// CoreHost is ShellHost cut down to the clock and the transport. This drives
// both with the same input, block by block, and compares every number either
// one exposes about time, bit for bit: the tempo, which clock is in charge,
// the sync mode and its tempo, the master clock's position, ticks, pulses and
// phases, the incoming MIDI clock, and the clock bytes each sends out.
//
// Scenarios: our own BPM at 120, 90, 140 and 150 with tempo changes; a host
// playhead that plays, jumps, loops and changes tempo; MIDI clock (0xF8) at 90
// and 140 with start, stop and continue; and MIDI clock under a stopped host.
// Each at 44.1, 48 and 96 kHz with ragged block sizes from a fixed seed.
//
//   synthcore_same_as_shell             0 differences or it fails
//   synthcore_same_as_shell --selftest  plants two faults in CoreHost's own
//                                       state and requires both to be caught
#include <shell/ShellHost.h>
#include <core/CoreHost.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <cstring>
#include <random>

namespace {

// A processor that exists only to own a parameter tree with "bpm" in it, the
// way every unit's does.
struct Holder : juce::AudioProcessor
{
    Holder() : apvts (*this, nullptr, "S", { std::make_unique<juce::AudioParameterFloat> (
                   juce::ParameterID { "bpm", 1 }, "BPM", juce::NormalisableRange<float> (20.f, 300.f, 0.01f), 120.f) }) {}
    const juce::String getName() const override { return "h"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    void setBpm (double b) { apvts.getParameter ("bpm")->setValueNotifyingHost (
                                 apvts.getParameter ("bpm")->convertTo0to1 ((float) b)); }
    juce::AudioProcessorValueTreeState apvts;
};

// The shell's host, with every pure virtual answered as a synth would.
struct Shell : shell::ShellHost
{
    explicit Shell (Holder& h) : holder (h) {}
    int programCount() const override { return 0; }
    int currentProgram() const override { return 0; }
    void selectProgram (int) override {}
    shell::ProgramInfo programInfo (int) const override { return {}; }
    bool isUserProgram (int) const override { return false; }
    bool isSessionPlaying() const override { return false; }
    void setSessionPlaying (bool) override {}
    bool isSessionLoop() const override { return false; }
    void setSessionLoop (bool) override {}
    int sessionBars() const override { return 2; }
    void setSessionBars (int) override {}
    double sessionPosBeats() const override { return 0; }
    juce::MidiKeyboardState* keyboardState() override { return &keys; }
    juce::String brandLabel() const override { return "test"; }
    juce::AudioProcessorValueTreeState& parameters() override { return holder.apvts; }
    Holder& holder; juce::MidiKeyboardState keys;
};

struct Core : core::CoreHost
{
    explicit Core (Holder& h) : holder (h) {}
    juce::MidiKeyboardState* keyboardState() override { return &keys; }
    juce::AudioProcessorValueTreeState& parameters() override { return holder.apvts; }
    Holder& holder; juce::MidiKeyboardState keys;
};

// A host playhead whose position the scenario moves.
struct Head : juce::AudioPlayHead
{
    bool playing = false; double bpm = 120, ppq = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setIsPlaying (playing); p.setBpm (bpm); p.setPpqPosition (ppq);
        return p;
    }
};

bool same (double a, double b) { return std::memcmp (&a, &b, sizeof a) == 0; }

// A fault must change the state the gate reads. A clock byte arrives only every
// ~1,225 samples at 90 BPM and a block can be 16, so "drop the byte in block N"
// usually drops nothing: drop the FIRST byte at or after block N (bugs.md B-004).
struct Fault { int extraSampleAtBlock = -1; int dropClockByteFromBlock = -1; };

// Runs one scenario through both hosts. Returns the number of blocks that differ.
int run (const char* name, double sr, int scenario, const Fault& fault, bool quiet)
{
    Holder hs, hc;
    Shell s (hs); Core c (hc);
    s.prepareClock (sr); c.prepareClock (sr);
    Head head;
    std::mt19937 rng (1234 + scenario);
    std::uniform_int_distribution<int> blockSize (16, 512);

    const double clockBpm = scenario == 2 ? 90.0 : 140.0;
    const double samplesPerPulse = 60.0 * sr / (clockBpm * 24.0);
    double nextPulse = 0.0; long long t = 0; bool dropped = false;
    int bad = 0, blocks = 1500;
    long long sentS = 0, sentC = 0;          // MIDI clock pulses sent so far, by the shell and the core (B-008)

    for (int b = 0; b < blocks; ++b)
    {
        const int n = blockSize (rng);
        // ---- what the outside world does this block
        if (scenario == 0)                              // our own BPM field
        {
            const double bpm = b < 300 ? 120 : b < 700 ? 90 : b < 1100 ? 140 : 150;
            hs.setBpm (bpm); hc.setBpm (bpm);
        }
        if (scenario == 1)                              // a DAW
        {
            head.playing = b >= 100 && b < 1300;
            head.bpm = b < 600 ? 120 : 150;
            if (head.playing) head.ppq += (double) n * head.bpm / (60.0 * sr);
            if (b == 400) head.ppq = 32.0;              // a locate
            if (b == 800) head.ppq = 4.0;               // a loop wrap
            if (b == 900) head.ppq += 0.1;              // a small drift: no re-anchor
        }
        juce::MidiBuffer ms, mc;
        if (scenario == 2 || scenario == 3)             // MIDI clock on a cable
        {
            if (scenario == 3) { head.playing = false; head.bpm = 120; }
            if (b == 50)  { ms.addEvent (juce::MidiMessage::midiStart(), 0); mc.addEvent (juce::MidiMessage::midiStart(), 0); }
            if (b == 700) { ms.addEvent (juce::MidiMessage::midiStop(), 0);  mc.addEvent (juce::MidiMessage::midiStop(), 0); }
            if (b == 900) { ms.addEvent (juce::MidiMessage::midiContinue(), 0); mc.addEvent (juce::MidiMessage::midiContinue(), 0); }
            while (nextPulse < (double) (t + n))
            {
                const int at = (int) std::llround (nextPulse - (double) t);
                const auto f8 = juce::MidiMessage::midiClock();
                ms.addEvent (f8, juce::jlimit (0, n - 1, at));
                if (fault.dropClockByteFromBlock >= 0 && b >= fault.dropClockByteFromBlock && ! dropped) dropped = true;
                else mc.addEvent (f8, juce::jlimit (0, n - 1, at));
                nextPulse += samplesPerPulse;
            }
        }
        juce::AudioPlayHead* ph = (scenario == 1 || scenario == 3) ? &head : nullptr;
        const int nc = b == fault.extraSampleAtBlock ? n + 1 : n;

        s.readTransport (ph, (const juce::MidiBuffer&) ms, n);
        c.readTransport (ph, (const juce::MidiBuffer&) mc, nc);

        juce::MidiBuffer os, oc;
        s.emitClock (os, n); c.emitClock (oc, n);

        // ---- compare everything about time
        const auto& A = s.clock(); const auto& B = c.clock();
        bool ok = same (s.hostTempo(), c.hostTempo())
               && s.followingExternalClock() == c.followingExternalClock()
               && s.syncMode() == c.syncMode()
               && same (s.syncTempo(), c.syncTempo())
               && same (s.internalTempo(), c.internalTempo())
               && s.slavedToExternal() == c.slavedToExternal()
               && s.waitingForClock() == c.waitingForClock()
               && same (A.ppqPosition(), B.ppqPosition())
               && same (A.tickPosition(), B.tickPosition())
               && same (A.blockStartPpq(), B.blockStartPpq())
               && A.pulses() == B.pulses()
               && s.incomingClock().hasClock() == c.incomingClock().hasClock()
               && s.incomingClock().isRunning() == c.incomingClock().isRunning()
               && same (s.incomingClock().bpm(), c.incomingClock().bpm())
               && same (s.incomingClock().ppqPosition(), c.incomingClock().ppqPosition());
        // THE PULSES WE SEND (changed after the proof, B-008, CUTS.md): the core puts each at its true sample, so
        // a pulse can land a block later than the shell's even spread put it. Here: the same pulses, at most one
        // carried into the next block; where in the block is tests/clock_out.cpp's to prove. Every other
        // message (Start, Stop) still the shell's, bit for bit.
        int f8s = 0, f8c = 0;
        juce::MidiBuffer xs, xc;
        for (const auto m : os) { if (m.getMessage().isMidiClock()) ++f8s; else xs.addEvent (m.getMessage(), m.samplePosition); }
        for (const auto m : oc) { if (m.getMessage().isMidiClock()) ++f8c; else xc.addEvent (m.getMessage(), m.samplePosition); }
        sentS += f8s; sentC += f8c;
        ok = ok && std::abs (sentS - sentC) <= 1 && xs.getNumEvents() == xc.getNumEvents();
        for (int d : { 1, 8, 24, 96, 384 })
            ok = ok && same (A.phase (d), B.phase (d)) && same (A.phaseAt (d, n / 2, s.hostTempo()), B.phaseAt (d, n / 2, c.hostTempo()));
        if (ok)
        {
            auto i = xs.begin(), j = xc.begin();
            for (; i != xs.end() && j != xc.end(); ++i, ++j)
                ok = ok && (*i).samplePosition == (*j).samplePosition
                        && (*i).getMessage().getRawDataSize() == (*j).getMessage().getRawDataSize()
                        && std::memcmp ((*i).data, (*j).data, (size_t) (*i).numBytes) == 0;
        }
        if (! ok && ! bad++ && ! quiet)
            std::printf ("    %s @ %.0f Hz: first difference at block %d: tempo %.6f / %.6f, ppq %.9f / %.9f\n",
                         name, sr, b, s.hostTempo(), c.hostTempo(), A.ppqPosition(), B.ppqPosition());
        t += n;
    }
    return bad;
}

int all (const Fault& f, bool quiet)
{
    const char* names[] = { "own BPM 120/90/140/150", "host plays, jumps, loops", "MIDI clock 90", "MIDI clock 140, host stopped" };
    int total = 0, checks = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int sc = 0; sc < 4; ++sc)
        {
            const int bad = run (names[sc], sr, sc, f, quiet);
            if (! quiet) std::printf ("  %-30s %6.0f Hz: %s\n", names[sc], sr, bad ? "DIFFERENT" : "identical, 1500 blocks");
            total += bad; ++checks;
        }
    if (! quiet) std::printf ("\n  %d runs, %d blocks differ\n", checks, total);
    return total;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc > 1 && std::strcmp (argv[1], "--selftest") == 0)
    {
        int wrong = 0;
        Fault extra;  extra.extraSampleAtBlock = 777;
        Fault drop;   drop.dropClockByteFromBlock = 333;
        const int e = all (extra, true), d = all (drop, true), z = all (Fault {}, true);
        std::printf ("  an extra sample at block 777: %d blocks differ (must be > 0)\n", e);
        std::printf ("  a lost clock byte at block 333: %d blocks differ (must be > 0)\n", d);
        std::printf ("  no fault: %d blocks differ (must be 0)\n", z);
        wrong += e == 0;     // one extra sample in CoreHost's master clock
        wrong += d == 0;     // one 0xF8 lost before CoreHost's MIDI clock
        wrong += z != 0;     // and no fault: nothing may differ
        std::printf ("same_as_shell selftest: 3 cases, %d wrong (an extra sample, a lost clock byte, no fault)\n", wrong);
        return wrong ? 1 : 0;
    }
    return all (Fault {}, false) ? 1 : 0;
}
