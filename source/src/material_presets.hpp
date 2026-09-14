#pragma once

#include <optional>
#include <string>
#include <vector>

namespace xrr {

enum class MaterialCategory {
    Element,
    Oxide,
    NitrideCarbide,
    SemiconductorCompound,
    AlloyMagnetic
};

struct MaterialPreset {
    // material is written into the editable Material / note cell. Its leading
    // token is always a formula accepted by the XRR electron-density model.
    std::string material;
    std::string name;
    double densityGcm3 = 0;
    MaterialCategory category = MaterialCategory::Element;
    std::string elements; // Space-separated element symbols for exact filtering.
    std::string note;
};

const std::vector<MaterialPreset>& materialPresets();
const char* materialCategoryLabel(MaterialCategory category);

// A one-element symbol/name query (for example Co, cobalt, or silicon) is
// treated as an exact composition filter. General text searches material,
// common name, category, element list, and notes.
std::vector<const MaterialPreset*> filterMaterialPresets(
    const std::string& query,
    std::optional<MaterialCategory> category = std::nullopt);

std::string formatPresetDensity(double densityGcm3);

}
