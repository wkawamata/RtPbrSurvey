#include "stdafx.h"

#include "ReflectionHdrDiagnosticStatistics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Engine
{
namespace
{

constexpr double kCoefficientOfVariationMeanEpsilon = 1.0e-6;

double Luminance(const ReflectionHdrDiagnosticSample& sample)
{
    return 0.2126 * sample.r + 0.7152 * sample.g + 0.0722 * sample.b;
}

const std::vector<ReflectionHdrDiagnosticSample>& SelectSamples(const ReflectionHdrDiagnosticFrame& frame,
                                                                ReflectionHdrDiagnosticSignal signal)
{
    switch (signal)
    {
        case ReflectionHdrDiagnosticSignal::EvaluatedRadiance:
            return frame.evaluatedRadiance;
        case ReflectionHdrDiagnosticSignal::SpecularEstimate:
            return frame.specularEstimate;
        case ReflectionHdrDiagnosticSignal::ResolvedRadiance:
        default:
            return frame.resolvedRadiance;
    }
}

double Percentile(const std::vector<double>& sortedValues, double percentile)
{
    if (sortedValues.empty())
    {
        return 0.0;
    }
    const double index = percentile * static_cast<double>(sortedValues.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(index));
    const size_t upper = static_cast<size_t>(std::ceil(index));
    const double fraction = index - static_cast<double>(lower);
    return sortedValues[lower] * (1.0 - fraction) + sortedValues[upper] * fraction;
}

} // namespace

ReflectionHdrDiagnosticStatistics CalculateReflectionHdrDiagnosticStatistics(
    const std::vector<ReflectionHdrDiagnosticFrame>& frames,
    ReflectionHdrDiagnosticSignal signal)
{
    if (frames.empty())
    {
        throw std::invalid_argument("Reflection HDR diagnostic statistics require at least one frame.");
    }

    const size_t pixelCount = SelectSamples(frames.front(), signal).size();
    if (pixelCount == 0)
    {
        throw std::invalid_argument("Reflection HDR diagnostic statistics require at least one pixel.");
    }
    for (const ReflectionHdrDiagnosticFrame& frame : frames)
    {
        if (SelectSamples(frame, signal).size() != pixelCount)
        {
            throw std::invalid_argument("Reflection HDR diagnostic frame dimensions do not match.");
        }
    }

    std::vector<double> pixelMeans(pixelCount, 0.0);
    double maximumLuminance = -std::numeric_limits<double>::infinity();
    for (const ReflectionHdrDiagnosticFrame& frame : frames)
    {
        const std::vector<ReflectionHdrDiagnosticSample>& samples = SelectSamples(frame, signal);
        for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
        {
            const double luminance = Luminance(samples[pixelIndex]);
            pixelMeans[pixelIndex] += luminance;
            maximumLuminance = (std::max)(maximumLuminance, luminance);
        }
    }

    const double frameCount = static_cast<double>(frames.size());
    double temporalMean = 0.0;
    for (double& pixelMean : pixelMeans)
    {
        pixelMean /= frameCount;
        temporalMean += pixelMean;
    }
    temporalMean /= static_cast<double>(pixelCount);

    double temporalVariance = 0.0;
    for (const ReflectionHdrDiagnosticFrame& frame : frames)
    {
        const std::vector<ReflectionHdrDiagnosticSample>& samples = SelectSamples(frame, signal);
        for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
        {
            const double difference = Luminance(samples[pixelIndex]) - pixelMeans[pixelIndex];
            temporalVariance += difference * difference;
        }
    }
    temporalVariance /= frameCount * static_cast<double>(pixelCount);

    std::vector<double> frameAbsoluteDifferences;
    if (frames.size() > 1)
    {
        frameAbsoluteDifferences.reserve((frames.size() - 1) * pixelCount);
        for (size_t frameIndex = 1; frameIndex < frames.size(); ++frameIndex)
        {
            const std::vector<ReflectionHdrDiagnosticSample>& current = SelectSamples(frames[frameIndex], signal);
            const std::vector<ReflectionHdrDiagnosticSample>& previous =
                SelectSamples(frames[frameIndex - 1], signal);
            for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                frameAbsoluteDifferences.push_back(
                    std::abs(Luminance(current[pixelIndex]) - Luminance(previous[pixelIndex])));
            }
        }
    }

    double absoluteDifferenceMean = 0.0;
    for (const double difference : frameAbsoluteDifferences)
    {
        absoluteDifferenceMean += difference;
    }
    if (!frameAbsoluteDifferences.empty())
    {
        absoluteDifferenceMean /= static_cast<double>(frameAbsoluteDifferences.size());
        std::sort(frameAbsoluteDifferences.begin(), frameAbsoluteDifferences.end());
    }

    ReflectionHdrDiagnosticStatistics statistics = {};
    statistics.temporalMeanLuminance = temporalMean;
    statistics.temporalVariance = temporalVariance;
    statistics.temporalStandardDeviation = std::sqrt(temporalVariance);
    statistics.coefficientOfVariationValid = std::abs(temporalMean) > kCoefficientOfVariationMeanEpsilon;
    if (statistics.coefficientOfVariationValid)
    {
        statistics.coefficientOfVariation = statistics.temporalStandardDeviation / std::abs(temporalMean);
    }
    statistics.frameAbsoluteDifferenceMean = absoluteDifferenceMean;
    statistics.frameAbsoluteDifferenceP95 = Percentile(frameAbsoluteDifferences, 0.95);
    statistics.frameAbsoluteDifferenceP99 = Percentile(frameAbsoluteDifferences, 0.99);
    statistics.maximumLuminance = maximumLuminance;
    return statistics;
}

ReflectionHdrDiagnosticBaselineComparison CompareReflectionHdrDiagnosticsToCurrentEstimatorMeanBaseline(
    const std::vector<ReflectionHdrDiagnosticFrame>& frames)
{
    if (frames.empty() || frames.front().evaluatedRadiance.empty())
    {
        throw std::invalid_argument("Current-estimator mean baseline requires evaluated radiance samples.");
    }

    const size_t pixelCount = frames.front().evaluatedRadiance.size();
    std::vector<double> baseline(pixelCount, 0.0);
    for (const ReflectionHdrDiagnosticFrame& frame : frames)
    {
        if (frame.evaluatedRadiance.size() != pixelCount || frame.resolvedRadiance.size() != pixelCount)
        {
            throw std::invalid_argument("Reflection HDR diagnostic frame dimensions do not match.");
        }
        for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
        {
            baseline[pixelIndex] += Luminance(frame.evaluatedRadiance[pixelIndex]);
        }
    }

    double baselineMean = 0.0;
    for (double& baselineSample : baseline)
    {
        baselineSample /= static_cast<double>(frames.size());
        baselineMean += baselineSample;
    }
    baselineMean /= static_cast<double>(pixelCount);

    ReflectionHdrDiagnosticBaselineComparison comparison = {};
    comparison.baselineMeanLuminance = baselineMean;
    comparison.evaluatedRmseByFrame.reserve(frames.size());
    comparison.resolvedRmseByFrame.reserve(frames.size());
    double evaluatedSquaredError = 0.0;
    double resolvedSquaredError = 0.0;
    for (const ReflectionHdrDiagnosticFrame& frame : frames)
    {
        double evaluatedFrameSquaredError = 0.0;
        double resolvedFrameSquaredError = 0.0;
        for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
        {
            const double evaluatedDifference =
                Luminance(frame.evaluatedRadiance[pixelIndex]) - baseline[pixelIndex];
            const double resolvedDifference =
                Luminance(frame.resolvedRadiance[pixelIndex]) - baseline[pixelIndex];
            evaluatedFrameSquaredError += evaluatedDifference * evaluatedDifference;
            resolvedFrameSquaredError += resolvedDifference * resolvedDifference;
        }
        evaluatedSquaredError += evaluatedFrameSquaredError;
        resolvedSquaredError += resolvedFrameSquaredError;
        comparison.evaluatedRmseByFrame.push_back(
            std::sqrt(evaluatedFrameSquaredError / static_cast<double>(pixelCount)));
        comparison.resolvedRmseByFrame.push_back(
            std::sqrt(resolvedFrameSquaredError / static_cast<double>(pixelCount)));
    }

    const double sampleCount = static_cast<double>(frames.size()) * static_cast<double>(pixelCount);
    comparison.evaluatedRmse = std::sqrt(evaluatedSquaredError / sampleCount);
    comparison.resolvedRmse = std::sqrt(resolvedSquaredError / sampleCount);
    return comparison;
}

} // namespace Engine
