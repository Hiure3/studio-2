#include "TestFramework.h"
#include "../Source/Presets/Presets.h"
#include <cstdlib>
#include <fstream>
using namespace slp; using namespace tu;

static void fullChain(ChannelState& s, int i) {
    s.eqOn = true; for (int b = 0; b < 6; ++b) s.eq.band[(size_t)b] = {true, b == 0 ? FilterType::HighPass : FilterType::Bell, 80.f + 700.f * b, (b % 2 ? -2.f : 2.f), 1.f, 24};
    s.gateOn = true; s.gate.thresholdDb = -60; s.dynEqOn = true; s.dyn.band[0] = {true, FilterType::Bell, 3000, 2, 0, -30, 3, 10, 100, 6, false}; s.deessOn = true; s.compOn = true; s.comp.model = (CompModel)(i % 7); s.comp.thresholdDb = -24;
    s.satOn = true; s.sat.model = (SatModel)(i % 8); s.sat.oversample = 2; s.transOn = true; s.trans.attack = 0.2f; s.fxDb[i % 2] = -15; s.auxDb[i % 4] = -10; s.busAssign = i % 5 - 1; s.pan = ((i % 9) - 4) / 4.f;
}
static MasterState fullMaster() { MasterState m; m.eqOn = m.dynEqOn = m.glueOn = m.mbOn = m.satOn = m.clipOn = m.limOn = true; m.eq.band[0] = {true, FilterType::HighPass, 30, 0, 0.707f, 12}; m.dyn.band[0] = {true, FilterType::Bell, 300, 1, 0, -30, 2, 10, 100, 4, false}; m.sat.oversample = 2; return m; }

TEST(audio_signals_through_full_engine) {
    const char* names[] = {"silence", "sine", "pink", "white", "impulse", "transients", "full-scale square"};
    for (int k = 0; k < 7; ++k) {
        MixerEngine e; e.prepare(FS, 512, 16); e.pushMaster(fullMaster()); for (int i = 0; i < 4; ++i) { ChannelState c; fullChain(c, i); c.inputIndex = i; e.pushChannel(i, c); }
        int n = 48000 * 3; vector<float> x((size_t)n, 0.f);
        switch (k) { case 1: x = sine(997, -6, n); break; case 2: x = pink(-12, n); break; case 3: x = white(-6, n); break; case 4: x[1000] = 1.f; x[30000] = -1.f; break;
            case 5: { Rng r; r.s = 5; for (int b = 0; b < 12; ++b) for (int i = 0; i < 2000; ++i) x[(size_t)(b * 12000 + i)] = r.next() * std::exp(-i / 300.f); } break; case 6: for (int i = 0; i < n; ++i) x[(size_t)i] = ((i / 50) % 2) ? 1.f : -1.f; break; }
        vector<vector<float>> in(4, x), out(kNumOutputs, vector<float>((size_t)n));
        for (int s = 0; s < n; s += 512) { const float* ip[4]; float* op[kNumOutputs]; int len = std::min(512, n - s); for (int i = 0; i < 4; ++i) ip[i] = in[(size_t)i].data() + s; for (int i = 0; i < kNumOutputs; ++i) op[i] = out[(size_t)i].data() + s; e.process(ip, 4, op, kNumOutputs, len); }
        bool fin = true; double pk = -200; for (int o = 0; o < kNumOutputs; ++o) { fin &= allFinite(out[(size_t)o].data(), n); } pk = std::max(peakDb(out[0].data(), n), peakDb(out[1].data(), n));
        std::printf("    %-18s finite=%d peak=%.2f dBFS dc=%.5f rms=%.1f dB\n", names[k], (int)fin, pk, dcOffset(out[0].data(), n), rmsDb(out[0].data(), n));
        CHECK(fin); CHECK(pk <= -1.0 + 0.05); if (k == 0) CHECK(pk < -90); CHECK(std::fabs(dcOffset(out[0].data() + 24000, n - 24000)) < 0.01);
    }
}
TEST(latency_report) {
    std::ofstream rep("Build/latency_report.txt"); rep << "Studio Live Pro - latency report (algorithmic, sample rate 48 kHz)\nbuffer_samples  buffer_ms  engine_latency_samples  engine_latency_ms  total_one_way_ms (buffer_in + engine + buffer_out)\n";
    for (int buf : {32, 64, 128, 256, 512, 1024}) for (int variant = 0; variant < 2; ++variant) {
        MixerEngine e; e.prepare(FS, buf, 16); MasterState m; m.limOn = true; m.lim.lookaheadMs = variant ? 3.f : 1.f; m.stereoOn = false; if (variant) { m.clipOn = true; m.clip.oversample = 2; } e.pushMaster(m); ChannelState c; c.eqOn = false; e.pushChannel(0, c);
        int n = 4800; vector<float> x((size_t)n, 0.f); x[100] = 0.5f; vector<vector<float>> out(kNumOutputs, vector<float>((size_t)n));
        for (int s = 0; s < n; s += buf) { int len = std::min(buf, n - s); const float* ip[1] = {x.data() + s}; float* op[kNumOutputs]; for (int i = 0; i < kNumOutputs; ++i) op[i] = out[(size_t)i].data() + s; e.process(ip, 1, op, kNumOutputs, len); }
        int pos = 0; float mx = 0; for (int i = 0; i < n; ++i) if (std::fabs(out[0][(size_t)i]) > mx) { mx = std::fabs(out[0][(size_t)i]); pos = i; } int meas = pos - 100;
        CHECK(std::abs(meas - e.masterLatencySamples()) <= 2);
        double ms = meas * 1000.0 / FS, bms = buf * 1000.0 / FS; rep << buf << "  " << bms << "  " << meas << "  " << ms << "  " << (2 * bms + ms) << (variant ? "   [limiter 3ms + clipper OS2x]" : "   [limiter 1ms]") << "\n";
    }
    rep << "\nNote: device (driver/ASIO/WASAPI) latency is added by the hardware driver and can only be measured on the target Windows machine.\n";
}
TEST(stress_32_channels_long_run) {
    const char* env = std::getenv("SLP_STRESS_SECONDS"); const int secs = env ? std::atoi(env) : 60; const int blk = 256;
    MixerEngine e; e.prepare(FS, blk, 32); e.pushMaster(fullMaster()); MixerController ctl(&e, 32); ctl.master = fullMaster();
    for (int i = 0; i < 32; ++i) { fullChain(ctl.ch[(size_t)i], i); ctl.ch[(size_t)i].inputIndex = i % 8; }
    ctl.pushAll(); ctl.saveScene(0, "A"); for (auto& c : ctl.ch) c.faderDb = -9; ctl.saveScene(1, "B");
    vector<vector<float>> src; for (int i = 0; i < 8; ++i) src.push_back(i % 2 ? pink(-16, 48000 * 4, (uint32_t)i + 1) : sine(220.0 * (i + 1), -18, 48000 * 4)); vector<vector<float>> out(kNumOutputs, vector<float>(blk));
    const long rss0 = rssKb(); long rssMid = 0; long blocks = (long)secs * 48000 / blk; bool allFin = true; double t0 = nowSec(); double worst = 0; size_t pos = 0;
    for (long b = 0; b < blocks; ++b) {
        if (b % 400 == 0) ctl.loadScene((int)((b / 400) % 2)); if (b % 97 == 0) { ctl.ch[(size_t)(b % 32)].pan = (float)((b % 7) - 3) / 3.f; ctl.commitChannel((int)(b % 32)); }
        const float* ip[8]; float* op[kNumOutputs]; for (int i = 0; i < 8; ++i) ip[i] = src[(size_t)i].data() + pos; for (int i = 0; i < kNumOutputs; ++i) op[i] = out[(size_t)i].data(); pos = (pos + blk) % (48000 * 4 - blk);
        double s0 = nowSec(); e.process(ip, 8, op, kNumOutputs, blk); worst = std::max(worst, nowSec() - s0);
        if (b % 200 == 0) { for (int o = 0; o < 2; ++o) allFin &= allFinite(out[(size_t)o].data(), blk); } if (b == blocks / 2) rssMid = rssKb();
    }
    double el = nowSec() - t0, rtf = el / secs; long rss1 = rssKb();
    std::printf("    %d s audio, 32 ch full chain: wall %.1f s  RTF %.3f (cpu load %.0f%%)  worst block %.2f ms (budget %.2f ms)  RSS %ld -> %ld -> %ld kB  overloads %ld  safeLevel %d\n", secs, el, rtf, rtf * 100, worst * 1000, blk * 1000.0 / FS, rss0, rssMid, rss1, e.meters.overloads.load(), e.meters.safeLevel.load());
    { std::ofstream rep("Build/stress_report.txt"); rep << "Stress test: 32 channels, full per-channel chain (HPF/EQ/gate/dynEQ/de-esser/comp/sat OS2x/transient), FX sends, master chain (EQ/dynEQ/glue/multiband/sat/stereo/clipper/limiter), scene recalls every 400 blocks, parameter pushes\n"
        << secs << " s of audio at 48 kHz, block " << blk << "\nwall time " << el << " s, real-time factor " << rtf << " (<1 = faster than real time on this 1-core VM)\nworst block " << worst * 1000 << " ms vs budget " << blk * 1000.0 / FS << " ms\nRSS start/mid/end kB: " << rss0 << " / " << rssMid << " / " << rss1 << "\nNaN/Inf: " << (allFin ? "none" : "FOUND") << "\n"; }
    CHECK(allFin); CHECK(rss1 - rssMid < 4096); CHECK(rtf < 1.0);
}
