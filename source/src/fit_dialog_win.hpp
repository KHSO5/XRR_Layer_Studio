#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "fit.hpp"
#include <optional>
#include <vector>

namespace xrr {

struct FitDialogSelection {
    std::vector<FitVariable> variables;
    std::optional<double> twoThetaMinimum;
    std::optional<double> twoThetaMaximum;
    FitObjective objective = FitObjective::RobustPoisson;
};

std::optional<FitDialogSelection> chooseFitVariables(HWND owner,
                                                     HINSTANCE instance,
                                                     HFONT font,
                                                     const Scan& measured,
                                                     const LayerStack& stack);

FitResult runGeneticFitDialog(HWND owner,
                              HINSTANCE instance,
                              HFONT font,
                              const Scan& measured,
                              const LayerStack& stack,
                              const FitDialogSelection& selection,
                              const PoissonModel& initialPoisson);

bool confirmFitResult(HWND owner,const LayerStack& initial,const FitResult& result);

}
