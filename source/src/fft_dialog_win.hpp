#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "bruker_reader.hpp"
#include <windows.h>

namespace xrr {

// Opens a modal range setup followed by an FFT thickness-spectrum window.
// Returns false when the setup is cancelled.
bool showThicknessFftDialog(HWND owner,HINSTANCE instance,HFONT font,const Scan& scan);

}
