#include "TestFramework.h"
#include "../Source/DSP/Eq.h"
#include "../Source/DSP/Dynamics.h"
#include "../Source/DSP/Saturation.h"
#include "../Source/DSP/Limiter.h"
#include "../Source/DSP/Time.h"
#include "../Source/DSP/Spectral.h"
#include "../Source/Metering/Meters.h"
using namespace slp; using namespace tu;

TEST(eq_bell_and_response) {
    ParametricEQ eq; eq.prepare(FS); EqParams p; p.band[0] = {true, FilterType::Bell, 1000.f, 6.f, 1.f, 12}; eq.setParams(p);
    auto x = sine(1000, -20, 48000); runBlocks(x, nullptr, 256, [&](float** b, int c, int n) { eq.process(b, c, n); });
    CHECK_NEAR(peakDb(x.data() + 24000, 24000) + 20, 6.0, 0.15);
    auto y = sine(100, -20, 48000); runBlocks(y, nullptr, 256, [&](float** b, int c, int n) { eq.process(b, c, n); });
    CHECK_NEAR(peakDb(y.data() + 24000, 24000) + 20, 0.0, 0.3);
    CHECK_NEAR(eq.responseDb(1000), 6.0, 0.01);
}
TEST(eq_highpass_slope_and_24_bands) {
    ParametricEQ eq; eq.prepare(FS); EqParams p; p.band[0] = {true, FilterType::HighPass, 100.f, 0.f, 0.707f, 24}; eq.setParams(p);
    CHECK_NEAR(eq.responseDb(100), -3.01, 0.1); CHECK(eq.responseDb(25) < -40.0); CHECK_NEAR(eq.responseDb(2000), 0.0, 0.1);
    for (int i = 0; i < 24; ++i) p.band[(size_t)i] = {true, (FilterType)(i % 9), 100.f * (i + 1), (float)((i % 5) - 2), 1.f, 12};
    eq.setParams(p); auto n = white(-20, 96000); runBlocks(n, nullptr, 512, [&](float** b, int c, int k) { eq.process(b, c, k); });
    CHECK(allFinite(n.data(), (int)n.size()));
}
TEST(eq_auto_gain_perceptual) {
    ParametricEQ eq; eq.prepare(FS); EqParams p; p.band[0] = {true, FilterType::Bell, 3000.f, 6.f, 1.f, 12}; p.autoGain = true; eq.setParams(p);
    CHECK(eq.autoGainDb() < -0.5f); CHECK(eq.autoGainDb() > -6.f);
    p.band[0].gainDb = 0; eq.setParams(p); CHECK_NEAR(eq.autoGainDb(), 0.0, 0.01);
}
TEST(dynamic_eq_reduces_above_threshold_only) {
    DynamicEQ d; d.prepare(FS); DynEqParams p; p.band[0] = {true, FilterType::Bell, 1000.f, 2.f, 0.f, -30.f, 4.f, 5.f, 50.f, 12.f, false}; d.setParams(p);
    auto loud = sine(1000, -10, 48000); runBlocks(loud, nullptr, 256, [&](float** b, int c, int n) { d.process(b, c, n); });
    CHECK(peakDb(loud.data() + 24000, 24000) < -10 - 4.0); CHECK(d.dynGainDb(0) < -4.f);
    d.reset(); auto quiet = sine(1000, -50, 48000); runBlocks(quiet, nullptr, 256, [&](float** b, int c, int n) { d.process(b, c, n); });
    CHECK_NEAR(peakDb(quiet.data() + 24000, 24000), -50.0, 0.3);
}
TEST(compressor_static_curve_vca) {
    Compressor c; c.prepare(FS); CompParams p; p.model = CompModel::VCA; p.thresholdDb = -20; p.ratio = 4; p.attackMs = 0.5f; p.releaseMs = 100; p.kneeDb = 0; c.setParams(p);
    auto x = sine(1000, -6, 96000); runBlocks(x, nullptr, 256, [&](float** b, int ch, int n) { c.process(b, ch, n); });
    CHECK_NEAR(peakDb(x.data() + 72000, 24000), -6.0 - 10.5, 0.7);
}
TEST(compressor_all_models_stable) {
    for (int m = 0; m <= 6; ++m) {
        Compressor c; c.prepare(FS); CompParams p; p.model = (CompModel)m; p.thresholdDb = -24; p.ratio = 6; p.autoGain = true; p.lookaheadMs = 2; p.sidechainHpHz = 100; p.mix = 0.8f; c.setParams(p);
        auto x = pink(-14, 96000); runBlocks(x, nullptr, 480, [&](float** b, int ch, int n) { c.process(b, ch, n); });
        CHECK(allFinite(x.data(), (int)x.size())); CHECK(c.grMeter < -0.5f); CHECK(peakDb(x.data(), (int)x.size()) < 12.0);
    }
}
TEST(compressor_autogain_matches_loudness) {
    Compressor c; c.prepare(FS); CompParams p; p.model = CompModel::Clean; p.thresholdDb = -30; p.ratio = 4; p.autoGain = true; c.setParams(p);
    auto x = pink(-18, 48000 * 12); auto ref = x; runBlocks(x, nullptr, 256, [&](float** b, int ch, int n) { c.process(b, ch, n); });
    CHECK_NEAR(rmsDb(x.data() + 48000 * 8, 48000 * 4) - rmsDb(ref.data() + 48000 * 8, 48000 * 4), 0.0, 2.0);
}
TEST(gate_closes_noise_passes_signal) {
    Gate g; g.prepare(FS); GateParams p; p.thresholdDb = -40; p.rangeDb = -80; p.ratio = 20; g.setParams(p);
    auto n = white(-60, 48000); runBlocks(n, nullptr, 256, [&](float** b, int c, int k) { g.process(b, c, k); });
    CHECK(rmsDb(n.data() + 24000, 24000) < -100 + 20);
    g.reset(); auto s = sine(1000, -20, 48000); runBlocks(s, nullptr, 256, [&](float** b, int c, int k) { g.process(b, c, k); });
    CHECK_NEAR(peakDb(s.data() + 24000, 24000), -20.0, 0.3);
}
TEST(deesser_targets_sibilance) {
    DeEsser d; d.prepare(FS); DeEssParams p; p.freqHz = 7000; p.thresholdDb = -30; p.rangeDb = 12; d.setParams(p);
    auto hi = sine(7000, -15, 48000); runBlocks(hi, nullptr, 256, [&](float** b, int c, int k) { d.process(b, c, k); });
    CHECK(peakDb(hi.data() + 24000, 24000) < -15 - 6); d.reset();
    auto lo = sine(500, -15, 48000); runBlocks(lo, nullptr, 256, [&](float** b, int c, int k) { d.process(b, c, k); });
    CHECK_NEAR(peakDb(lo.data() + 24000, 24000), -15.0, 1.0);
}
TEST(transient_designer_direction) {
    auto burst = [&](float atk, float sus) {
        TransientDesigner t; t.prepare(FS); TransientParams p; p.attack = atk; p.sustain = sus; t.setParams(p);
        vector<float> x(48000, 0.f); Rng r; r.s = 99; for (int k = 0; k < 4; ++k) for (int i = 0; i < 12000; ++i) x[(size_t)(k * 12000 + i)] = r.next() * 0.5f * std::exp(-i / 2500.f);
        runBlocks(x, nullptr, 128, [&](float** b, int c, int n) { t.process(b, c, n); });
        double pk = 0, tail = 0; for (int k = 1; k < 4; ++k) { for (int i = 0; i < 480; ++i) pk += x[(size_t)(k * 12000 + i)] * x[(size_t)(k * 12000 + i)]; for (int i = 6000; i < 9000; ++i) tail += x[(size_t)(k * 12000 + i)] * x[(size_t)(k * 12000 + i)]; }
        return std::make_pair(pk, tail); };
    auto base = burst(0, 0), up = burst(1, 0), dn = burst(-1, 0), sus = burst(0, 1), cut = burst(0, -1);
    CHECK(up.first > base.first * 1.5); CHECK(dn.first < base.first * 0.7); CHECK(sus.second > base.second * 1.5); CHECK(cut.second < base.second * 0.6);
}
TEST(multiband_is_transparent_when_idle) {
    MultibandCompressor mb; mb.prepare(FS); MbParams p; for (auto& b : p.band) b.thresholdDb = 0; mb.setParams(p);
    auto x = pink(-20, 96000); auto ref = x; runBlocks(x, nullptr, 480, [&](float** b, int c, int n) { mb.process(b, c, n); });
    CHECK_NEAR(rmsDb(x.data() + 48000, 48000) - rmsDb(ref.data() + 48000, 48000), 0.0, 0.5); CHECK(allFinite(x.data(), (int)x.size()));
    p.band[1].thresholdDb = -40; p.band[1].ratio = 8; mb.setParams(p); auto y = pink(-12, 96000); runBlocks(y, nullptr, 480, [&](float** b, int c, int n) { mb.process(b, c, n); });
    CHECK(mb.grDb(1) < -3.f); CHECK(allFinite(y.data(), (int)y.size()));
}
TEST(saturation_models_harmonics_no_nan) {
    for (int m = 0; m < 8; ++m) for (int os : {1, 2, 4}) {
        Saturator s; s.prepare(FS, 1024); SatParams p; p.model = (SatModel)m; p.driveDb = 15; p.oversample = os; p.mix = 1; s.setParams(p);
        auto x = sine(1000, -12, 48000); runBlocks(x, nullptr, 512, [&](float** b, int c, int n) { s.process(b, c, n); });
        const float* t = x.data() + 24000; CHECK(allFinite(x.data(), 48000)); CHECK(std::fabs(dcOffset(t, 20000)) < 0.01);
        double fund = tone(t, 20000, 1000), h3 = tone(t, 20000, 3000), h2 = tone(t, 20000, 2000); CHECK(fund > 0.05); CHECK((h3 + h2) / fund > 0.003);
    }
    Saturator s; s.prepare(FS, 1024); SatParams p; p.driveDb = 0; p.model = SatModel::Tape; s.setParams(p); vector<float> z(4096, 0.f); runBlocks(z, nullptr, 512, [&](float** b, int c, int n) { s.process(b, c, n); }); CHECK(peakDb(z.data(), 4096) < -100);
}
TEST(saturation_clean_small_signal_unity) {
    Saturator s; s.prepare(FS, 1024); SatParams p; p.model = SatModel::Console; p.driveDb = 0; p.oversample = 2; s.setParams(p);
    auto x = sine(1000, -40, 48000); runBlocks(x, nullptr, 512, [&](float** b, int c, int n) { s.process(b, c, n); }); CHECK_NEAR(peakDb(x.data() + 24000, 24000), -40.0, 0.4);
}
TEST(tape_engine_stable_and_bounded) {
    TapeEngine t; t.prepare(FS, 1024); TapeParams p; p.saturation = 0.9f; p.wow = 0.5f; p.flutter = 0.5f; p.noiseDb = -70; t.setParams(p);
    auto x = pink(-10, 96000); runBlocks(x, nullptr, 512, [&](float** b, int c, int n) { t.process(b, c, n); });
    CHECK(allFinite(x.data(), (int)x.size())); CHECK(peakDb(x.data(), (int)x.size()) < 6.0); CHECK(rmsDb(x.data() + 48000, 48000) > -30);
}
TEST(soft_clipper_respects_ceiling) {
    SoftClipper c; c.prepare(FS, 1024); ClipParams p; p.thresholdDb = -6; p.ceilingDb = -1; p.softness = 1.f; p.oversample = 1; c.setParams(p);
    auto x = sine(1000, 6, 48000); runBlocks(x, nullptr, 512, [&](float** b, int ch, int n) { c.process(b, ch, n); }); CHECK(peakDb(x.data(), 48000) <= -1.0 + 0.01);
    c.setParams(p); auto q = sine(1000, -20, 48000); runBlocks(q, nullptr, 512, [&](float** b, int ch, int n) { c.process(b, ch, n); }); CHECK_NEAR(peakDb(q.data(), 48000), -20.0, 0.01);
}
TEST(limiter_true_peak_ceiling) {
    TruePeakLimiter L; L.prepare(FS); LimiterParams p; p.thresholdDb = -12; p.ceilingDb = -1; p.truePeak = true; L.setParams(p);
    auto x = pink(-10, 48000 * 4); vector<float> l = x, r = x; std::reverse(r.begin(), r.end());
    runBlocks(l, &r, 512, [&](float** b, int c, int n) { L.process(b, c, n); });
    CHECK(allFinite(l.data(), (int)l.size())); CHECK(peakDb(l.data(), (int)l.size()) <= -1.0 + 0.01);
    CHECK(truePeakRef(l.data() + 4800, (int)l.size() - 9600) <= -1.0 + 0.25);
    CHECK(L.grMeter < -3.f);
    // intersample-peak test: sine at fs/4 with 45deg phase has sample peak 3 dB under true peak
    L.reset(); auto s = sine(12000, 3, 48000, FS, kPi / 4); vector<float> s2 = s; runBlocks(s, &s2, 512, [&](float** b, int c, int n) { L.process(b, c, n); });
    CHECK(truePeakRef(s.data() + 4800, 38000) <= -1.0 + 0.25);
}
TEST(limiter_transparent_below_ceiling) {
    TruePeakLimiter L; L.prepare(FS); LimiterParams p; p.thresholdDb = 0; p.ceilingDb = -1; L.setParams(p);
    auto x = sine(1000, -20, 48000); vector<float> y = x; runBlocks(x, &y, 512, [&](float** b, int c, int n) { L.process(b, c, n); });
    CHECK_NEAR(peakDb(x.data() + 24000, 24000), -20.0, 0.05);
}
TEST(delay_time_and_feedback) {
    DelayFx d; d.prepare(FS); DelayParams p; p.type = DelayType::Digital; p.timeMs = 100; p.feedback = 0.5f; p.mix = 1.f; p.filterHz = 20000; d.setParams(p); d.reset();
    for (int k = 0; k < 100; ++k) { vector<float> w(512, 0.f), w2 = w; float* pp[2] = {w.data(), w2.data()}; d.process(pp, 2, 512); } // settle slew
    vector<float> l(24000, 0.f), r(24000, 0.f); l[0] = 1.f; r[0] = 1.f; runBlocks(l, &r, 512, [&](float** b, int c, int n) { d.process(b, c, n); });
    int first = 0; for (int i = 1; i < 24000; ++i) if (std::fabs(l[(size_t)i]) > 0.1f) { first = i; break; }
    CHECK(std::abs(first - 4800) <= 3); float e2 = 0; for (int k = -3; k <= 3; ++k) e2 = std::max(e2, std::fabs(l[(size_t)(first + 4800 + k)])); CHECK(e2 > 0.3f);
}
TEST(reverb_decay_and_stability) {
    Reverb r; r.prepare(FS); auto p = ReverbParams::preset(ReverbType::Hall); p.mix = 1.f; p.decaySec = 2.0f; r.setParams(p);
    vector<float> l(48000 * 6, 0.f), rr(48000 * 6, 0.f); l[0] = rr[0] = 1.f; runBlocks(l, &rr, 512, [&](float** b, int c, int n) { r.process(b, c, n); });
    CHECK(allFinite(l.data(), (int)l.size())); double e1 = rmsDb(l.data() + 4800, 24000), e2 = rmsDb(l.data() + 48000 * 3, 24000), e3 = rmsDb(l.data() + 48000 * 5, 24000);
    CHECK(e1 > e2 + 10); CHECK(e2 > e3 + 3); CHECK(e1 > -60);
    for (int t = 0; t <= 6; ++t) { Reverb q; q.prepare(FS); auto pp = ReverbParams::preset((ReverbType)t); pp.mix = 0.5f; q.setParams(pp); auto n = white(-12, 96000); vector<float> n2 = n; runBlocks(n, &n2, 480, [&](float** b, int c, int k) { q.process(b, c, k); }); CHECK(allFinite(n.data(), (int)n.size())); CHECK(peakDb(n.data(), (int)n.size()) < 12); }
}
TEST(reverb_ducking_reduces_wet_under_signal) {
    auto run = [&](float duck) { Reverb r; r.prepare(FS); auto p = ReverbParams::preset(ReverbType::Plate); p.mix = 1.f; p.duck = duck; r.setParams(p);
        auto x = pink(-18, 96000); vector<float> y = x; runBlocks(x, &y, 480, [&](float** b, int c, int n) { r.process(b, c, n); }); return rmsDb(x.data() + 48000, 48000); };
    CHECK(run(1.f) < run(0.f) - 1.0);
}
TEST(loudness_meter_bs1770_reference) {
    LoudnessMeter m; m.prepare(FS); auto s = sine(1000, -20, 48000 * 10); const float* ch[2] = {s.data(), s.data()}; m.process(ch, 48000 * 10);
    CHECK_NEAR(m.integrated(), -20.0, 0.25); CHECK_NEAR(m.momentary(), -20.0, 0.25); CHECK_NEAR(m.shortTerm(), -20.0, 0.25); CHECK_NEAR(m.truePeakDb(), -20.0, 0.3); CHECK_NEAR(m.peakDb(), -20.0, 0.05);
    LoudnessMeter m2; m2.prepare(FS); auto q = sine(1000, -40, 48000 * 6); const float* c2[2] = {q.data(), q.data()}; m2.process(c2, 48000 * 6); CHECK_NEAR(m2.integrated(), -40.0, 0.25);
}
TEST(correlation_and_mono_compat) {
    CorrelationMeter c; c.prepare(FS); auto a = sine(440, -12, 48000); auto b = a; c.process(a.data(), b.data(), 48000); CHECK_NEAR(c.correlation(), 1.0, 0.01); CHECK(c.monoCompatible()); CHECK(c.width() < 0.01f);
    c.reset(); for (auto& v : b) v = -v; c.process(a.data(), b.data(), 48000); CHECK_NEAR(c.correlation(), -1.0, 0.01); CHECK(!c.monoCompatible());
}
TEST(spectrum_analyzer_finds_tone) {
    SpectrumAnalyzer s; s.prepare(FS); auto x = sine(1000, -6, 48000); s.push(x.data(), 48000);
    vector<float> sp(SpectrumAnalyzer::kBins); s.getSpectrum(sp.data()); int pk = (int)(std::max_element(sp.begin(), sp.end()) - sp.begin());
    CHECK(std::fabs(s.binHz(pk) - 1000.0) < 20.0); CHECK_NEAR(sp[(size_t)pk], -6.0, 1.5);
}
TEST(noise_reducer_profile) {
    NoiseReducer nr; NoiseParams p; p.enabled = true; p.reductionDb = 20; nr.setParams(p); nr.prepare(FS);
    auto noise = white(-45, 48000 * 2, 3); nr.startLearning(); { auto t = noise; nr.process(t.data(), (int)t.size()); } nr.stopLearning();
    auto n2 = white(-45, 48000 * 2, 5); auto sg = sine(1000, -20, 48000 * 2); vector<float> mix(n2.size()); for (size_t i = 0; i < mix.size(); ++i) mix[i] = n2[i] + sg[i];
    auto onlyNoise = n2; nr.reset(); nr.process(onlyNoise.data(), (int)onlyNoise.size()); nr.reset(); nr.process(mix.data(), (int)mix.size());
    double nIn = rmsDb(n2.data() + 48000, 48000), nOut = rmsDb(onlyNoise.data() + 48000, 48000); CHECK(nOut < nIn - 10.0);
    CHECK_NEAR(tone(mix.data() + 48000, 24000, 1000), std::pow(10.0, -20.0 / 20.0), 0.03); CHECK(allFinite(mix.data(), (int)mix.size()));
}
TEST(hum_remover_removes_60hz) {
    HumRemover h; h.prepare(FS); HumParams p; p.enabled = true; p.mainsHz = 60; p.harmonics = 4; p.intensity = 1.f; h.setParams(p);
    auto hum = sine(60, -20, 96000), h2 = sine(120, -26, 96000), v = sine(1000, -20, 96000); for (size_t i = 0; i < hum.size(); ++i) hum[i] += h2[i] + v[i];
    runBlocks(hum, nullptr, 512, [&](float** b, int c, int n) { h.process(b, c, n); });
    CHECK(tone(hum.data() + 48000, 48000, 60) < 0.01 * 0.1); CHECK(tone(hum.data() + 48000, 48000, 120) < 0.0501 * 0.1); CHECK_NEAR(tone(hum.data() + 48000, 48000, 1000), 0.1, 0.01);
}
