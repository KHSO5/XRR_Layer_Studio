#pragma once
#include "bruker_reader.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace xrr {
struct Layer {
    bool substrate = false;
    std::string material;
    std::string density;   // g/cm^3; blank is permitted.
    std::string thickness; // Expected thickness in nm; blank is permitted.
    std::string roughness; // RMS width of this layer's TOP interface, nm.
    bool fitDensity = false;
    bool fitThickness = false;
    bool fitRoughness = false;
};
struct PoissonModel {
    bool enabled = false;
    double diffuseStrength = 0;  // Dimensionless loss from specular into diffuse scattering.
    double diffuseExponent = 1;  // Attenuation follows exp[-strength*(qz/qz_min-1)^exponent].
    double detectorFraction = 0; // Constant detector mean relative to unit reflectivity.
    double resolutionFwhmDegrees = 0; // Gaussian-equivalent instrumental broadening in 2theta.
};
struct View {
    double xmin=0, xmax=10, ymin=0, ymax=8; // y bounds are log10(Intensity).
};
class LayerStack {
public:
    std::vector<Layer> rows{Layer{true,{},{},{},{}}};
    std::size_t insertBefore(std::size_t selected);
    std::size_t insertAfter(std::size_t selected);
    bool erase(std::size_t index);
    bool move(std::size_t index, int direction);
    void validateStructure() const;
    std::string cellError(std::size_t row, int column) const;
    std::string validationError() const;
};
struct Project {
    LayerStack layers;
    std::vector<Scan> scans;
    std::size_t activeScan=0;
    View view;
    PoissonModel poisson;
};
View autoView(const Scan* scan, bool nominal=false);
void saveProject(const std::filesystem::path& path, const Project& project);
Project loadProject(const std::filesystem::path& path);
void exportLayersCsv(const std::filesystem::path& path, const LayerStack& layers);
// Plot data are exported as exactly three columns: 2theta, measured intensity,
// and simulated/fitted intensity. The third column is blank when model is null.
void exportDataCsv(const std::filesystem::path& path, const Scan& scan, const Scan* model=nullptr);
std::string xmlEscape(const std::string& text);
}
