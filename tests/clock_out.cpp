// THE MIDI CLOCK WE SEND IS ON TIME (Tabula bugs.md B-008). MasterClock::sendTo once spread each block's
// pulses evenly across the block, so the output carried up to a block of jitter: at 140 BPM in 256-sample
// blocks, pulse 24 to pulse 408 measured a quarter of 428.6667 ms against 428.5714. Expected values are
// arithmetic: a pulse every 60 / (bpm x 24) seconds. Every gap within one sample of it, and 384 pulses
// spanning 16 quarters to within one sample, at 90, 120, 140 and 150 BPM (CLAUDE.md: a mirrored host
// tempo so a wrong sum cannot pass by luck), in 256- and 333-sample blocks.
//   synthcore_clock_out              the checks
//   synthcore_clock_out --selftest   plants the old even spread: must fail
#include <core/MidiClock.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int run (bool plant)
{
    int bad = 0;
    const double sr = 48000.0;
    for (const double bpm : { 90.0, 120.0, 140.0, 150.0 })
        for (const int block : { 256, 333 })
        {
            core::MasterClock clk; clk.prepare (sr); clk.devEvenSpread = plant;
            std::vector<long long> at;
            juce::MidiBuffer out;
            for (long long b = 0; at.size() < 420; ++b)
            {
                out.clear(); clk.advance (block, bpm, true); clk.sendTo (out, block);
                for (const auto m : out) if (m.getMessage().isMidiClock()) at.push_back (b * block + m.samplePosition);
            }
            const double gap = sr * 60.0 / (bpm * 24.0);
            double worst = 0.0;
            for (size_t i = 1; i < at.size(); ++i) worst = std::max (worst, std::abs ((double) (at[i] - at[i - 1]) - gap));
            const double span = (double) (at[408] - at[24]), want = 16.0 * sr * 60.0 / bpm;
            const bool ok = worst <= 1.0 + 1e-9 && std::abs (span - want) <= 1.0;
            if (! plant || ! ok)
                std::printf ("%s  %.0f BPM, %d-sample blocks: every gap within %.2f samples of %.3f; pulses 24 to 408 span %.0f samples (%.4f ms a quarter, %.4f wanted)\n",
                             ok ? "ok  " : (plant ? "caught" : "FAIL"), bpm, block, worst, gap, span, span / 16.0 / sr * 1000.0, 60000.0 / bpm);
            bad += ! ok;
        }
    return bad;
}

int main (int argc, char** argv)
{
    if (argc > 1 && std::strcmp (argv[1], "--selftest") == 0)
    {
        const int caught = run (true);
        std::printf ("clock out selftest: the old even spread fails %d of 8 cases (must be > 0). %s\n", caught, caught > 0 ? "caught" : "NOT CAUGHT");
        return caught > 0 ? 0 : 1;
    }
    const int bad = run (false);
    std::printf ("%s: the MIDI clock we send\n", bad == 0 ? "PASS" : "FAIL");
    return bad == 0 ? 0 : 1;
}
