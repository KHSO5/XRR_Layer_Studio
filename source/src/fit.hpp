#pragma once
#include "project.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xrr {

enum class FitParameter { Density, Thickness, Roughness };
enum class FitObjective { RobustPoisson, PurePoisson };

struct FitVariable {
    std::size_t row = 0;
    FitParameter parameter = FitParameter::Density;
    double initial = 0;
    double lower = 0;
    double upper = 0;
    double fitted = 0;
    bool selected = false;
};

std::vector<FitVariable> availableFitVariables(const LayerStack& stack);
std::string fitVariableName(const LayerStack& stack, const FitVariable& variable);

struct FitOptions {
    std::size_t populationSize = 0; // Zero selects an efficient size from dimensionality.
    std::size_t generations = 0;    // Zero selects an efficient size from dimensionality.
    std::size_t maxSamplePoints = 1500;
    unsigned workerThreads = 0;     // Zero uses available logical CPU cores.
    std::uint64_t randomSeed = 0x5852524741323230ULL;
    std::optional<double> twoThetaMinimum; // Blank in the UI means unbounded.
    std::optional<double> twoThetaMaximum;
    FitObjective objective = FitObjective::RobustPoisson;
};

struct FitResult {
    bool cancelled = false;
    LayerStack fittedLayers;
    PoissonModel poisson;
    Scan fittedCurve;
    std::vector<FitVariable> variables;
    double initialPoissonDeviance = 0;
    double finalPoissonDeviance = 0;
    double initialObjective = 0;
    double finalObjective = 0;
    FitObjective objective = FitObjective::RobustPoisson;
    double highAngleStart = 0;
    double initialHighAngleLogRmse = 0;
    double finalHighAngleLogRmse = 0;
    double elapsedMilliseconds = 0;
    std::size_t evaluations = 0;
    std::size_t generationsCompleted = 0;
    std::size_t sampledPoints = 0;
    std::size_t pointsInFitRange = 0;
    unsigned workerThreads = 1;
    double fitTwoThetaMinimum = 0;
    double fitTwoThetaMaximum = 0;
    double diffuseStrength = 0;
    double detectorMeanCounts = 0;
};

using FitProgress = std::function<void(double)>;
using FitCancelled = std::function<bool()>;

// Real-coded, bounded, multi-threaded genetic algorithm. Structural variables
// are user-selected; diffuse attenuation, detector background and instrumental
// resolution are nuisance parameters. The default robust objective is a Cauchy
// loss on Poisson deviance residuals; pure Poisson remains selectable.
FitResult fitXrrGenetic(const Scan& measured,
                        const LayerStack& initialStack,
                        const std::vector<FitVariable>& variables,
                        const PoissonModel& initialPoisson = {},
                        const FitOptions& options = {},
                        FitProgress progress = {},
                        FitCancelled cancelled = {});

}
