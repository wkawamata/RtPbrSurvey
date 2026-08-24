#pragma once

#include "ReflectionHdrDiagnosticCapture.h"

#include <vector>

namespace Engine
{

enum class ReflectionHdrDiagnosticSignal
{
    EvaluatedRadiance,
    SpecularEstimate,
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

struct ReflectionHdrDiagnosticBaselineComparison
{
    double baselineMeanLuminance = 0.0;
    double evaluatedRmse = 0.0;
    double resolvedRmse = 0.0;
    std::vector<double> evaluatedRmseByFrame;
    std::vector<double> resolvedRmseByFrame;
};

ReflectionHdrDiagnosticStatistics CalculateReflectionHdrDiagnosticStatistics(
    const std::vector<ReflectionHdrDiagnosticFrame>& frames,
    ReflectionHdrDiagnosticSignal signal);
ReflectionHdrDiagnosticBaselineComparison CompareReflectionHdrDiagnosticsToCurrentEstimatorMeanBaseline(
    const std::vector<ReflectionHdrDiagnosticFrame>& frames);

} // namespace Engine
