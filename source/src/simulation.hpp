#pragma once
#include "project.hpp"
#include <complex>
#include <string>
#include <vector>

namespace xrr {

// Converts a mass density and the leading chemical formula in a material label
// (for example Ru, SiO2, or Co80Tb20) to electron density in e-/Angstrom^3.
double electronDensityAngstrom3(const std::string& material, double densityGcm3);

struct OpticalLayer {
    double sld = 0;                 // r_e * electron density, Angstrom^-2.
    double thicknessAngstrom = 0;   // Zero for the semi-infinite substrate.
    double roughnessAngstrom = 0;   // Roughness of this layer's top interface.
};

// Allocation-aware numerical kernel shared by preview and fitting. The caller
// owns both output and kzWorkspace, so GA worker threads can reuse their buffers.
void calculateParrattReflectivity(const std::vector<double>& twoTheta,
                                  double wavelengthAngstrom,
                                  const std::vector<OpticalLayer>& layers,
                                  std::vector<double>& reflectivity,
                                  std::vector<std::complex<double>>& kzWorkspace);

// Calculates an incoherent Kalpha1/Kalpha2 mixture when the optional second
// wavelength and I2/I1 ratio are present. Buffers are owned by the caller so
// repeated GA evaluations do not allocate.
void calculateSpectrumReflectivity(const std::vector<double>& twoTheta,
                                   double primaryWavelengthAngstrom,
                                   double secondaryWavelengthAngstrom,
                                   double secondaryRatio,
                                   const std::vector<OpticalLayer>& layers,
                                   std::vector<double>& reflectivity,
                                   std::vector<double>& secondaryWorkspace,
                                   std::vector<std::complex<double>>& kzWorkspace);

// Adds phenomenological diffuse attenuation and the detector mean, then scales the full
// expectation so its maximum equals the imported positive maximum.
double composeExpectedIntensity(const Scan& measured,
                                const std::vector<double>& reflectivity,
                                const PoissonModel& poisson,
                                std::vector<double>& expected,
                                std::vector<double>* resolutionWorkspace = nullptr);

// Ideal specular XRR calculated with iterative Parratt recursion and a
// Nevot-Croce interface factor. Output points use exactly the imported point
// coordinates and gap flags; their maximum matches the imported positive max.
Scan simulateXrr(const Scan& measured, const LayerStack& stack, const PoissonModel& poisson = {});

}
