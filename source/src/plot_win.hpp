#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "project.hpp"

namespace xrr {
struct PlotBox { double left,top,width,height; };
struct PlotState {
    const Scan* scan=nullptr;
    const Scan* simulated=nullptr;
    View view;
    std::ptrdiff_t hovered=-1;
    POINT mouse{};
    std::ptrdiff_t selectedPoint1=-1;
    std::ptrdiff_t selectedPoint2=-1;
    bool zoomSelecting=false;
    POINT zoomStart{};
    POINT zoomEnd{};
};
std::wstring wide(const std::string& utf8);
std::string utf8(const std::wstring& text);
PlotBox plotBox(int width,int height,double scale);
void paintPlot(Gdiplus::Graphics& g,int width,int height,double scale,const PlotState& state,bool cursor=true);
void savePlotPng(const std::filesystem::path& file,const PlotState& state);
void savePlotSvg(const std::filesystem::path& file,const PlotState& state);
void saveWindowPng(HWND window,const std::filesystem::path& path);
}
