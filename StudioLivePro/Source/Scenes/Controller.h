// Studio Live Pro - UI-side model: scenes, snapshots, undo/redo, A/B (with level match), project save/load
#pragma once
#include <fstream>
#include <memory>
#include "../Mixer/MixerEngine.h"

namespace slp {

struct Scene {
    std::string name = "Empty"; bool used = false;
    std::vector<ChannelState> ch; MasterState master;
};

inline std::string sceneToText(Scene& s, const std::string& pfx) {
    TextWriter w; w.push(pfx); w.os << w.key("name") << '=' << s.name << '\n'; w.os << w.key("count") << '=' << s.ch.size() << '\n';
    for (size_t i = 0; i < s.ch.size(); ++i) { w.push("c" + std::to_string(i)); s.ch[i].visit(w); w.pop(); }
    w.push("m"); s.master.visit(w); w.pop(); return w.str();
}
inline bool sceneFromText(Scene& s, const std::string& text, const std::string& pfx) {
    TextReader r(text); r.push(pfx); auto* cnt = r.get("count"); if (!cnt) return false;
    int n = 0; try { n = std::stoi(*cnt); } catch (...) { return false; } if (n < 0 || n > kMaxChannels) return false;
    if (auto* nm = r.get("name")) s.name = *nm;
    s.ch.assign((size_t)n, ChannelState()); for (int i = 0; i < n; ++i) { r.push("c" + std::to_string(i)); s.ch[(size_t)i].visit(r); r.pop(); }
    r.push("m"); s.master.visit(r); r.pop(); s.used = true; return true;
}

class MixerController {
public:
    static constexpr int kScenes = 32, kUndoMax = 100;
    MixerController(MixerEngine* e, int numChannels) : eng_(e), n_((size_t)numChannels) {
        ch.assign(n_, ChannelState()); for (size_t i = 0; i < n_; ++i) { ch[i].inputIndex = (int)i; std::snprintf(ch[i].name, sizeof ch[i].name, "CH %d", (int)i + 1); }
        scenes.resize(kScenes);
    }
    std::vector<ChannelState> ch; MasterState master; std::vector<Scene> scenes; std::vector<Scene> snapshots; std::string projectName = "Untitled";
    int numChannels() const { return (int)n_; }

    void commitChannel(int i) { if (eng_) eng_->pushChannel(i, ch[(size_t)i]); }
    void commitMaster() { if (eng_) eng_->pushMaster(master); }
    void pushAll() { for (int i = 0; i < (int)n_; ++i) commitChannel(i); commitMaster(); }

    // ---- undo / redo (call beginEdit() before a user gesture changes the model) ----
    void beginEdit() { undo_.push_back(capture("undo")); if (undo_.size() > kUndoMax) undo_.erase(undo_.begin()); redo_.clear(); }
    bool canUndo() const { return !undo_.empty(); } bool canRedo() const { return !redo_.empty(); }
    bool undo() { if (undo_.empty()) return false; redo_.push_back(capture("redo")); apply(undo_.back()); undo_.pop_back(); return true; }
    bool redo() { if (redo_.empty()) return false; undo_.push_back(capture("undo")); apply(redo_.back()); redo_.pop_back(); return true; }

    Scene capture(const std::string& name) const { Scene s; s.name = name; s.used = true; s.ch = ch; s.master = master; return s; }
    void apply(const Scene& s) { for (size_t i = 0; i < std::min(n_, s.ch.size()); ++i) ch[i] = s.ch[i]; master = s.master; pushAll(); }

    // ---- scenes ----
    void saveScene(int idx, const std::string& name) { if (idx >= 0 && idx < kScenes) scenes[(size_t)idx] = capture(name); }
    bool loadScene(int idx) { if (idx < 0 || idx >= kScenes || !scenes[(size_t)idx].used) return false; beginEdit(); apply(scenes[(size_t)idx]); return true; }
    // ---- snapshots ----
    int snapshotSave(const std::string& name) { snapshots.push_back(capture(name)); return (int)snapshots.size() - 1; }
    bool snapshotLoad(int i) { if (i < 0 || i >= (int)snapshots.size()) return false; beginEdit(); apply(snapshots[(size_t)i]); return true; }
    // Compare: returns human-readable list of differences between current state and snapshot i
    std::vector<std::string> snapshotCompare(int i) {
        std::vector<std::string> d; if (i < 0 || i >= (int)snapshots.size()) return d;
        for (size_t c = 0; c < std::min(n_, snapshots[(size_t)i].ch.size()); ++c) if (serialize(ch[c]) != serialize(snapshots[(size_t)i].ch[c])) d.push_back(std::string("channel ") + ch[c].name + " differs");
        if (serialize(master) != serialize(snapshots[(size_t)i].master)) d.push_back("master differs"); return d;
    }

    // ---- A/B with level match ----
    void abStore(int slot) { ab_[slot & 1] = capture(slot ? "B" : "A"); abActive_ = slot & 1; }   // storing makes that slot the live one
    void copyAtoB() { ab_[1] = ab_[0]; ab_[1].name = "B"; } void copyBtoA() { ab_[0] = ab_[1]; ab_[0].name = "A"; }
    // currentShortTermLufs: engine meter value while the current slot was playing; used to match loudness on switch.
    void abSwitch(int toSlot, float currentLufs, bool levelMatch) {
        toSlot &= 1; if (!ab_[toSlot].used) return; abLufs_[abActive_] = currentLufs; ab_[abActive_] = capture(abActive_ ? "B" : "A");
        abActive_ = toSlot; apply(ab_[toSlot]);
        if (levelMatch && abLufs_[0] > -100.f && abLufs_[1] > -100.f) { master.matchTrimDb = clampf(abLufs_[1 - toSlot] - abLufs_[toSlot], -12.f, 12.f); if (toSlot == 0 || true) commitMaster(); }
        else { master.matchTrimDb = 0; commitMaster(); }
    }
    void abReset() { ab_[0] = ab_[1] = Scene(); abLufs_[0] = abLufs_[1] = -120.f; abActive_ = 0; master.matchTrimDb = 0; commitMaster(); }
    int abActive() const { return abActive_; }
    void setBypassAll(bool b) { if (b) { bypassSaved_ = capture("bypass"); for (auto& c : ch) { c.eqOn = c.dynEqOn = c.compOn = c.gateOn = c.deessOn = c.satOn = c.transOn = false; } master.eqOn = master.dynEqOn = master.glueOn = master.mbOn = master.satOn = master.clipOn = false; pushAll(); } else if (bypassSaved_.used) { apply(bypassSaved_); } }   // Before/After

    // ---- project ----
    std::string projectToText() {
        std::string t = "SLP_PROJECT 1\nname=" + projectName + "\n"; Scene cur = capture("current"); t += sceneToText(cur, "cur");
        for (int i = 0; i < kScenes; ++i) if (scenes[(size_t)i].used) t += sceneToText(scenes[(size_t)i], "scene" + std::to_string(i));
        t += "snapcount=" + std::to_string(snapshots.size()) + "\n"; for (size_t i = 0; i < snapshots.size(); ++i) t += sceneToText(snapshots[i], "snap" + std::to_string(i));
        return t;
    }
    bool projectFromText(const std::string& t) {
        if (t.rfind("SLP_PROJECT 1", 0) != 0) return false; Scene cur; if (!sceneFromText(cur, t, "cur")) return false;
        TextReader r(t); if (auto* nm = r.get("name")) projectName = *nm;
        for (int i = 0; i < kScenes; ++i) { Scene s; scenes[(size_t)i] = sceneFromText(s, t, "scene" + std::to_string(i)) ? s : Scene(); }
        snapshots.clear(); if (auto* sc = r.get("snapcount")) { int n = 0; try { n = std::stoi(*sc); } catch (...) {} for (int i = 0; i < n && i < 1000; ++i) { Scene s; if (sceneFromText(s, t, "snap" + std::to_string(i))) snapshots.push_back(s); } }
        apply(cur); return true;
    }
    bool saveProject(const std::string& path) { std::ofstream f(path, std::ios::binary); if (!f) return false; f << projectToText(); return (bool)f; }
    bool openProject(const std::string& path) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::string t((std::istreambuf_iterator<char>(f)), {}); return projectFromText(t); }

private:
    MixerEngine* eng_; size_t n_; std::vector<Scene> undo_, redo_; Scene ab_[2], bypassSaved_; float abLufs_[2] = {-120.f, -120.f}; int abActive_ = 0;
};

} // namespace slp
