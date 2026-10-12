// Studio Live Pro - key=value text serialization via visitor pattern (used by scenes, snapshots, presets, projects)
#pragma once
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace slp {

template <class D> struct EnumMixin {
    template <class E> void enumField(const char* n, E& e) { int i = (int)e; static_cast<D*>(this)->field(n, i); e = (E)i; }
};

struct PrefixStack {
    std::vector<std::string> pre;
    void push(const std::string& s) { pre.push_back(s); }
    void pop() { pre.pop_back(); }
    std::string key(const char* n) const { std::string k; for (auto& p : pre) { k += p; k += '.'; } return k + n; }
};

struct TextWriter : PrefixStack, EnumMixin<TextWriter> {
    std::ostringstream os;
    TextWriter() { os.precision(9); }
    void field(const char* n, float& v) { os << key(n) << '=' << v << '\n'; }
    void field(const char* n, int& v) { os << key(n) << '=' << v << '\n'; }
    void field(const char* n, bool& v) { os << key(n) << '=' << (v ? 1 : 0) << '\n'; }
    void text(const char* n, char* buf, size_t) { std::string s(buf); for (auto& c : s) if (c == '\n' || c == '\r') c = ' '; os << key(n) << '=' << s << '\n'; }
    std::string str() const { return os.str(); }
};

struct TextReader : PrefixStack, EnumMixin<TextReader> {
    std::map<std::string, std::string> kv;
    explicit TextReader(const std::string& text) {
        std::istringstream is(text); std::string line;
        while (std::getline(is, line)) { auto p = line.find('='); if (p != std::string::npos) kv[line.substr(0, p)] = line.substr(p + 1); }
    }
    const std::string* get(const char* n) const { auto it = kv.find(key(n)); return it == kv.end() ? nullptr : &it->second; }
    void field(const char* n, float& v) { if (auto* s = get(n)) { try { v = std::stof(*s); } catch (...) {} } }
    void field(const char* n, int& v) { if (auto* s = get(n)) { try { v = std::stoi(*s); } catch (...) {} } }
    void field(const char* n, bool& v) { if (auto* s = get(n)) v = (*s == "1"); }
    void text(const char* n, char* buf, size_t cap) { if (auto* s = get(n)) { std::snprintf(buf, cap, "%s", s->c_str()); } }
};

} // namespace slp
