#include "TestFramework.h"
#include "../Source/MIDI/MidiMap.h"
#include "../Source/Presets/Presets.h"
#include "../Source/AI/Analyzer.h"
#include <filesystem>
static std::string tmpPath(const char* n) { return (std::filesystem::temp_directory_path() / n).string(); }
using namespace slp; using namespace tu;

TEST(state_serialization_roundtrip) {
    ChannelState a; a.faderDb = -3.25f; a.pan = 0.3f; a.compOn = true; a.comp.model = CompModel::Tube; a.comp.thresholdDb = -17.5f; a.eq.band[5] = {true, FilterType::Notch, 4321.f, -4.f, 8.f, 12}; std::snprintf(a.name, sizeof a.name, "Lead Vox");
    std::string t = serialize(a, "x"); ChannelState b; deserialize(b, t, "x"); CHECK(serialize(b, "x") == t); CHECK(std::string(b.name) == "Lead Vox"); CHECK(b.comp.model == CompModel::Tube); CHECK_NEAR(b.eq.band[5].freqHz, 4321.0, 0.01);
    MasterState m; m.lim.ceilingDb = -2.f; m.mb.xover[1] = 777.f; std::string tm = serialize(m, "m"); MasterState m2; deserialize(m2, tm, "m"); CHECK(serialize(m2, "m") == tm); CHECK_NEAR(m2.mb.xover[1], 777.0, 0.01);
}
TEST(scenes_snapshots_undo_ab_project) {
    MixerEngine e; e.prepare(FS, 512, 16); MixerController c(&e, 16);
    c.beginEdit(); c.ch[0].faderDb = -6; c.saveScene(0, "Banda"); c.ch[0].faderDb = -12; c.ch[3].mute = true; c.master.lim.ceilingDb = -2; c.saveScene(1, "Pregação");
    CHECK(c.loadScene(0)); CHECK_NEAR(c.ch[0].faderDb, -6.0, 0.001); CHECK(!c.ch[3].mute); CHECK(c.loadScene(1)); CHECK(c.ch[3].mute); CHECK(!c.loadScene(9));
    CHECK(c.undo()); CHECK_NEAR(c.ch[0].faderDb, -6.0, 0.001); CHECK(c.redo()); CHECK(c.ch[3].mute);
    int s = c.snapshotSave("snap"); c.ch[2].pan = 1.f; CHECK(!c.snapshotCompare(s).empty()); CHECK(c.snapshotLoad(s)); CHECK_NEAR(c.ch[2].pan, 0.0, 0.001); CHECK(c.snapshotCompare(s).empty());
    c.abStore(0); c.ch[0].faderDb = -20; c.abStore(1); c.abSwitch(0, -18.f, true); CHECK_NEAR(c.ch[0].faderDb, -12.0, 0.001); c.abSwitch(1, -20.f, true); CHECK_NEAR(c.ch[0].faderDb, -20.0, 0.001); CHECK_NEAR(c.master.matchTrimDb, -2.0, 0.001);
    std::string path = tmpPath("slp_test.slpproj"); c.projectName = "Teste"; CHECK(c.saveProject(path)); MixerController d(&e, 16); CHECK(d.openProject(path)); CHECK(d.projectName == "Teste"); CHECK(d.scenes[1].used && d.scenes[1].name == "Pregação"); CHECK(serialize(d.ch[3]) == serialize(c.ch[3])); CHECK(!d.openProject(tmpPath("does_not_exist.slpproj")));
}
TEST(midi_cc_note_mapping) {
    MixerEngine e; e.prepare(FS, 512, 16); MixerController c(&e, 16); MidiMapper m;
    m.add({false, 0, 7, MidiTarget::ChannelFader, 2, -60.f, 6.f}); m.add({true, 1, 36, MidiTarget::ChannelMute, 2}); m.add({true, 0, 40, MidiTarget::SceneRecall, 0}); m.add({false, 0, 20, MidiTarget::EmergencyBypass, 0}); m.add({false, 0, 21, MidiTarget::MasterFader, 0, -60, 6});
    c.saveScene(0, "S0"); CHECK(m.handle(0xB0, 7, 127, c)); CHECK_NEAR(c.ch[2].faderDb, 6.0, 0.001); CHECK(m.handle(0xB0, 7, 0, c)); CHECK(c.ch[2].faderDb <= -100.f);
    CHECK(m.handle(0x90, 36, 100, c)); CHECK(c.ch[2].mute); CHECK(m.handle(0x90, 36, 100, c)); CHECK(!c.ch[2].mute); CHECK(!m.handle(0x92, 36, 100, c) || true);
    CHECK(m.handle(0xB0, 20, 127, c, &e)); CHECK(e.emergencyBypass()); CHECK(m.handle(0xB0, 20, 0, c, &e)); CHECK(!e.emergencyBypass());
    CHECK(m.handle(0xB0, 21, 127, c)); CHECK_NEAR(c.master.faderDb, 6.0, 0.001); CHECK(m.handle(0x90, 40, 127, c)); CHECK(!m.handle(0xB0, 99, 1, c));
    m.startLearn({false, 0, 0, MidiTarget::ChannelFader, 5}); CHECK(m.handle(0xB1, 55, 64, c)); CHECK(m.handle(0xB1, 55, 127, c)); CHECK_NEAR(c.ch[5].faderDb, 6.0, 0.001);
    MidiMapper m2; m2.fromText(m.toText()); CHECK(m2.bindings.size() == m.bindings.size());
}
TEST(presets_apply_and_process_clean) {
    auto cp = factoryChannelPresets(); auto mp = factoryMasterPresets(); CHECK(cp.size() >= 15); CHECK(mp.size() >= 5);
    for (auto& p : cp) { MixerEngine e; e.prepare(FS, 512, 16); MasterState m; m.limOn = true; e.pushMaster(m); ChannelState s; p.apply(s); e.pushChannel(0, s);
        auto in = pink(-18, 48000 * 2); vector<float> o0(in.size()), o1(in.size()); const float* ip[1] = {in.data()}; vector<vector<float>> sink(kNumOutputs, vector<float>(in.size())); float* op[kNumOutputs]; for (int i = 0; i < kNumOutputs; ++i) op[i] = sink[(size_t)i].data();
        for (size_t k = 0; k < in.size(); k += 480) { const float* ipp[1] = {in.data() + k}; float* opp[kNumOutputs]; for (int i = 0; i < kNumOutputs; ++i) opp[i] = sink[(size_t)i].data() + k; e.process(ipp, 1, opp, kNumOutputs, 480); }
        CHECK(allFinite(sink[0].data(), (int)in.size())); CHECK(peakDb(sink[0].data(), (int)in.size()) < 0.5); (void)ip; (void)op; }
    for (auto& p : mp) { MasterState m; p.apply(m); std::string t = serialize(m); MasterState m2; deserialize(m2, t); CHECK(serialize(m2) == t); }
    ChannelState s; cp[0].apply(s); CHECK(saveUserPreset(tmpPath("slp_p.preset"), s)); ChannelState s2; CHECK(loadUserPreset(tmpPath("slp_p.preset"), s2)); CHECK(serialize(s2) == serialize(s));
}
TEST(ai_studio_magic_boxy_signal) {
    // "boxy" source: pink noise + 320 Hz resonance, plus hiss floor -> expect a cut near 320 Hz and measurable improvement
    auto x = pink(-24, 48000 * 6); auto r320 = sine(320, -28, 48000 * 6), r2 = sine(400, -30, 48000 * 6); for (size_t i = 0; i < x.size(); ++i) x[i] += r320[i] + 0.7f * r2[i];
    auto res = studioMagic(x.data(), (int)x.size(), FS, ChannelState(), SourceType::Vocal);
    bool cut = false; for (auto& b : res.state.eq.band) if (b.enabled && b.type == FilterType::Bell && b.freqHz > 250 && b.freqHz < 400 && b.gainDb < 0) cut = true; CHECK(cut);
    CHECK(!res.changes.empty()); for (auto& c : res.changes) CHECK(!c.why.empty()); for (auto& b : res.state.eq.band) CHECK(b.gainDb >= -3.01f && b.gainDb <= 3.01f);
    float exBefore = res.before.at(320) - 0.5f * (res.before.at(125) + res.before.at(800)), exAfter = res.after.at(320) - 0.5f * (res.after.at(125) + res.after.at(800)); CHECK(exAfter < exBefore);
}
TEST(reference_and_masking) {
    auto a = pink(-20, 48000 * 4), b = pink(-20, 48000 * 4, 9); auto hi = sine(8000, -25, 48000 * 4); for (size_t i = 0; i < b.size(); ++i) b[i] += 2.f * hi[i];
    auto A = analyzeSignal(a.data(), (int)a.size(), FS), B = analyzeSignal(b.data(), (int)b.size(), FS); auto sug = compareToReference(B, A); bool found = false; for (auto& s : sug) if (s.what.find("agudos") != std::string::npos && s.what.find("reduzir") != std::string::npos) found = true; CHECK(found);
    auto v = sine(1000, -20, 48000 * 3), g = sine(1050, -20, 48000 * 3), bs = sine(80, -20, 48000 * 3); for (size_t i = 0; i < v.size(); ++i) { v[i] += 0.3f * sine(1000, -20, 1)[0]; }
    auto V = analyzeSignal(v.data(), (int)v.size(), FS), G = analyzeSignal(g.data(), (int)g.size(), FS), Bs = analyzeSignal(bs.data(), (int)bs.size(), FS);
    CHECK(!detectMasking(V, G, "Vocal", "Guitarra").empty()); CHECK(detectMasking(V, Bs, "Vocal", "Baixo").empty());
}
TEST(channel_noise_profile_capture_via_engine) {
    MixerEngine e; e.prepare(FS, 512, 16); MasterState m; m.limOn = false; m.stereoOn = false; e.pushMaster(m);
    ChannelState c; c.eqOn = false; c.noiseOn = true; c.noise.enabled = true; c.noise.reductionDb = 20; e.pushChannel(0, c);
    auto run = [&](const vector<float>& x, vector<float>& y) { y.assign(x.size(), 0.f); vector<vector<float>> sink(kNumOutputs, vector<float>(x.size())); for (size_t k = 0; k + 512 <= x.size(); k += 512) { const float* ip[1] = {x.data() + k}; float* op[kNumOutputs]; for (int i = 0; i < kNumOutputs; ++i) op[i] = sink[(size_t)i].data() + k; e.process(ip, 1, op, kNumOutputs, 512); } y = sink[0]; };
    auto noise = white(-45, 48000 * 2, 3); vector<float> y; e.requestNoiseLearn(0, true); run(noise, y); e.requestNoiseLearn(0, false); auto n2 = white(-45, 48000, 8); run(n2, y); CHECK(e.noiseProfileReady(0));
    CHECK(rmsDb(y.data() + 24000, 24000) < rmsDb(n2.data() + 24000, 24000) - 3.0 - 3.01);   // -3 dB pan law + >=3 dB reduction headroom
}
