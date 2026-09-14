#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <optional>
#include <string>

namespace xrr {

struct MaterialInsertChoice {
    std::string material;
    std::string density;
    bool fromPreset = false;
};

// Opens the material catalog used by Insert Above / Insert Below. The first,
// default row is always an empty custom layer. std::nullopt means Cancel.
std::optional<MaterialInsertChoice> chooseMaterialForInsertion(HWND owner,
                                                               HINSTANCE instance,
                                                               HFONT font);

}
