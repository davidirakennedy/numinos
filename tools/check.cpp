// numin-check — automated safety tests for every Numin (or the ones you name).
//
//   numin-check            # all Numins
//   numin-check Honey      # just these
//
// Each Numin is run at several sample rates with silence, noise and impulses,
// at default, minimum, maximum and random parameter settings. It fails if:
//   * output contains NaN or infinity
//   * output differs depending on block size (a classic state bug)
//   * output differs after reset() (state that reset() forgot to clear)
//   * in-place processing (in == out) gives a different result
// A Numin that listens to a sidechain gets all of that twice: with nothing connected, and with a
// sidechain playing. And it warns (without failing) if output gets very loud.
//
// Exit code is non-zero on failure, so CI can run it on every push.

#include "Registry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace numinos;

namespace
{

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo(size_t n = 0) : l(n), r(n) {}
};

Stereo makeInput(size_t frames, unsigned seed)
{
    // silence, an impulse, then noise at -6 dBFS, then a loud sine burst
    Stereo s(frames);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    for (size_t i = 0; i < frames; ++i)
    {
        float x = 0;
        if (i == frames / 8)
            x = 1.0f;
        else if (i > frames / 4 && i < frames * 3 / 4)
            x = u(rng);
        else if (i >= frames * 3 / 4)
            x = 0.99f * std::sin((float)i * 0.05f);
        s.l[i] = x;
        s.r[i] = i > frames / 4 ? u(rng) : x;
    }
    return s;
}

// Process `in` with a given block-size pattern. blockSize <= 0 means random sizes. `sidechain`,
// if given, is handed over block by block, lined up with the input.
Stereo run(Numin& n, const Stereo& in, int blockSize, const Stereo* sidechain, unsigned seed = 1)
{
    Stereo out(in.l.size());
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> sizes(1, 700);
    for (size_t pos = 0; pos < in.l.size();)
    {
        int bs = blockSize > 0 ? blockSize : sizes(rng);
        int count = (int)std::min<size_t>(bs, in.l.size() - pos);
        n.setSidechain(sidechain ? &sidechain->l[pos] : nullptr, sidechain ? &sidechain->r[pos] : nullptr);
        n.process(&in.l[pos], &in.r[pos], &out.l[pos], &out.r[pos], count);
        pos += count;
    }
    return out;
}

double maxDiff(const Stereo& a, const Stereo& b)
{
    double d = 0;
    for (size_t i = 0; i < a.l.size(); ++i)
        d = std::max({d, (double)std::fabs(a.l[i] - b.l[i]), (double)std::fabs(a.r[i] - b.r[i])});
    return d;
}

bool allFinite(const Stereo& s)
{
    for (size_t i = 0; i < s.l.size(); ++i)
        if (!std::isfinite(s.l[i]) || !std::isfinite(s.r[i]))
            return false;
    return true;
}

double peak(const Stereo& s)
{
    double p = 0;
    for (size_t i = 0; i < s.l.size(); ++i)
        p = std::max({p, (double)std::fabs(s.l[i]), (double)std::fabs(s.r[i])});
    return p;
}

struct Result
{
    int failures = 0, warnings = 0;
    void fail(const std::string& msg) { std::printf("    FAIL  %s\n", msg.c_str()), ++failures; }
    void warn(const std::string& msg) { std::printf("    warn  %s\n", msg.c_str()), ++warnings; }
};

Result checkNumin(const NuminEntry& entry)
{
    Result res;
    auto n = entry.create();
    std::printf("%s\n", entry.name);

    const double rates[] = {44100, 48000, 96000};
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);

    // parameter sets: defaults, all-min, all-max, then random
    std::vector<std::vector<float>> sets;
    std::vector<float> defaults;
    for (int i = 0; i < n->numParams(); ++i)
        defaults.push_back(n->paramInfo(i).defaultValue);
    sets.push_back(defaults);
    sets.push_back(std::vector<float>(n->numParams(), 0.0f));
    sets.push_back(std::vector<float>(n->numParams(), 1.0f));
    for (int k = 0; k < 6; ++k)
    {
        std::vector<float> s;
        for (int i = 0; i < n->numParams(); ++i)
            s.push_back(u(rng));
        sets.push_back(s);
    }
    const char* setNames[] = {"defaults", "all 0", "all 1"};

    double worstSpeed = 1e9;
    const int sidechainModes = n->usesSidechain() ? 2 : 1; // without, then with
    for (double sr : rates)
        for (int withSidechain = 0; withSidechain < sidechainModes; ++withSidechain)
    {
        const Stereo input = makeInput((size_t)(sr * 0.5), (unsigned)sr);
        const Stereo sidechainInput = makeInput((size_t)(sr * 0.5), (unsigned)sr + 7);
        const Stereo* sidechain = withSidechain ? &sidechainInput : nullptr;
        for (size_t s = 0; s < sets.size(); ++s)
        {
            char where[128];
            std::snprintf(where, sizeof(where), "@%.0f Hz, params %s%s", sr,
                          s < 3 ? setNames[s] : ("random #" + std::to_string(s - 2)).c_str(),
                          withSidechain ? ", with sidechain" : "");

            for (int i = 0; i < n->numParams(); ++i)
                n->setParam(i, sets[s][i]);

            n->prepare(sr);
            auto t0 = std::chrono::steady_clock::now();
            Stereo ref = run(*n, input, 512, sidechain);
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            worstSpeed = std::min(worstSpeed, (input.l.size() / sr) / std::max(secs, 1e-9));

            if (!allFinite(ref))
            {
                res.fail(std::string("NaN/inf in output ") + where);
                continue;
            }
            if (peak(ref) > 16.0) // +24 dBFS
                res.warn(std::string("output peaks above +24 dBFS ") + where);

            n->reset();
            Stereo again = run(*n, input, 512, sidechain);
            if (maxDiff(ref, again) > 1e-6)
            {
                res.fail(std::string("output changes after reset() — some state isn't cleared ") + where);
                continue; // the tests below rely on reset() working
            }

            n->reset();
            Stereo jagged = run(*n, input, 0, sidechain);
            if (maxDiff(ref, jagged) > 1e-5)
            {
                res.fail(std::string("output depends on block size (per-block smoothing or state?) ") + where);
                continue;
            }

            n->reset();
            Stereo inPlace = input;
            for (size_t pos = 0; pos < inPlace.l.size(); pos += 512)
            {
                int c = (int)std::min<size_t>(512, inPlace.l.size() - pos);
                n->setSidechain(sidechain ? &sidechain->l[pos] : nullptr, sidechain ? &sidechain->r[pos] : nullptr);
                n->process(&inPlace.l[pos], &inPlace.r[pos], &inPlace.l[pos], &inPlace.r[pos], c);
            }
            if (maxDiff(ref, inPlace) > 1e-6)
                res.fail(std::string("in-place processing differs (reading input after writing output?) ") + where);
        }
    }

    // Display strings shouldn't crash or be empty.
    for (int i = 0; i < n->numParams(); ++i)
        for (float v : {0.0f, 0.5f, 1.0f})
            if (n->displayValue(i, v).empty())
                res.warn(std::string("empty display text for ") + n->paramInfo(i).name);

    if (res.failures == 0)
        std::printf("    ok    (%d warnings, %.0fx realtime worst case)\n", res.warnings, worstSpeed);
    return res;
}

} // namespace

int main(int argc, char** argv)
{
    int failures = 0, checked = 0;
    for (const auto& e : allNumins())
    {
        bool wanted = argc < 2;
        for (int a = 1; a < argc; ++a)
            wanted |= std::string(argv[a]) == e.name;
        if (!wanted)
            continue;
        failures += checkNumin(e).failures;
        ++checked;
    }
    if (checked == 0)
    {
        std::fprintf(stderr, "No matching Numins.\n");
        return 2;
    }
    std::printf("\n%d Numin(s) checked, %d failure(s)\n", checked, failures);
    return failures ? 1 : 0;
}
