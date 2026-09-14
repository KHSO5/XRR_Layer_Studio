#include "material_presets.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <locale>
#include <sstream>
#include <string_view>

namespace xrr {
namespace {

using Category = MaterialCategory;

std::string lowerAscii(std::string text) {
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string trim(std::string text) {
    const auto first=std::find_if_not(text.begin(),text.end(),[](unsigned char c){return std::isspace(c)!=0;});
    const auto last=std::find_if_not(text.rbegin(),text.rend(),[](unsigned char c){return std::isspace(c)!=0;}).base();
    return first<last?std::string(first,last):std::string{};
}

bool hasElement(const MaterialPreset& preset,std::string_view symbol) {
    std::istringstream input(preset.elements);std::string token;
    while(input>>token)if(token==symbol)return true;
    return false;
}

struct ElementAlias { const char* alias; const char* symbol; };
constexpr ElementAlias aliases[]{
    {"h","H"},{"hydrogen","H"},{"b","B"},{"boron","B"},{"c","C"},{"carbon","C"},
    {"n","N"},{"nitrogen","N"},{"o","O"},{"oxygen","O"},{"mg","Mg"},{"magnesium","Mg"},
    {"al","Al"},{"aluminium","Al"},{"aluminum","Al"},{"si","Si"},{"silicon","Si"},
    {"p","P"},{"phosphorus","P"},{"s","S"},{"sulfur","S"},{"se","Se"},{"selenium","Se"},
    {"ti","Ti"},{"titanium","Ti"},{"v","V"},{"vanadium","V"},{"cr","Cr"},{"chromium","Cr"},
    {"mn","Mn"},{"manganese","Mn"},{"fe","Fe"},{"iron","Fe"},{"co","Co"},{"cobalt","Co"},
    {"ni","Ni"},{"nickel","Ni"},{"cu","Cu"},{"copper","Cu"},{"zn","Zn"},{"zinc","Zn"},
    {"ga","Ga"},{"gallium","Ga"},{"ge","Ge"},{"germanium","Ge"},{"as","As"},{"arsenic","As"},
    {"y","Y"},{"yttrium","Y"},{"zr","Zr"},{"zirconium","Zr"},{"nb","Nb"},{"niobium","Nb"},
    {"mo","Mo"},{"molybdenum","Mo"},{"ru","Ru"},{"ruthenium","Ru"},{"rh","Rh"},{"rhodium","Rh"},
    {"pd","Pd"},{"palladium","Pd"},{"ag","Ag"},{"silver","Ag"},{"cd","Cd"},{"cadmium","Cd"},
    {"in","In"},{"indium","In"},{"sn","Sn"},{"tin","Sn"},{"sb","Sb"},{"antimony","Sb"},
    {"te","Te"},{"tellurium","Te"},{"la","La"},{"lanthanum","La"},{"ce","Ce"},{"cerium","Ce"},
    {"nd","Nd"},{"neodymium","Nd"},{"sm","Sm"},{"samarium","Sm"},{"gd","Gd"},{"gadolinium","Gd"},
    {"tb","Tb"},{"terbium","Tb"},{"dy","Dy"},{"dysprosium","Dy"},{"hf","Hf"},{"hafnium","Hf"},
    {"ta","Ta"},{"tantalum","Ta"},{"w","W"},{"tungsten","W"},{"re","Re"},{"rhenium","Re"},
    {"os","Os"},{"osmium","Os"},{"ir","Ir"},{"iridium","Ir"},{"pt","Pt"},{"platinum","Pt"},
    {"au","Au"},{"gold","Au"},{"pb","Pb"},{"lead","Pb"},{"bi","Bi"},{"bismuth","Bi"},
    {"sr","Sr"},{"strontium","Sr"},{"ba","Ba"},{"barium","Ba"}
};

std::optional<std::string_view> elementSymbol(const std::string& query) {
    const auto lowered=lowerAscii(trim(query));
    const auto found=std::find_if(std::begin(aliases),std::end(aliases),[&](const ElementAlias& value){return lowered==value.alias;});
    if(found==std::end(aliases))return std::nullopt;
    return found->symbol;
}

const std::vector<MaterialPreset> presets{
    // Stable/common elemental thin-film materials. Densities are nominal bulk
    // values near room temperature and remain editable after insertion.
    {"B","Boron",2.340,Category::Element,"B","crystalline reference"},
    {"C (graphite)","Carbon, graphite",2.267,Category::Element,"C","phase-dependent"},
    {"C (diamond)","Carbon, diamond",3.515,Category::Element,"C","phase-dependent"},
    {"Mg","Magnesium",1.738,Category::Element,"Mg","metal"},
    {"Al","Aluminium",2.700,Category::Element,"Al","metal"},
    {"Si","Silicon",2.329,Category::Element,"Si","semiconductor"},
    {"P (red)","Phosphorus, red",2.340,Category::Element,"P","allotrope-dependent"},
    {"S (orthorhombic)","Sulfur",2.067,Category::Element,"S","allotrope-dependent"},
    {"Ti","Titanium",4.506,Category::Element,"Ti","metal"},
    {"V","Vanadium",6.110,Category::Element,"V","metal"},
    {"Cr","Chromium",7.190,Category::Element,"Cr","metal"},
    {"Mn","Manganese",7.210,Category::Element,"Mn","metal"},
    {"Fe","Iron",7.874,Category::Element,"Fe","ferromagnetic metal"},
    {"Co","Cobalt",8.900,Category::Element,"Co","ferromagnetic metal"},
    {"Ni","Nickel",8.908,Category::Element,"Ni","ferromagnetic metal"},
    {"Cu","Copper",8.960,Category::Element,"Cu","metal"},
    {"Zn","Zinc",7.134,Category::Element,"Zn","metal"},
    {"Ga","Gallium",5.910,Category::Element,"Ga","near-melting-point reference"},
    {"Ge","Germanium",5.323,Category::Element,"Ge","semiconductor"},
    {"Se","Selenium",4.809,Category::Element,"Se","allotrope-dependent"},
    {"Zr","Zirconium",6.520,Category::Element,"Zr","metal"},
    {"Nb","Niobium",8.570,Category::Element,"Nb","superconducting metal"},
    {"Mo","Molybdenum",10.280,Category::Element,"Mo","metal"},
    {"Ru","Ruthenium",12.370,Category::Element,"Ru","metal"},
    {"Rh","Rhodium",12.410,Category::Element,"Rh","metal"},
    {"Pd","Palladium",12.023,Category::Element,"Pd","metal"},
    {"Ag","Silver",10.490,Category::Element,"Ag","metal"},
    {"Cd","Cadmium",8.650,Category::Element,"Cd","metal"},
    {"In","Indium",7.310,Category::Element,"In","metal"},
    {"Sn","Tin",7.310,Category::Element,"Sn","white tin"},
    {"Sb","Antimony",6.697,Category::Element,"Sb","semimetal"},
    {"Te","Tellurium",6.240,Category::Element,"Te","semimetal"},
    {"Y","Yttrium",4.472,Category::Element,"Y","rare-earth metal"},
    {"Nd","Neodymium",7.010,Category::Element,"Nd","magnetic rare-earth metal"},
    {"Sm","Samarium",7.520,Category::Element,"Sm","magnetic rare-earth metal"},
    {"Gd","Gadolinium",7.900,Category::Element,"Gd","magnetic rare-earth metal"},
    {"Tb","Terbium",8.230,Category::Element,"Tb","magnetic rare-earth metal"},
    {"Dy","Dysprosium",8.540,Category::Element,"Dy","magnetic rare-earth metal"},
    {"Hf","Hafnium",13.310,Category::Element,"Hf","metal"},
    {"Ta","Tantalum",16.690,Category::Element,"Ta","metal"},
    {"W","Tungsten",19.250,Category::Element,"W","metal"},
    {"Re","Rhenium",21.020,Category::Element,"Re","metal"},
    {"Os","Osmium",22.590,Category::Element,"Os","metal"},
    {"Ir","Iridium",22.560,Category::Element,"Ir","metal"},
    {"Pt","Platinum",21.450,Category::Element,"Pt","metal"},
    {"Au","Gold",19.320,Category::Element,"Au","metal"},
    {"Pb","Lead",11.340,Category::Element,"Pb","metal"},
    {"Bi","Bismuth",9.780,Category::Element,"Bi","semimetal"},

    // Oxides frequently encountered as substrates, dielectrics, electrodes,
    // tunnel barriers, magnetic layers, or native surface layers.
    {"SiO2 (fused silica)","Silicon dioxide, fused",2.200,Category::Oxide,"Si O","amorphous/fused reference"},
    {"SiO2 (quartz)","Silicon dioxide, quartz",2.650,Category::Oxide,"Si O","crystalline reference"},
    {"Al2O3","Aluminium oxide",3.970,Category::Oxide,"Al O","corundum reference"},
    {"MgO","Magnesium oxide",3.580,Category::Oxide,"Mg O","rock-salt reference"},
    {"TiO2 (rutile)","Titanium dioxide, rutile",4.230,Category::Oxide,"Ti O","phase-dependent"},
    {"TiO2 (anatase)","Titanium dioxide, anatase",3.900,Category::Oxide,"Ti O","phase-dependent"},
    {"HfO2","Hafnium oxide",9.680,Category::Oxide,"Hf O","high-k dielectric; phase-dependent"},
    {"ZrO2","Zirconium oxide",5.680,Category::Oxide,"Zr O","phase-dependent"},
    {"Ta2O5","Tantalum oxide",8.200,Category::Oxide,"Ta O","phase/stoichiometry-dependent"},
    {"Nb2O5","Niobium oxide",4.600,Category::Oxide,"Nb O","phase-dependent"},
    {"WO3","Tungsten oxide",7.160,Category::Oxide,"W O","phase/oxygen-dependent"},
    {"MoO3","Molybdenum oxide",4.690,Category::Oxide,"Mo O","phase-dependent"},
    {"V2O5","Vanadium oxide",3.360,Category::Oxide,"V O","phase-dependent"},
    {"Cr2O3","Chromium oxide",5.220,Category::Oxide,"Cr O","antiferromagnetic oxide"},
    {"Fe2O3","Iron oxide, hematite",5.240,Category::Oxide,"Fe O","phase-dependent"},
    {"Fe3O4","Iron oxide, magnetite",5.170,Category::Oxide,"Fe O","ferrimagnetic oxide"},
    {"CoO","Cobalt monoxide",6.440,Category::Oxide,"Co O","antiferromagnetic oxide"},
    {"Co3O4","Cobalt oxide",6.110,Category::Oxide,"Co O","spinel reference"},
    {"NiO","Nickel oxide",6.670,Category::Oxide,"Ni O","antiferromagnetic oxide"},
    {"Cu2O","Copper(I) oxide",6.000,Category::Oxide,"Cu O","phase-dependent"},
    {"CuO","Copper(II) oxide",6.310,Category::Oxide,"Cu O","phase-dependent"},
    {"ZnO","Zinc oxide",5.610,Category::Oxide,"Zn O","semiconducting oxide"},
    {"RuO2","Ruthenium oxide",6.970,Category::Oxide,"Ru O","conducting oxide"},
    {"In2O3","Indium oxide",7.180,Category::Oxide,"In O","conducting oxide"},
    {"SnO2","Tin oxide",6.950,Category::Oxide,"Sn O","conducting oxide"},
    {"Y2O3","Yttrium oxide",5.010,Category::Oxide,"Y O","dielectric"},
    {"La2O3","Lanthanum oxide",6.510,Category::Oxide,"La O","hygroscopic; phase-dependent"},
    {"CeO2","Cerium oxide",7.220,Category::Oxide,"Ce O","oxygen-content-dependent"},
    {"SrTiO3","Strontium titanate",5.120,Category::Oxide,"Sr Ti O","perovskite substrate"},
    {"BaTiO3","Barium titanate",6.020,Category::Oxide,"Ba Ti O","ferroelectric perovskite"},
    {"MgAl2O4","Magnesium aluminate spinel",3.580,Category::Oxide,"Mg Al O","spinel substrate"},
    {"NiFe2O4","Nickel ferrite",5.380,Category::Oxide,"Ni Fe O","ferrimagnetic spinel"},
    {"CoFe2O4","Cobalt ferrite",5.300,Category::Oxide,"Co Fe O","ferrimagnetic spinel"},
    {"Y3Fe5O12","Yttrium iron garnet (YIG)",5.170,Category::Oxide,"Y Fe O","magnetic insulator"},
    {"In1.8Sn0.2O3 (ITO)","Indium tin oxide, nominal",7.140,Category::Oxide,"In Sn O","composition/porosity-dependent"},

    // Hard coatings, diffusion barriers, and electronic compounds.
    {"BN (hexagonal)","Boron nitride, h-BN",2.100,Category::NitrideCarbide,"B N","phase-dependent"},
    {"Si3N4","Silicon nitride",3.170,Category::NitrideCarbide,"Si N","phase/porosity-dependent"},
    {"AlN","Aluminium nitride",3.260,Category::NitrideCarbide,"Al N","wurtzite reference"},
    {"GaN","Gallium nitride",6.150,Category::NitrideCarbide,"Ga N","wurtzite reference"},
    {"InN","Indium nitride",6.810,Category::NitrideCarbide,"In N","wurtzite reference"},
    {"TiN","Titanium nitride",5.220,Category::NitrideCarbide,"Ti N","stoichiometry-dependent"},
    {"TaN","Tantalum nitride",14.300,Category::NitrideCarbide,"Ta N","phase/stoichiometry-dependent"},
    {"SiC","Silicon carbide",3.210,Category::NitrideCarbide,"Si C","polytype-dependent"},
    {"TiC","Titanium carbide",4.930,Category::NitrideCarbide,"Ti C","stoichiometry-dependent"},
    {"WC","Tungsten carbide",15.630,Category::NitrideCarbide,"W C","hexagonal reference"},

    // Semiconductor, optoelectronic, and layered functional compounds.
    {"GaAs","Gallium arsenide",5.318,Category::SemiconductorCompound,"Ga As","III-V semiconductor"},
    {"InP","Indium phosphide",4.810,Category::SemiconductorCompound,"In P","III-V semiconductor"},
    {"GaP","Gallium phosphide",4.138,Category::SemiconductorCompound,"Ga P","III-V semiconductor"},
    {"In0.53Ga0.47As","InGaAs, lattice-matched nominal",5.670,Category::SemiconductorCompound,"In Ga As","composition-dependent"},
    {"Al0.3Ga0.7As","AlGaAs, nominal",4.850,Category::SemiconductorCompound,"Al Ga As","composition-dependent"},
    {"Si0.8Ge0.2","Silicon germanium, nominal",2.750,Category::SemiconductorCompound,"Si Ge","composition-dependent"},
    {"CdTe","Cadmium telluride",5.850,Category::SemiconductorCompound,"Cd Te","II-VI semiconductor"},
    {"MoS2","Molybdenum disulfide",5.060,Category::SemiconductorCompound,"Mo S","layered semiconductor"},
    {"WS2","Tungsten disulfide",7.500,Category::SemiconductorCompound,"W S","layered semiconductor"},
    {"Ge2Sb2Te5 (GST)","GST phase-change material",6.100,Category::SemiconductorCompound,"Ge Sb Te","phase/composition-dependent"},

    // Representative compositions used in magnetic, spintronic, interconnect,
    // and shape-memory stacks. These values are especially composition-dependent.
    {"Ni80Fe20 (Permalloy)","Permalloy",8.700,Category::AlloyMagnetic,"Ni Fe","nominal composition-dependent density"},
    {"Co40Fe40B20","Cobalt iron boron",7.800,Category::AlloyMagnetic,"Co Fe B","amorphous; nominal density"},
    {"Co90Fe10","Cobalt iron",8.800,Category::AlloyMagnetic,"Co Fe","nominal density"},
    {"Fe50Co50","Iron cobalt",8.350,Category::AlloyMagnetic,"Fe Co","nominal density"},
    {"Co80Tb20","Cobalt terbium",8.397,Category::AlloyMagnetic,"Co Tb","nominal density; composition-dependent"},
    {"Fe50Pt50","Iron platinum, L1_0 nominal",14.600,Category::AlloyMagnetic,"Fe Pt","order/composition-dependent"},
    {"Co50Pt50","Cobalt platinum, L1_0 nominal",15.000,Category::AlloyMagnetic,"Co Pt","order/composition-dependent"},
    {"Ir20Mn80","Iridium manganese",10.500,Category::AlloyMagnetic,"Ir Mn","antiferromagnet; nominal density"},
    {"Pt50Mn50","Platinum manganese",14.000,Category::AlloyMagnetic,"Pt Mn","antiferromagnet; nominal density"},
    {"Fe50Mn50","Iron manganese",7.600,Category::AlloyMagnetic,"Fe Mn","antiferromagnet; nominal density"},
    {"Cu50Ni50","Copper nickel",8.900,Category::AlloyMagnetic,"Cu Ni","nominal composition"},
    {"Ni2MnGa","Nickel manganese gallium",8.100,Category::AlloyMagnetic,"Ni Mn Ga","Heusler; phase-dependent"},
    {"Co2MnSi","Cobalt manganese silicon",7.300,Category::AlloyMagnetic,"Co Mn Si","Heusler; phase-dependent"},
    {"Co2FeAl","Cobalt iron aluminium",7.200,Category::AlloyMagnetic,"Co Fe Al","Heusler; phase-dependent"},
    {"Al98Cu2","Aluminium copper, nominal",2.760,Category::AlloyMagnetic,"Al Cu","interconnect alloy; nominal density"}
};

}

const std::vector<MaterialPreset>& materialPresets(){return presets;}

const char* materialCategoryLabel(MaterialCategory category) {
    switch(category) {
    case MaterialCategory::Element:return "Elements";
    case MaterialCategory::Oxide:return "Oxides";
    case MaterialCategory::NitrideCarbide:return "Nitrides / carbides";
    case MaterialCategory::SemiconductorCompound:return "Semiconductor / functional compounds";
    case MaterialCategory::AlloyMagnetic:return "Alloys / magnetic materials";
    }
    return "Materials";
}

std::vector<const MaterialPreset*> filterMaterialPresets(const std::string& query,
                                                         std::optional<MaterialCategory> category) {
    const auto cleaned=trim(query);
    const auto lowered=lowerAscii(cleaned);
    const auto symbol=elementSymbol(cleaned);
    std::vector<const MaterialPreset*> result;result.reserve(presets.size());
    for(const auto& preset:presets) {
        if(category&&preset.category!=*category)continue;
        bool match=cleaned.empty();
        if(!match&&symbol)match=hasElement(preset,*symbol);
        if(!match&&!symbol) {
            std::string haystack=preset.material+' '+preset.name+' '+preset.elements+' '+preset.note+' '+materialCategoryLabel(preset.category);
            match=lowerAscii(std::move(haystack)).find(lowered)!=std::string::npos;
        }
        if(match)result.push_back(&preset);
    }
    return result;
}

std::string formatPresetDensity(double densityGcm3) {
    if(!std::isfinite(densityGcm3)||densityGcm3<=0)return {};
    std::ostringstream output;output.imbue(std::locale::classic());output<<std::fixed<<std::setprecision(3)<<densityGcm3;
    return output.str();
}

}
