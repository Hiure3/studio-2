// Studio Live Pro - factory presets (channel + master) and user preset files
#pragma once
#include <functional>
#include <fstream>
#include "../Scenes/Controller.h"

namespace slp {

struct ChannelPreset { std::string category, name; std::function<void(ChannelState&)> apply; };
struct MasterPreset { std::string category, name; std::function<void(MasterState&)> apply; };

namespace pr {
inline void band(ChannelState& s, int i, FilterType t, float f, float g, float q = 1.f, int slope = 12) { s.eq.band[(size_t)i] = {true, t, f, g, q, slope}; }
inline void comp(ChannelState& s, CompModel m, float thr, float ratio, float att, float rel, float knee = 6.f, float mk = 0.f) { s.compOn = true; s.comp.model = m; s.comp.thresholdDb = thr; s.comp.ratio = ratio; s.comp.attackMs = att; s.comp.releaseMs = rel; s.comp.kneeDb = knee; s.comp.makeupDb = mk; }
inline void gate(ChannelState& s, float thr) { s.gateOn = true; s.gate.thresholdDb = thr; s.gate.rangeDb = -40; s.gate.releaseMs = 120; }
inline void sat(ChannelState& s, SatModel m, float drive, float mix = 1.f) { s.satOn = true; s.sat.model = m; s.sat.driveDb = drive; s.sat.mix = mix; s.sat.oversample = 1; }
inline void deess(ChannelState& s, float thr, float f = 7000.f) { s.deessOn = true; s.deess.thresholdDb = thr; s.deess.freqHz = f; s.deess.rangeDb = 8; }
}

inline std::vector<ChannelPreset> factoryChannelPresets() {
    using namespace pr; std::vector<ChannelPreset> p;
    p.push_back({"Vocal", "Lead Vocal - Natural", [](ChannelState& s) { s.hpfHz = 90; band(s, 0, FilterType::Bell, 320, -2.f, 1.2f); band(s, 1, FilterType::Bell, 3500, 1.5f, 1.f); band(s, 2, FilterType::HighShelf, 10000, 1.5f, 0.7f); deess(s, -32); comp(s, CompModel::Optical, -20, 3, 12, 140, 8, 3); }});
    p.push_back({"Vocal", "Lead Vocal - Studio", [](ChannelState& s) { s.hpfHz = 100; band(s, 0, FilterType::Bell, 280, -2.5f, 1.3f); band(s, 1, FilterType::Bell, 4000, 2.f, 1.f); band(s, 2, FilterType::HighShelf, 12000, 2.f, 0.7f); deess(s, -30); comp(s, CompModel::FET, -22, 4, 6, 100, 3, 4); sat(s, SatModel::Tube, 4, 0.5f); s.fxDb[0] = -18; }});
    p.push_back({"Vocal", "Backing Vocal", [](ChannelState& s) { s.hpfHz = 140; band(s, 0, FilterType::Bell, 400, -2.f, 1.f); band(s, 1, FilterType::Bell, 3000, 1.f, 1.f); comp(s, CompModel::Optical, -22, 3, 15, 180); s.fxDb[0] = -12; s.width = 1.2f; }});
    p.push_back({"Church", "Pastor / Preaching", [](ChannelState& s) { s.hpfHz = 110; gate(s, -52); band(s, 0, FilterType::Bell, 250, -2.5f, 1.2f); band(s, 1, FilterType::Bell, 2800, 2.f, 1.f); deess(s, -30); comp(s, CompModel::VCA, -22, 3, 8, 160, 6, 3); }});
    p.push_back({"Church", "Choir Mic", [](ChannelState& s) { s.hpfHz = 120; band(s, 0, FilterType::Bell, 350, -2.f, 0.9f); band(s, 1, FilterType::HighShelf, 9000, 1.5f, 0.7f); comp(s, CompModel::Optical, -24, 2.5f, 20, 200); s.fxDb[0] = -10; }});
    p.push_back({"Podcast", "Podcast Voice", [](ChannelState& s) { s.hpfHz = 80; gate(s, -50); band(s, 0, FilterType::Bell, 200, 1.5f, 0.9f); band(s, 1, FilterType::Bell, 450, -2.f, 1.2f); band(s, 2, FilterType::Bell, 4500, 2.f, 1.f); deess(s, -30); comp(s, CompModel::Clean, -22, 3.5f, 8, 150, 6, 4); s.noiseOn = false; }});
    p.push_back({"Broadcast", "Radio Voice", [](ChannelState& s) { s.hpfHz = 80; band(s, 0, FilterType::LowShelf, 150, 2.f, 0.7f); band(s, 1, FilterType::Bell, 5000, 2.5f, 1.f); deess(s, -32); comp(s, CompModel::VCA, -24, 4, 4, 120, 4, 5); sat(s, SatModel::Console, 3, 0.6f); }});
    p.push_back({"YouTube", "YouTube Voice", [](ChannelState& s) { s.hpfHz = 90; gate(s, -48); band(s, 0, FilterType::Bell, 300, -2.f, 1.2f); band(s, 1, FilterType::Bell, 3500, 2.f, 1.f); deess(s, -31); comp(s, CompModel::Clean, -21, 3.5f, 8, 130, 6, 4); }});
    p.push_back({"Drum", "Kick - Punch", [](ChannelState& s) { s.hpfHz = 30; band(s, 0, FilterType::Bell, 60, 2.5f, 1.2f); band(s, 1, FilterType::Bell, 350, -4.f, 1.5f); band(s, 2, FilterType::Bell, 4000, 2.f, 1.f); gate(s, -35); comp(s, CompModel::FET, -18, 4, 15, 80, 2); s.transOn = true; s.trans.attack = 0.3f; }});
    p.push_back({"Drum", "Snare - Crack", [](ChannelState& s) { s.hpfHz = 80; band(s, 0, FilterType::Bell, 200, 2.f, 1.2f); band(s, 1, FilterType::Bell, 500, -3.f, 1.5f); band(s, 2, FilterType::Bell, 5000, 2.5f, 1.f); gate(s, -38); comp(s, CompModel::FET, -18, 4, 10, 90, 2); s.transOn = true; s.trans.attack = 0.4f; s.trans.sustain = -0.1f; }});
    p.push_back({"Drum", "Drum Bus - Glue", [](ChannelState& s) { s.hpfHz = 35; band(s, 0, FilterType::Bell, 400, -2.f, 1.f); band(s, 1, FilterType::HighShelf, 10000, 1.5f, 0.7f); comp(s, CompModel::Bus, -16, 3, 30, 120, 6, 2); sat(s, SatModel::Tape, 5, 0.5f); }});
    p.push_back({"Bass", "Bass DI - Solid", [](ChannelState& s) { s.hpfHz = 35; band(s, 0, FilterType::Bell, 80, 2.f, 1.f); band(s, 1, FilterType::Bell, 300, -3.f, 1.2f); band(s, 2, FilterType::Bell, 1500, 1.5f, 1.f); comp(s, CompModel::VCA, -20, 4, 20, 120, 4, 3); sat(s, SatModel::Transformer, 6, 0.6f); }});
    p.push_back({"Guitar", "Acoustic Guitar", [](ChannelState& s) { s.hpfHz = 90; band(s, 0, FilterType::Bell, 220, -2.f, 1.f); band(s, 1, FilterType::Bell, 3000, 1.5f, 1.f); band(s, 2, FilterType::HighShelf, 11000, 2.f, 0.7f); comp(s, CompModel::Optical, -22, 3, 15, 160); s.stereo = false; s.fxDb[0] = -14; }});
    p.push_back({"Guitar", "Electric Guitar", [](ChannelState& s) { s.hpfHz = 100; band(s, 0, FilterType::Bell, 400, -2.f, 1.2f); band(s, 1, FilterType::Bell, 3500, 1.5f, 1.f); band(s, 2, FilterType::LowPass, 11000, 0.f, 0.7f, 12); comp(s, CompModel::VCA, -20, 3, 15, 120); sat(s, SatModel::Console, 3, 0.4f); }});
    p.push_back({"Keyboard", "Keys / Synth", [](ChannelState& s) { s.hpfHz = 60; band(s, 0, FilterType::Bell, 300, -1.5f, 1.f); band(s, 1, FilterType::HighShelf, 9000, 1.f, 0.7f); comp(s, CompModel::Clean, -24, 2.f, 20, 200); s.stereo = true; s.fxDb[0] = -14; }});
    p.push_back({"Piano", "Grand Piano", [](ChannelState& s) { s.hpfHz = 40; band(s, 0, FilterType::Bell, 250, -2.f, 1.f); band(s, 1, FilterType::Bell, 3500, 1.f, 1.f); comp(s, CompModel::Optical, -24, 2.f, 25, 250, 10); s.stereo = true; s.fxDb[0] = -12; }});
    p.push_back({"Live", "Live Vocal - Low Latency", [](ChannelState& s) { s.hpfHz = 100; band(s, 0, FilterType::Bell, 300, -2.f, 1.2f); band(s, 1, FilterType::Bell, 3500, 1.5f, 1.f); comp(s, CompModel::VCA, -20, 3, 8, 120, 6, 2); s.sat.oversample = 1; }});
    return p;
}
inline std::vector<MasterPreset> factoryMasterPresets() {
    std::vector<MasterPreset> p;
    auto base = [](MasterState& m, float ceil, float thr) { m.limOn = true; m.lim.ceilingDb = ceil; m.lim.thresholdDb = thr; m.lim.truePeak = true; m.stereoOn = true; m.stereo.bassMonoHz = 120; };
    p.push_back({"Master", "Clean Master", [=](MasterState& m) { base(m, -1.f, -2.f); m.glueOn = true; m.glue.thresholdDb = -10; m.glue.ratio = 1.8f; }});
    p.push_back({"YouTube", "YouTube Live (-14 LUFS target)", [=](MasterState& m) { base(m, -1.f, -4.f); m.glueOn = true; m.glue.thresholdDb = -14; m.glue.ratio = 2.2f; m.clipOn = true; m.clip.thresholdDb = -3; m.clip.ceilingDb = -1.5f; m.eqOn = true; m.eq.band[0] = {true, FilterType::HighPass, 30.f, 0.f, 0.707f, 12}; }});
    p.push_back({"Broadcast", "Broadcast (-23 LUFS EBU R128)", [=](MasterState& m) { base(m, -1.f, 0.f); m.glueOn = true; m.glue.thresholdDb = -20; m.glue.ratio = 2.f; }});
    p.push_back({"Church", "Church Live", [=](MasterState& m) { base(m, -1.f, -3.f); m.glueOn = true; m.glue.thresholdDb = -16; m.glue.ratio = 2.f; m.eqOn = true; m.eq.band[0] = {true, FilterType::HighPass, 35.f, 0.f, 0.707f, 12}; m.eq.band[1] = {true, FilterType::Bell, 300.f, -1.5f, 1.f, 12}; }});
    p.push_back({"Podcast", "Podcast Master", [=](MasterState& m) { base(m, -1.f, -3.f); m.glueOn = true; m.glue.thresholdDb = -16; m.stereo.bassMonoHz = 0; m.stereo.mono = false; }});
    p.push_back({"Live", "Band Live", [=](MasterState& m) { base(m, -1.f, -3.f); m.glueOn = true; m.glue.thresholdDb = -12; m.glue.ratio = 2.f; m.satOn = true; m.sat.model = SatModel::Console; m.sat.driveDb = 2; m.sat.mix = 0.4f; }});
    return p;
}
inline bool saveUserPreset(const std::string& path, ChannelState s) { std::ofstream f(path); if (!f) return false; f << serialize(s, "preset"); return (bool)f; }
inline bool loadUserPreset(const std::string& path, ChannelState& s) { std::ifstream f(path); if (!f) return false; std::string t((std::istreambuf_iterator<char>(f)), {}); if (t.empty()) return false; deserialize(s, t, "preset"); return true; }

} // namespace slp
