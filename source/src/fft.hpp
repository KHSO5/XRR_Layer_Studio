#pragma once

#include "bruker_reader.hpp"
#include <cstddef>
#include <optional>
#include <vector>

namespace xrr {

struct ThicknessFftOptions {
    std::optional<double> twoThetaMinimum;
    std::optional<double> twoThetaMaximum;
    double maximumThicknessNm = 200.0;
    std::size_t maximumPeaks = 8;
};

struct ThicknessSpectrumPoint {
    double thicknessNm = 0;
    double relativeAmplitude = 0;
};

struct ThicknessPeak {
    double thicknessNm = 0;
    double relativeAmplitude = 0;
};

struct FringeSpacingThickness {
    double twoThetaDifferenceDegrees = 0;
    double qDifferenceInverseAngstrom = 0;
    double thicknessNm = 0;
};

struct ThicknessFftResult {
    double twoThetaMinimum = 0;
    double twoThetaMaximum = 0;
    double qMinimumInverseAngstrom = 0;
    double qMaximumInverseAngstrom = 0;
    double qStepInverseAngstrom = 0;
    double minimumReliableThicknessNm = 0;
    double thicknessResolutionNm = 0;
    double nyquistThicknessNm = 0;
    double displayedMaximumThicknessNm = 0;
    std::size_t selectedPoints = 0;
    std::size_t resampledPoints = 0;
    std::size_t missingGridPoints = 0;
    std::size_t fftSize = 0;
    std::vector<ThicknessSpectrumPoint> spectrum;
    std::vector<ThicknessPeak> peaks;
};

// Estimates periodic distances from Kiessig fringes. The measured signal is
// resampled on an equally spaced qz grid, transformed as log10(I), detrended
// quadratically, Hann-windowed, and zero padded before an iterative radix-2 FFT.
ThicknessFftResult estimateThicknessFft(const Scan& scan,const ThicknessFftOptions& options={});

// Converts the exact qz separation of two selected XRR fringe points to a
// single-layer thickness. The two points are assumed to be adjacent fringes.
FringeSpacingThickness thicknessFromFringePoints(double firstTwoThetaDegrees,
                                                 double secondTwoThetaDegrees,
                                                 double wavelengthAngstrom);

}
