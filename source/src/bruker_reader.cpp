#include "bruker_reader.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace xrr {
namespace {
constexpr std::size_t maxFileBytes = 256u * 1024u * 1024u;
std::string trim(std::string s) {
    auto white = [](unsigned char c) { return std::isspace(c) != 0; };
    auto a = std::find_if_not(s.begin(), s.end(), white);
    auto b = std::find_if_not(s.rbegin(), s.rend(), white).base();
    return a < b ? std::string(a, b) : std::string();
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool number(const std::string& s, double& v) {
    const auto t = trim(s);
    if (t.empty()) return false;
    const auto l = lower(t);
    if (l == "nan" || l == "+nan" || l == "-nan") {
        v = std::numeric_limits<double>::quiet_NaN(); return true;
    }
    if (l == "inf" || l == "+inf" || l == "infinity" || l == "-inf") {
        v = l[0] == '-' ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity(); return true;
    }
    std::istringstream in(t);
    in.imbue(std::locale::classic());
    if (!(in >> v)) return false;
    in >> std::ws;
    return in.eof();
}
void append(Scan& s, double x, double y, bool& pendingGap) {
    ++s.records;
    if (y == -9999.0) { ++s.unmeasured; pendingGap = true; return; }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        ++s.nonfinite; pendingGap = true; return;
    }
    if (y <= 0) ++s.nonpositive;
    s.points.push_back({x, y, pendingGap});
    pendingGap = false;
}
struct Bytes {
    const std::vector<unsigned char>& data;
    void check(std::size_t p, std::size_t n) const {
        if (p > data.size() || n > data.size() - p)
            throw std::runtime_error("Truncated RAW4 file at byte " + std::to_string(p));
    }
    std::uint32_t u32(std::size_t p) const {
        check(p, 4);
        return std::uint32_t(data[p]) | (std::uint32_t(data[p+1]) << 8) |
               (std::uint32_t(data[p+2]) << 16) | (std::uint32_t(data[p+3]) << 24);
    }
    float f32(std::size_t p) const {
        auto bits = u32(p); float x;
        std::memcpy(&x, &bits, 4); return x;
    }
    double f64(std::size_t p) const {
        std::uint64_t bits = u32(p);
        bits |= std::uint64_t(u32(p+4)) << 32;
        double x; std::memcpy(&x, &bits, 8); return x;
    }
    std::string str(std::size_t p, std::size_t n) const {
        check(p, n); std::string s;
        for (std::size_t i = 0; i < n && data[p+i] != 0; ++i)
            s += static_cast<char>(data[p+i]);
        return trim(s);
    }
    std::size_t segmentLength(std::size_t p, std::size_t limit) const {
        if (p > limit || limit - p < 8) throw std::runtime_error("Incomplete RAW4 metadata segment");
        const auto n = u32(p+4);
        if (n < 8 || n > limit - p) throw std::runtime_error("Invalid RAW4 metadata length at byte " + std::to_string(p));
        return n;
    }
};
bool coupled(const std::string& type) {
    const auto t = lower(trim(type));
    return t == "locked coupled" || t == "unlocked coupled";
}
std::vector<Scan> raw4(const std::vector<unsigned char>& bytes, const std::string& filename) {
    static_assert(sizeof(float) == 4 && sizeof(double) == 8, "IEEE floating point required");
    Bytes b{bytes}; b.check(0, 61);
    std::size_t p = 61;
    double globalLambda = 0;
    // Variable-length metadata segments: never assume a fixed data offset.
    while (p < bytes.size()) {
        const auto type = b.u32(p);
        if (type == 0 || type == 160) break;
        const auto n = b.segmentLength(p, bytes.size());
        if (type == 30 && n >= 88) globalLambda = b.f64(p+80);
        p += n;
    }
    std::vector<Scan> scans;
    while (p < bytes.size()) {
        b.check(p, 160);
        if (b.u32(p) != 0 && b.u32(p) != 160)
            throw std::runtime_error("Unexpected RAW4 range marker at byte " + std::to_string(p));
        Scan s;
        s.name = filename + " / range " + std::to_string(scans.size()+1);
        s.format = "RAW4.00";
        s.scanType = b.str(p+32, 24);
        if (!coupled(s.scanType))
            throw std::runtime_error("Unsupported RAW4 scan type: " + s.scanType + ". Only coupled 2Theta scans are supported; a rocking-curve axis must not be mislabeled 2Theta.");
        const auto n = b.u32(p+88), recordBytes = b.u32(p+136), extra = b.u32(p+140);
        if (recordBytes != 4 || b.u32(p+20) != 1 || b.u32(p+24) != 0 || b.u32(p+124) != 1)
            throw std::runtime_error("Unsupported RAW4 layout: expected one scalar 32-bit intensity per point and no varying parameters");
        s.nominalStart = b.f64(p+72); s.step = b.f64(p+80);
        s.wavelength = b.f64(p+112);
        if (!std::isfinite(s.wavelength) || s.wavelength <= 0) s.wavelength = globalLambda;
        if (!n || n > maxFileBytes/4 || !std::isfinite(s.nominalStart) || !std::isfinite(s.step) || (n > 1 && s.step == 0))
            throw std::runtime_error("Invalid RAW4 point count/start/increment");
        s.nominalEnd = s.nominalStart + double(n-1) * s.step;
        if (!std::isfinite(s.nominalEnd)) throw std::runtime_error("RAW4 angle overflow");
        const auto headers = p + 160;
        b.check(headers, extra);
        const auto dataStart = headers + extra;
        for (auto h = headers; h < dataStart;) {
            const auto len = b.segmentLength(h, dataStart);
            if (b.u32(h) == 50 && len >= 64) {
                const auto drive = lower(b.str(h+12, 24));
                if (drive == "2theta" || drive == "twotheta") {
                    const double driveStart = b.f64(h+56);
                    if (!std::isfinite(driveStart) || std::abs(driveStart-s.nominalStart) > 1e-5)
                        throw std::runtime_error("RAW4 range start disagrees with the 2Theta drive; cannot safely assign the x axis");
                }
            }
            h += len;
        }
        b.check(dataStart, std::size_t(n)*4);
        bool gap = false;
        s.points.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            append(s, s.nominalStart + double(i)*s.step, b.f32(dataStart+i*4), gap);
        scans.push_back(std::move(s));
        p = dataStart + std::size_t(n)*4;
    }
    if (scans.empty()) throw std::runtime_error("No RAW4 measurement ranges found");
    return scans;
}
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> v;
    if (line.find(',') != std::string::npos || line.find(';') != std::string::npos) {
        const char sep = line.find(',') != std::string::npos ? ',' : ';';
        std::istringstream in(line); std::string x;
        while (std::getline(in, x, sep)) v.push_back(trim(x));
    } else {
        std::istringstream in(line); std::string x;
        while (in >> x) v.push_back(x);
    }
    while (!v.empty() && v.back().empty()) v.pop_back();
    return v;
}
std::vector<Scan> textData(const std::vector<unsigned char>& bytes, const std::string& filename) {
    std::string text(bytes.begin(), bytes.end());
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    if (text.find('\0') != std::string::npos)
        throw std::runtime_error("Unsupported binary/text encoding; use a RAW4.00 or UTF-8/ASCII TXT export");
    std::istringstream in(text); std::string line, section;
    std::vector<Scan> scans;
    std::unordered_map<std::string, std::string> meta, hardware;
    Scan s; bool active = false, gap = false, headerSeen = false;
    std::size_t lineNo = 0, declared = 0;
    auto finish = [&] {
        if (!active) return;
        if (!s.records) throw std::runtime_error("Empty [Data] section");
        if (declared && declared != s.records)
            throw std::runtime_error("TXT Steps does not match the number of data records");
        scans.push_back(std::move(s)); active = false;
    };
    while (std::getline(in, line)) {
        ++lineNo; line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            finish(); section = lower(trim(line.substr(1, line.size()-2)));
            if (section == "rangeheader") meta.clear();
            if (section == "data") {
                s = Scan{}; s.name = filename + " / range " + std::to_string(scans.size()+1);
                s.format = "Bruker TXT"; s.scanType = meta["scantype"];
                if (!s.scanType.empty() && !coupled(s.scanType))
                    throw std::runtime_error("Unsupported TXT scan type: " + s.scanType + ". Expected a coupled 2Theta scan.");
                for (const auto& key : {"numberofcounts", "numberofdetectors"}) {
                    double count = 1;
                    if (meta.count(key) && (!number(meta[key],count) || count != 1))
                        throw std::runtime_error("TXT multi-channel data are not supported");
                }
                double d = 0;
                declared = 0;
                if (meta.count("steps")) {
                    if (!number(meta["steps"],d) || !std::isfinite(d) || d < 1 || d > maxFileBytes/4 || std::floor(d) != d)
                        throw std::runtime_error("Invalid TXT Steps value");
                    declared = static_cast<std::size_t>(d);
                }
                if (number(meta["increment"],d) && std::isfinite(d)) s.step = d;
                if (number(meta["actuallyusedlambda"],d) && std::isfinite(d)) s.wavelength = d;
                double alpha2 = 0, ratio = 0, monochromator = 0, analyzer = 0;
                const bool hasAlpha2 = number(hardware["alpha2"],alpha2) && std::isfinite(alpha2) && alpha2 > 0;
                const bool hasRatio = number(hardware["alpharatio"],ratio) && std::isfinite(ratio) && ratio > 0;
                const bool hasMonochromator = number(hardware["monochromator"],monochromator) && std::isfinite(monochromator);
                const bool hasAnalyzer = number(hardware["analyzer"],analyzer) && std::isfinite(analyzer);
                if (hasAlpha2 && hasRatio && (!hasMonochromator || monochromator == 0) && (!hasAnalyzer || analyzer == 0)
                    && std::abs(alpha2-s.wavelength) > 1.0e-8) {
                    s.secondaryWavelength = alpha2;
                    s.secondaryRatio = ratio;
                }
                active = true; gap = false; headerSeen = false;
            }
            continue;
        }
        if (section == "hardwareconfiguration") {
            const auto eq = line.find('=');
            if (eq != std::string::npos) hardware[lower(trim(line.substr(0,eq)))] = trim(line.substr(eq+1));
            continue;
        }
        if (section == "rangeheader") {
            const auto eq = line.find('=');
            if (eq != std::string::npos) meta[lower(trim(line.substr(0,eq)))] = trim(line.substr(eq+1));
            continue;
        }
        if (!active) continue;
        const auto f = fields(line);
        double x = 0, y = 0;
        if (f.size() == 2 && number(f[0],x) && number(f[1],y)) {
            if (!s.records) s.nominalStart = x;
            s.nominalEnd = x;
            append(s, x, y, gap);
        } else if (!s.records && !headerSeen && f.size() == 2) {
            const auto axis = lower(f[0]);
            if (axis != "angle" && axis != "2theta" && axis != "twotheta" && axis != "2-theta")
                throw std::runtime_error("Unrecognized TXT angle column at line " + std::to_string(lineNo));
            headerSeen = true;
        } else {
            throw std::runtime_error("Malformed or unsupported TXT data row at line " + std::to_string(lineNo));
        }
    }
    finish();
    if (scans.empty()) throw std::runtime_error("No [Data] section found. Export Bruker RAW as sectioned TXT (Angle, Intensity).");
    return scans;
}
}
std::vector<Scan> readBruker(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open input: " + path.u8string());
    const auto size = in.tellg();
    if (size <= 0 || static_cast<std::uintmax_t>(size) > maxFileBytes)
        throw std::runtime_error("Input must be non-empty and smaller than 256 MiB");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    in.seekg(0); in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in) throw std::runtime_error("Input read failed");
    const std::string filename = path.filename().u8string();
    if (bytes.size() >= 8 && std::memcmp(bytes.data(), "RAW4.00\0", 8) == 0)
        return raw4(bytes, filename);
    if (bytes.size() >= 3 && std::memcmp(bytes.data(), "RAW", 3) == 0)
        throw std::runtime_error("This RAW version is unsupported. RAW4.00 is supported; RAW1/2/3 are not.");
    return textData(bytes, filename);
}
}
