// numin-render — run a WAV file through a Numin from the terminal.
//
//   numin-render --list
//   numin-render Honey --info
//   numin-render Honey in.wav [out.wav] [Viscosity=0.8 ...] [options]
//
// Options:
//   --sweep NAME     sweep that parameter 0 -> 1 across the whole file
//   --ab SECONDS     alternate dry/wet every SECONDS (quick A/B listening)
//   --block N        block size (default 512)
//   --tail SECONDS   extra silence after the input, to hear reverb/delay tails
//   --sidechain WAV  a second file for Numins that listen to another track (same sample rate)
//
// No JUCE here: WAV I/O is dr_wav, so this builds in seconds.

#include "Registry.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace numinos;

namespace
{

std::string squash(std::string s) // "Output Gain" -> "outputgain"
{
    std::string r;
    for (char c : s)
        if (c != ' ' && c != '_' && c != '-')
            r += (char)std::tolower((unsigned char)c);
    return r;
}

int findParam(const Numin& n, const std::string& key)
{
    for (int i = 0; i < n.numParams(); ++i)
        if (squash(n.paramInfo(i).name) == squash(key))
            return i;
    if (key.size() >= 2 && (key[0] == 'p' || key[0] == 'P')) // p1, p2, ...
    {
        int i = std::atoi(key.c_str() + 1) - 1;
        if (i >= 0 && i < n.numParams())
            return i;
    }
    return -1;
}

void printInfo(const Numin& n)
{
    std::printf("%s  [%s]\n  %s\n\n", n.name(), n.category(), n.blurb());
    for (int i = 0; i < n.numParams(); ++i)
    {
        auto p = n.paramInfo(i);
        std::printf("  p%-2d %-14s default %.2f  (%s)\n", i + 1, p.name, p.defaultValue,
                    n.displayValue(i, p.defaultValue).c_str());
    }
}

double toDb(double x) { return x > 0 ? 20.0 * std::log10(x) : -INFINITY; }

struct Stats
{
    double peak = 0, sumSq = 0;
    long count = 0, nonFinite = 0;
    void add(float x)
    {
        if (!std::isfinite(x))
        {
            ++nonFinite;
            return;
        }
        peak = std::max(peak, (double)std::fabs(x));
        sumSq += (double)x * x;
        ++count;
    }
    double rmsDb() const { return toDb(std::sqrt(sumSq / std::max(1L, count))); }
};

int usage()
{
    std::fprintf(stderr,
                 "usage: numin-render --list\n"
                 "       numin-render <Numin> --info\n"
                 "       numin-render <Numin> <in.wav> [out.wav] [Param=0..1 ...]\n"
                 "                    [--sweep Param] [--ab seconds] [--block N] [--tail seconds]\n"
                 "                    [--sidechain other.wav]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
        return usage();

    if (std::strcmp(argv[1], "--list") == 0)
    {
        for (const auto& e : allNumins())
            std::printf("%-20s %s\n", e.name, e.category);
        return 0;
    }

    auto numin = createNumin(argv[1]);
    if (!numin)
    {
        std::fprintf(stderr, "No Numin called '%s'. Try --list.\n", argv[1]);
        return 1;
    }
    if (argc < 3)
        return usage();
    if (std::strcmp(argv[2], "--info") == 0)
    {
        printInfo(*numin);
        return 0;
    }

    // ---- arguments --------------------------------------------------------
    std::string inPath = argv[2], outPath, sidechainPath;
    int sweepParam = -1, block = 512;
    double abSeconds = 0, tailSeconds = 0;

    for (int a = 3; a < argc; ++a)
    {
        std::string arg = argv[a];
        auto next = [&]() -> std::string {
            if (a + 1 >= argc)
            {
                std::fprintf(stderr, "%s needs a value\n", arg.c_str());
                std::exit(2);
            }
            return argv[++a];
        };

        if (arg == "--sweep")
        {
            std::string p = next();
            if ((sweepParam = findParam(*numin, p)) < 0)
            {
                std::fprintf(stderr, "Unknown parameter '%s'. Try: numin-render %s --info\n", p.c_str(), argv[1]);
                return 2;
            }
        }
        else if (arg == "--ab")
            abSeconds = std::atof(next().c_str());
        else if (arg == "--block")
            block = std::max(1, std::atoi(next().c_str()));
        else if (arg == "--tail")
            tailSeconds = std::atof(next().c_str());
        else if (arg == "--sidechain")
            sidechainPath = next();
        else if (auto eq = arg.find('='); eq != std::string::npos)
        {
            int p = findParam(*numin, arg.substr(0, eq));
            if (p < 0)
            {
                std::fprintf(stderr, "Unknown parameter '%s'. Try: numin-render %s --info\n",
                             arg.substr(0, eq).c_str(), argv[1]);
                return 2;
            }
            numin->setParam(p, (float)std::atof(arg.c_str() + eq + 1));
        }
        else if (outPath.empty() && arg.rfind("--", 0) != 0)
            outPath = arg;
        else
            return usage();
    }

    if (outPath.empty())
    {
        auto slash = inPath.find_last_of('/');
        auto base = inPath.substr(slash == std::string::npos ? 0 : slash + 1);
        base = base.substr(0, base.find_last_of('.'));
        outPath = "renders/" + base + "." + numin->name() + ".wav";
        if (std::system("mkdir -p renders") != 0)
            return 1;
    }

    // ---- read ---------------------------------------------------------------
    unsigned int channels = 0, rate = 0;
    drwav_uint64 inFrames = 0;
    float* data = drwav_open_file_and_read_pcm_frames_f32(inPath.c_str(), &channels, &rate, &inFrames, nullptr);
    if (!data)
    {
        std::fprintf(stderr, "Couldn't read '%s' (WAV only — convert with: afconvert -f WAVE -d LEI24 in.aif out.wav)\n",
                     inPath.c_str());
        return 1;
    }

    const size_t frames = (size_t)inFrames + (size_t)(tailSeconds * rate);
    std::vector<float> inL(frames, 0.0f), inR(frames, 0.0f), outL(frames), outR(frames);
    for (size_t i = 0; i < inFrames; ++i)
    {
        inL[i] = data[i * channels];
        inR[i] = data[i * channels + (channels > 1 ? 1 : 0)]; // mono -> both sides
    }
    drwav_free(data, nullptr);

    // ---- sidechain (optional): padded with silence or cut to the input's length ----
    std::vector<float> scL, scR;
    if (!sidechainPath.empty())
    {
        unsigned int scChannels = 0, scRate = 0;
        drwav_uint64 scFrames = 0;
        float* sc = drwav_open_file_and_read_pcm_frames_f32(sidechainPath.c_str(), &scChannels, &scRate, &scFrames, nullptr);
        if (!sc)
        {
            std::fprintf(stderr, "Couldn't read sidechain '%s'\n", sidechainPath.c_str());
            return 1;
        }
        if (scRate != rate)
        {
            std::fprintf(stderr, "The sidechain is at %u Hz and the input at %u Hz: they need to match.\n", scRate, rate);
            drwav_free(sc, nullptr);
            return 1;
        }
        if (!numin->usesSidechain())
            std::fprintf(stderr, "(%s doesn't listen to a sidechain; ignoring it)\n", numin->name());
        scL.assign(frames, 0.0f);
        scR.assign(frames, 0.0f);
        for (size_t i = 0; i < std::min<size_t>(frames, (size_t)scFrames); ++i)
        {
            scL[i] = sc[i * scChannels];
            scR[i] = sc[i * scChannels + (scChannels > 1 ? 1 : 0)];
        }
        drwav_free(sc, nullptr);
    }

    // ---- process ------------------------------------------------------------
    numin->prepare(rate);
    auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < frames; pos += block)
    {
        int n = (int)std::min<size_t>(block, frames - pos);
        if (sweepParam >= 0)
            numin->setParam(sweepParam, (float)pos / (float)frames);
        if (!scL.empty())
            numin->setSidechain(&scL[pos], &scR[pos]);
        numin->process(&inL[pos], &inR[pos], &outL[pos], &outR[pos], n);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // ---- optional A/B interleave (wet first), 10 ms crossfades ------------------
    if (abSeconds > 0)
    {
        const double period = abSeconds * rate, fade = 0.010 * rate;
        for (size_t i = 0; i < frames; ++i)
        {
            double phase = std::fmod((double)i, 2 * period);
            double wet = phase < period ? 1.0 : 0.0;
            double d = std::min(std::fabs(phase - period), std::min(phase, 2 * period - phase));
            if (d < fade) // ramp around each switch point
                wet = 0.5 + 0.5 * (phase < period ? 1 : -1) * (d / fade);
            outL[i] = (float)(wet * outL[i] + (1 - wet) * inL[i]);
            outR[i] = (float)(wet * outR[i] + (1 - wet) * inR[i]);
        }
    }

    // ---- stats --------------------------------------------------------------
    Stats sIn, sOut;
    std::vector<float> interleaved(frames * 2);
    for (size_t i = 0; i < frames; ++i)
    {
        sIn.add(inL[i]), sIn.add(inR[i]);
        sOut.add(outL[i]), sOut.add(outR[i]);
        interleaved[i * 2] = outL[i];
        interleaved[i * 2 + 1] = outR[i];
    }

    // ---- write (32-bit float, so nothing clips or gets hidden) --------------
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 2;
    fmt.sampleRate = rate;
    fmt.bitsPerSample = 32;
    drwav wav;
    if (!drwav_init_file_write(&wav, outPath.c_str(), &fmt, nullptr))
    {
        std::fprintf(stderr, "Couldn't write '%s'\n", outPath.c_str());
        return 1;
    }
    drwav_write_pcm_frames(&wav, frames, interleaved.data());
    drwav_uninit(&wav);

    // ---- report -------------------------------------------------------------
    std::printf("%s", numin->name());
    for (int i = 0; i < numin->numParams(); ++i)
        std::printf("  %s=%.2f", numin->paramInfo(i).name, numin->getParam(i));
    if (sweepParam >= 0)
        std::printf("  (sweeping %s)", numin->paramInfo(sweepParam).name);
    std::printf("\n  in   peak %6.1f dBFS   rms %6.1f dBFS\n", toDb(sIn.peak), sIn.rmsDb());
    std::printf("  out  peak %6.1f dBFS   rms %6.1f dBFS%s\n", toDb(sOut.peak), sOut.rmsDb(),
                sOut.peak > 1.0 ? "   <- over 0 dBFS" : "");
    if (sOut.nonFinite)
        std::printf("  !! %ld NaN/inf samples in output\n", sOut.nonFinite);
    std::printf("  %.0fx realtime -> %s\n", (double)frames / rate / std::max(secs, 1e-9), outPath.c_str());
    return sOut.nonFinite ? 1 : 0;
}
