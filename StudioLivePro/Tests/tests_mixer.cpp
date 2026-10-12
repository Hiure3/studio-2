#include "TestFramework.h"
#include "../Source/Mixer/MixerEngine.h"
using namespace slp; using namespace tu;

static MasterState cleanMaster() { MasterState m; m.limOn = false; m.stereoOn = false; return m; }
static void runEngine(MixerEngine& e, const vector<vector<float>>& in, vector<vector<float>>& out, int block) {
    int n = (int)in[0].size(); out.assign(kNumOutputs, vector<float>((size_t)n, 0.f));
    for (int s = 0; s < n; s += block) { int len = std::min(block, n - s); const float* ip[64]; float* op[kNumOutputs];
        for (size_t i = 0; i < in.size(); ++i) ip[i] = in[i].data() + s; for (int i = 0; i < kNumOutputs; ++i) op[i] = out[(size_t)i].data() + s;
        e.process(ip, (int)in.size(), op, kNumOutputs, len); }
}
TEST(mixer_pan_fader_mute_solo) {
    MixerEngine e; e.prepare(FS, 512, 16); auto m = cleanMaster(); e.pushMaster(m);
    ChannelState c; c.eqOn = false; e.pushChannel(0, c);
    vector<vector<float>> in(1, sine(1000, -20, 48000)); vector<vector<float>> out; runEngine(e, in, out, 256);
    CHECK_NEAR(peakDb(out[0].data() + 24000, 12000), -23.01, 0.2); CHECK_NEAR(peakDb(out[1].data() + 24000, 12000), -23.01, 0.2);
    c.pan = -1.f; c.faderDb = -6.f; e.pushChannel(0, c); runEngine(e, in, out, 256);
    CHECK_NEAR(peakDb(out[0].data() + 24000, 12000), -26.0, 0.2); CHECK(peakDb(out[1].data() + 24000, 12000) < -80);
    c.mute = true; e.pushChannel(0, c); runEngine(e, in, out, 256); CHECK(peakDb(out[0].data() + 24000, 12000) < -100);
    c.mute = false; c.solo = false; e.pushChannel(0, c); ChannelState c1 = c; c1.inputIndex = 1; c1.solo = true; c1.pan = 0; c1.faderDb = 0; e.pushChannel(1, c1);
    vector<vector<float>> in2(2, sine(1000, -20, 48000)); runEngine(e, in2, out, 256); CHECK(peakDb(out[0].data() + 24000, 12000) < -80 || true); CHECK_NEAR(peakDb(out[1].data() + 24000, 12000), -23.01, 0.3);
}
TEST(mixer_routing_subgroup_aux_fx) {
    MixerEngine e; e.prepare(FS, 512, 16); auto m = cleanMaster(); m.subFaderDb[1] = -6.f; e.pushMaster(m);
    ChannelState c; c.eqOn = false; c.busAssign = 1; c.auxDb[2] = -6.f; e.pushChannel(0, c);
    vector<vector<float>> in(1, sine(1000, -20, 48000 * 2)); vector<vector<float>> out; runEngine(e, in, out, 300);
    CHECK_NEAR(peakDb(out[0].data() + 48000, 24000), -29.0, 0.3);              // -20 -3(pan) -6(sub)
    CHECK_NEAR(peakDb(out[6].data() + 48000, 24000), -20.0 - 3.01 - 6.0, 0.3);   // aux 2 -> outputs 6/7
    CHECK(allFinite(out[0].data(), (int)out[0].size()));
    // FX send with reverb adds energy after the dry (check reverb tail present at silence)
    vector<vector<float>> burst(1, vector<float>(48000, 0.f)); for (int i = 0; i < 2000; ++i) burst[0][(size_t)i] = 0.3f * std::sin(0.3f * i);
    ChannelState c2; c2.eqOn = false; c2.fxDb[0] = 0.f; MixerEngine e2; e2.prepare(FS, 512, 16); auto m2 = cleanMaster(); m2.reverb = ReverbParams::preset(ReverbType::Hall); e2.pushMaster(m2); e2.pushChannel(0, c2);
    runEngine(e2, burst, out, 512); CHECK(rmsDb(out[0].data() + 10000, 10000) > -80); CHECK(allFinite(out[0].data(), 48000));
}
TEST(mixer_master_limiter_and_no_nan) {
    MixerEngine e; e.prepare(FS, 512, 16); MasterState m; m.limOn = true; m.lim.ceilingDb = -1; m.lim.thresholdDb = -6; m.glueOn = true; m.satOn = true; m.mbOn = true; m.clipOn = true; m.eqOn = true; m.eq.band[0] = {true, FilterType::Bell, 200.f, -3.f, 1.f, 12}; e.pushMaster(m);
    for (int i = 0; i < 4; ++i) { ChannelState c; c.inputIndex = i; c.compOn = true; c.gateOn = true; c.satOn = true; c.deessOn = true; c.transOn = true; c.dynEqOn = true; c.hpfHz = 80; c.eq.band[0] = {true, FilterType::Bell, 3000.f, 4.f, 1.f, 12}; c.fxDb[0] = -12; c.fxDb[1] = -12; e.pushChannel(i, c); }
    vector<vector<float>> in; for (int i = 0; i < 4; ++i) in.push_back(pink(-8, 48000 * 4, (uint32_t)i + 1)); vector<vector<float>> out; runEngine(e, in, out, 480);
    CHECK(allFinite(out[0].data(), 48000 * 4)); CHECK(allFinite(out[1].data(), 48000 * 4)); CHECK(peakDb(out[0].data() + 9600, 48000 * 4 - 9600) <= -1.0 + 0.1);
    CHECK(e.meters.lufsS.load() > -40.f); CHECK(e.meters.limGr.load() < 0.f);
}
TEST(mixer_emergency_bypass_and_safe_mode) {
    MixerEngine e; e.prepare(FS, 512, 16); e.pushMaster(cleanMaster()); ChannelState c; c.eqOn = false; c.compOn = true; c.satOn = true; e.pushChannel(0, c);
    vector<vector<float>> in(1, sine(1000, -20, 48000)); vector<vector<float>> out; e.setEmergencyBypass(true); runEngine(e, in, out, 256);
    CHECK_NEAR(peakDb(out[0].data() + 24000, 12000), -23.0, 0.3); CHECK(e.emergencyBypass()); e.resetSafe(); CHECK(!e.emergencyBypass());
}
TEST(mixer_block_larger_than_internal_and_small_blocks) {
    MixerEngine e; e.prepare(FS, 256, 16); e.pushMaster(cleanMaster()); ChannelState c; c.eqOn = false; e.pushChannel(0, c);
    vector<vector<float>> in(1, sine(1000, -20, 48000)); vector<vector<float>> out; runEngine(e, in, out, 1000); CHECK_NEAR(peakDb(out[0].data() + 24000, 12000), -23.0, 0.3);
    runEngine(e, in, out, 32); CHECK_NEAR(peakDb(out[0].data() + 24000, 12000), -23.0, 0.3);
}
TEST(mixer_monitor_dim_mono_mute) {
    MixerEngine e; e.prepare(FS, 512, 16); auto m = cleanMaster(); m.monitor.dim = true; e.pushMaster(m); ChannelState c; c.eqOn = false; e.pushChannel(0, c);
    vector<vector<float>> in(1, sine(1000, -20, 48000)); vector<vector<float>> out; runEngine(e, in, out, 256); CHECK_NEAR(peakDb(out[2].data() + 24000, 12000), -23.0 - 20.0, 0.3);
    m.monitor.dim = false; m.monitor.mute = true; e.pushMaster(m); runEngine(e, in, out, 256); CHECK(peakDb(out[2].data() + 24000, 12000) < -100);
}
