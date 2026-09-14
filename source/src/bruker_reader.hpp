#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace xrr {
struct Point {
    double twoTheta = 0;
    double intensity = 0;
    bool gapBefore = false;
};
struct Scan {
    std::string name, format, scanType;
    std::size_t records = 0, unmeasured = 0, nonfinite = 0, nonpositive = 0;
    double nominalStart = 0, nominalEnd = 0, step = 0, wavelength = 0;
    // Optional second spectral line. secondaryRatio is I2/I1; zero disables it.
    double secondaryWavelength = 0, secondaryRatio = 0;
    std::vector<Point> points;
    std::vector<std::string> warnings;
};
// Reads RAW4.00 scalar coupled 2Theta scans or Bruker sectioned TXT.
// -9999 is removed at ingestion; gaps and original angles are preserved.
std::vector<Scan> readBruker(const std::filesystem::path& path);
}
