// Studio Live Pro - plain-data mixer state (trivially copyable => realtime-safe hand-off UI -> audio thread)
#pragma once
#include "../DSP/Eq.h"
#include "../DSP/Dynamics.h"
#include "../DSP/Saturation.h"
#include "../DSP/Limiter.h"
#include "../DSP/Time.h"
#include "../DSP/Spectral.h"
#include "Serialize.h"
#include <type_traits>

namespace slp {

constexpr int kMaxChannels = 64, kNumSubs = 4, kNumAux = 4, kNumFx = 2, kNumOutputs = 2 + 2 * kNumAux; // out: main, monitor/aux0, aux1..3

struct ChannelState {
    char name[24] = "Channel";
    bool active = true, stereo = false; int inputIndex = 0;
    float inputGainDb = 0.f; bool phaseInvert = false; float hpfHz = 0.f;
    bool noiseOn = false, humOn = false;                       // (per-channel noise/hum are offered by the Live Studio presets; processed in ChannelStrip)
    HumParams hum; NoiseParams noise;
    bool gateOn = false; GateParams gate;
    bool eqOn = true; EqParams eq;
    bool dynEqOn = false; DynEqParams dyn;
    bool deessOn = false; DeEssParams deess;
    bool compOn = false; CompParams comp;
    bool satOn = false; SatParams sat;
    bool transOn = false; TransientParams trans;
    float pan = 0.f, width = 1.f, faderDb = 0.f; bool mute = false, solo = false;
    int busAssign = -1;                                         // -1 = master, 0..3 = subgroup
    float auxDb[kNumAux] = {-100.f, -100.f, -100.f, -100.f}; bool auxPre = false;   // -100 = off
    float fxDb[kNumFx] = {-100.f, -100.f};

    template <class V> void visit(V& v) {
        v.text("name", name, sizeof name); v.field("active", active); v.field("stereo", stereo); v.field("input", inputIndex);
        v.field("gain", inputGainDb); v.field("phase", phaseInvert); v.field("hpf", hpfHz);
        v.field("noiseOn", noiseOn); v.field("humOn", humOn);
        v.push("hum"); hum.visit(v); v.pop(); v.push("noise"); noise.visit(v); v.pop();
        v.field("gateOn", gateOn); v.push("gate"); gate.visit(v); v.pop();
        v.field("eqOn", eqOn); v.push("eq"); eq.visit(v); v.pop();
        v.field("dynOn", dynEqOn); v.push("dyn"); dyn.visit(v); v.pop();
        v.field("deessOn", deessOn); v.push("deess"); deess.visit(v); v.pop();
        v.field("compOn", compOn); v.push("comp"); comp.visit(v); v.pop();
        v.field("satOn", satOn); v.push("sat"); sat.visit(v); v.pop();
        v.field("transOn", transOn); v.push("trans"); trans.visit(v); v.pop();
        v.field("pan", pan); v.field("width", width); v.field("fader", faderDb); v.field("mute", mute); v.field("solo", solo);
        v.field("bus", busAssign); v.field("auxPre", auxPre);
        for (int i = 0; i < kNumAux; ++i) v.field(("aux" + std::to_string(i)).c_str(), auxDb[i]);
        for (int i = 0; i < kNumFx; ++i) v.field(("fx" + std::to_string(i)).c_str(), fxDb[i]);
    }
};

struct StereoParams {
    float width = 1.f, balance = 0.f, bassMonoHz = 0.f; bool mono = false;
    template <class V> void visit(V& v) { v.field("width", width); v.field("bal", balance); v.field("bassmono", bassMonoHz); v.field("mono", mono); }
};
struct MonitorState {
    float levelDb = 0.f, dimDb = -20.f; bool dim = false, mono = false, mute = false; int source = 0; // 0 = master, 1 = aux0 (headphone mix)
    template <class V> void visit(V& v) { v.field("level", levelDb); v.field("dimdb", dimDb); v.field("dim", dim); v.field("mono", mono); v.field("mute", mute); v.field("src", source); }
};

struct MasterState {
    float faderDb = 0.f; bool mute = false;
    bool eqOn = false; EqParams eq; bool dynEqOn = false; DynEqParams dyn; bool glueOn = false; CompParams glue;
    bool mbOn = false; MbParams mb; bool satOn = false; SatParams sat; bool stereoOn = true; StereoParams stereo;
    bool clipOn = false; ClipParams clip; bool limOn = true; LimiterParams lim;
    float subFaderDb[kNumSubs] = {0, 0, 0, 0}; bool subMute[kNumSubs] = {false, false, false, false};
    float auxMasterDb[kNumAux] = {0, 0, 0, 0}; float fxReturnDb[kNumFx] = {0, 0}; bool fxOn[kNumFx] = {true, true};
    ReverbParams reverb; DelayParams delay; MonitorState monitor; float matchTrimDb = 0.f;
    MasterState() { glue.model = CompModel::Bus; glue.thresholdDb = -14; glue.ratio = 2; glue.attackMs = 30; glue.releaseMs = 200; glue.kneeDb = 6; glue.autoGain = false;
        for (auto& b : mb.band) { b.model = CompModel::Master; b.thresholdDb = -20; b.ratio = 2; b.attackMs = 20; b.releaseMs = 150; } }
    template <class V> void visit(V& v) {
        v.field("fader", faderDb); v.field("mute", mute);
        v.field("eqOn", eqOn); v.push("eq"); eq.visit(v); v.pop(); v.field("dynOn", dynEqOn); v.push("dyn"); dyn.visit(v); v.pop();
        v.field("glueOn", glueOn); v.push("glue"); glue.visit(v); v.pop(); v.field("mbOn", mbOn); v.push("mb"); mb.visit(v); v.pop();
        v.field("satOn", satOn); v.push("sat"); sat.visit(v); v.pop(); v.field("stereoOn", stereoOn); v.push("stereo"); stereo.visit(v); v.pop();
        v.field("clipOn", clipOn); v.push("clip"); clip.visit(v); v.pop(); v.field("limOn", limOn); v.push("lim"); lim.visit(v); v.pop();
        for (int i = 0; i < kNumSubs; ++i) { v.field(("subF" + std::to_string(i)).c_str(), subFaderDb[i]); v.field(("subM" + std::to_string(i)).c_str(), subMute[i]); }
        for (int i = 0; i < kNumAux; ++i) v.field(("auxM" + std::to_string(i)).c_str(), auxMasterDb[i]);
        for (int i = 0; i < kNumFx; ++i) { v.field(("fxR" + std::to_string(i)).c_str(), fxReturnDb[i]); v.field(("fxOn" + std::to_string(i)).c_str(), fxOn[i]); }
        v.push("reverb"); reverb.visit(v); v.pop(); v.push("delay"); delay.visit(v); v.pop(); v.push("mon"); monitor.visit(v); v.pop(); v.field("match", matchTrimDb);
    }
};

static_assert(std::is_trivially_copyable<ChannelState>::value, "ChannelState must be trivially copyable");
static_assert(std::is_trivially_copyable<MasterState>::value, "MasterState must be trivially copyable");

template <class T> std::string serialize(T& s, const std::string& prefix = "") { TextWriter w; if (!prefix.empty()) w.push(prefix); s.visit(w); return w.str(); }
template <class T> void deserialize(T& s, const std::string& text, const std::string& prefix = "") { TextReader r(text); if (!prefix.empty()) r.push(prefix); s.visit(r); }

} // namespace slp
