#pragma once

#include "ReflectionHdrDiagnosticCapture.h"

#include <vector>

namespace Engine
{

enum class ReflectionHdrDiagnosticSignal
{
    EvaluatedRadiance,
    ResolvedRadiance,
};

struct ReflectionHdrDiagnosticStatistics
{
    double temporalMeanLuminance = 0.0;
    double temporalVariance = 0.0;
    double temporalStandardDeviation = 0.0;
    double coefficientOfVariation = 0.0;
    bool coefficientOfVariationValid = false;
    double frameAbsoluteDifferenceMean = 0.0;
    double frameAbsoluteDifferenceP95 = 0.0;
    double frameAbsoluteDifferenceP99 = 0.0;
    double maximumLuminance = 0.0;
};

ReflectionHdrDiagnosticStatistics CalculateReflectionHdrDiagnosticStatistics(
    const std::vector<ReflectionHdrDiagnosticFrame>& frames,
    ReflectionHdrDiagnosticSignal signal);

} // namespace Engine
