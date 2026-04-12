#pragma once
#include <algorithm>
#include <string>
#include <cstdlib>

static inline std::string ltrim(const std::string& s) {
    auto i = s.begin();
    while (i != s.end() && std::isspace((unsigned char)*i)) ++i;
    return {i, s.end()};
}
static inline std::string rtrim(const std::string& s) {
    auto i = s.rbegin();
    while (i != s.rend() && std::isspace((unsigned char)*i)) ++i;
    return {s.begin(), i.base()};
}
static inline std::string trim(const std::string& s) { return ltrim(rtrim(s)); }

static inline std::string replaceAll(std::string s, const std::string& f, const std::string& t) {
    size_t p = 0;
    while ((p = s.find(f, p)) != std::string::npos) { s.replace(p, f.size(), t); p += t.size(); }
    return s;
}
static inline std::string expandHome(const std::string& s) {
    if (s.empty() || s[0] != '~') return s;
    const char* h = getenv("HOME");
    return h ? std::string(h) + s.substr(1) : s;
}
static inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}
