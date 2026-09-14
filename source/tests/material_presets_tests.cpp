#include "material_presets.hpp"
#include "simulation.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

bool containsElement(const xrr::MaterialPreset& preset,const std::string& symbol) {
    std::istringstream input(preset.elements);std::string token;
    while(input>>token)if(token==symbol)return true;
    return false;
}

int main() {
    try {
        const auto& presets=xrr::materialPresets();
        check(presets.size()>=100,"The preset catalog is unexpectedly small");
        std::set<std::string> identities;
        std::array<std::size_t,5> categories{};
        for(const auto& preset:presets) {
            check(!preset.material.empty()&&!preset.name.empty()&&!preset.elements.empty(),"A preset has missing metadata");
            check(std::isfinite(preset.densityGcm3)&&preset.densityGcm3>0,"A preset has an invalid density");
            check(xrr::formatPresetDensity(preset.densityGcm3).size()>=5,"Preset density lost three-decimal display precision");
            check(identities.insert(preset.material).second,"Duplicate preset material label");
            const auto category=static_cast<std::size_t>(preset.category);
            check(category<categories.size(),"Unknown material category");++categories[category];
            const double electronDensity=xrr::electronDensityAngstrom3(preset.material,preset.densityGcm3);
            check(std::isfinite(electronDensity)&&electronDensity>0,"A preset formula is not accepted by the XRR model");
        }
        for(const auto count:categories)check(count>0,"An empty preset category was published");

        const auto cobalt=xrr::filterMaterialPresets("Co");
        check(cobalt.size()>=10,"Cobalt element search returned too few useful materials");
        for(const auto* preset:cobalt)check(containsElement(*preset,"Co"),"Exact Co filter returned a false match");
        const auto silicon=xrr::filterMaterialPresets("silicon");
        check(silicon.size()>=7,"Element-name search did not find silicon compounds");
        for(const auto* preset:silicon)check(containsElement(*preset,"Si"),"Element-name filter returned a false match");
        const auto oxides=xrr::filterMaterialPresets("oxygen",xrr::MaterialCategory::Oxide);
        check(oxides.size()>=25,"Oxide category and oxygen filter did not combine correctly");
        for(const auto* preset:oxides)
            check(preset->category==xrr::MaterialCategory::Oxide&&containsElement(*preset,"O"),"Combined category/element filter failed");
        const auto permalloy=xrr::filterMaterialPresets("Permalloy");
        check(permalloy.size()==1&&permalloy.front()->material.find("Ni80Fe20")==0,"General text search failed");
        check(xrr::filterMaterialPresets("element-that-does-not-exist").empty(),"Nonsense query returned presets");

        std::cout<<"Material preset tests passed: "<<presets.size()<<" entries; "<<cobalt.size()
                 <<" contain Co; "<<silicon.size()<<" contain Si\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
