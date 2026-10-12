// Studio Live Pro - MIDI mapping (CC / Note) onto the controller
#pragma once
#include "../Scenes/Controller.h"

namespace slp {

enum class MidiTarget { ChannelFader, ChannelMute, ChannelSolo, ChannelPan, MasterFader, EmergencyBypass, SceneRecall, SnapshotRecall, MasterBypassChain, PresetNext };
struct MidiBinding { bool isNote = false; int channel = 0; /*0 = omni, 1..16*/ int number = 0; MidiTarget target = MidiTarget::ChannelFader; int index = 0; float minDb = -60.f, maxDb = 6.f; };

class MidiMapper {
public:
    std::vector<MidiBinding> bindings;
    void add(const MidiBinding& b) { bindings.push_back(b); }
    void clear() { bindings.clear(); }
    // learn: next incoming control is bound to `pending`
    void startLearn(const MidiBinding& pending) { learn_ = true; pending_ = pending; }
    bool learning() const { return learn_; }
    // Returns true if the message was consumed. status: 0x80..0xEF.
    bool handle(int status, int d1, int d2, MixerController& c, MixerEngine* eng = nullptr) {
        const int type = status & 0xF0, chn = (status & 0x0F) + 1; bool isNote = false, on = false;
        if (type == 0x90) { isNote = true; on = d2 > 0; } else if (type == 0x80) { isNote = true; on = false; } else if (type != 0xB0) return false;
        if (learn_) { MidiBinding b = pending_; b.isNote = isNote; b.number = d1; b.channel = chn; bindings.push_back(b); learn_ = false; return true; }
        bool used = false;
        for (auto& b : bindings) {
            if (b.isNote != isNote || b.number != d1 || (b.channel != 0 && b.channel != chn)) continue;
            used = true; const float v = isNote ? (on ? 1.f : 0.f) : (float)d2 / 127.f; const bool pressed = isNote ? on : d2 >= 64;
            switch (b.target) {
            case MidiTarget::ChannelFader: if (b.index < c.numChannels() && !isNote) { c.ch[(size_t)b.index].faderDb = v <= 0.f ? -100.f : b.minDb + (b.maxDb - b.minDb) * v; c.commitChannel(b.index); } break;
            case MidiTarget::ChannelPan: if (b.index < c.numChannels() && !isNote) { c.ch[(size_t)b.index].pan = v * 2.f - 1.f; c.commitChannel(b.index); } break;
            case MidiTarget::ChannelMute: if (b.index < c.numChannels() && (isNote ? on : true)) { auto& s = c.ch[(size_t)b.index]; s.mute = isNote ? !s.mute : pressed; c.commitChannel(b.index); } break;
            case MidiTarget::ChannelSolo: if (b.index < c.numChannels() && (isNote ? on : true)) { auto& s = c.ch[(size_t)b.index]; s.solo = isNote ? !s.solo : pressed; c.commitChannel(b.index); } break;
            case MidiTarget::MasterFader: if (!isNote) { c.master.faderDb = v <= 0.f ? -100.f : b.minDb + (b.maxDb - b.minDb) * v; c.commitMaster(); } break;
            case MidiTarget::EmergencyBypass: if (eng && (isNote ? on : true)) eng->setEmergencyBypass(isNote ? !eng->emergencyBypass() : pressed); break;
            case MidiTarget::SceneRecall: if (pressed) c.loadScene(b.index); break;
            case MidiTarget::SnapshotRecall: if (pressed) c.snapshotLoad(b.index); break;
            case MidiTarget::MasterBypassChain: if (isNote ? on : true) { c.master.limOn = isNote ? !c.master.limOn : !pressed; c.commitMaster(); } break;
            case MidiTarget::PresetNext: break;
            }
        }
        return used;
    }
    std::string toText() const { std::string t; for (auto& b : bindings) t += std::to_string(b.isNote) + " " + std::to_string(b.channel) + " " + std::to_string(b.number) + " " + std::to_string((int)b.target) + " " + std::to_string(b.index) + " " + std::to_string(b.minDb) + " " + std::to_string(b.maxDb) + "\n"; return t; }
    void fromText(const std::string& t) { bindings.clear(); std::istringstream is(t); MidiBinding b; int n, tg; while (is >> n >> b.channel >> b.number >> tg >> b.index >> b.minDb >> b.maxDb) { b.isNote = n != 0; b.target = (MidiTarget)tg; bindings.push_back(b); } }
private:
    bool learn_ = false; MidiBinding pending_;
};

} // namespace slp
