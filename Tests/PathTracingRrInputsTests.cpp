#include "Renderer/PathTracingRrInputs.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <regex>
#include <set>

namespace
{
using Signal = Engine::PathTracingInputSignal;
using Issue = Engine::PathTracingRrInputIssue;

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Engine::PathTracingReconstructionInputs Inputs()
{
    Engine::PathTracingReconstructionInputs inputs;
    auto& frame = inputs.frame;
    frame.renderWidth = frame.renderHeight = frame.outputWidth = frame.outputHeight = 16;
    frame.frameId = 42;
    frame.sampleStartIndex = 30;
    frame.pixelCenterPrimary = true;
    frame.samplingSignature = 7;
    frame.worldToView = frame.viewToClip =
        frame.previousViewProjection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (Signal signal : Engine::kPathTracingRrSignals)
    {
        DXGI_FORMAT format = signal == Signal::ViewZ           ? DXGI_FORMAT_R32_FLOAT
                             : signal == Signal::MotionVectors ? DXGI_FORMAT_R32G32_FLOAT
                                                               : DXGI_FORMAT_R32G32B32A32_FLOAT;
        inputs.signals[static_cast<size_t>(signal)] = {true, format, 16, 16, 42, 30, 1, 7};
    }
    return inputs;
}

Engine::PathTracingRrPixelSummary Pixels()
{
    return {true, 42, 7, 30, 16, 16, {256, 0, 0, 0, 0, 0}};
}

void Bindings()
{
    std::ifstream stream(RTPBRSURVEY_RR_INPUT_SHADER_PATH);
    Require(bool(stream), "Missing texture conversion source");
    const std::string shader{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const std::map<std::string, Signal> meanings = {{"g_frameRadiance", Signal::FrameRadiance},
                                                    {"g_normalRoughness", Signal::NormalRoughness},
                                                    {"g_viewZ", Signal::ViewZ},
                                                    {"g_motionNdc", Signal::MotionVectors},
                                                    {"g_baseColor", Signal::BaseColor},
                                                    {"g_f0Metallic", Signal::MaterialF0Metallic},
                                                    {"g_primaryPosition", Signal::PrimaryPosition},
                                                    {"g_specularDistance", Signal::SpecularDistance},
                                                    {"g_secondaryRayData", Signal::SecondaryRayData},
                                                    {"g_secondarySurfaceDistance", Signal::SecondarySurfaceDistance}};
    const std::regex declaration(R"(Texture2D<[^>]+>\s+(\w+)\s*:\s*register\(t(\d+)\))");
    std::set<size_t> slots;
    for (auto entry = std::sregex_iterator(shader.begin(), shader.end(), declaration); entry != std::sregex_iterator();
         ++entry)
    {
        const size_t slot = std::stoul((*entry)[2].str());
        const auto meaning = meanings.find((*entry)[1].str());
        Require(slot < Engine::kPathTracingRrLiveSignals.size() && meaning != meanings.end() &&
                    slots.insert(slot).second,
                "Unexpected texture input slot");
        Require(Engine::kPathTracingRrLiveSignals[slot] == meaning->second, "Wrong common input semantic");
    }
    Require(slots.size() == Engine::kPathTracingRrLiveSignals.size(), "Incomplete texture input inventory");
}
} // namespace

int main()
{
    try
    {
        Bindings();
        Require(Engine::InspectPathTracingRrFrame(Inputs(), Pixels()).Passed(), "Complete numeric frame rejected");
        for (Signal signal : Engine::kPathTracingRrSignals)
        {
            auto inputs = Inputs();
            inputs.signals[static_cast<size_t>(signal)].present = false;
            const auto result = Engine::InspectPathTracingRrMetadata(inputs);
            Require(!result.Passed() && result.signal == signal, "Missing RR signal accepted or misidentified");
        }
        auto inputs = Inputs();
        inputs.signals[static_cast<size_t>(Signal::SpecularDistance)].frameId = 41;
        Require(Engine::InspectPathTracingRrFrame(inputs, Pixels()).common.issue ==
                    Engine::PathTracingInputIssue::FrameMismatch,
                "Common provenance failure was masked");
        for (size_t field = 0; field < 5; ++field)
        {
            auto pixels = Pixels();
            if (field == 0)
                ++pixels.frameId;
            if (field == 1)
                ++pixels.samplingSignature;
            if (field == 2)
                ++pixels.sampleStartIndex;
            if (field == 3)
                ++pixels.width;
            if (field == 4)
                ++pixels.height;
            Require(Engine::InspectPathTracingRrFrame(Inputs(), pixels).issue == Issue::StalePixels,
                    "Stale summary accepted");
        }
        auto pixels = Pixels();
        pixels.completed = false;
        Require(Engine::InspectPathTracingRrFrame(Inputs(), pixels).issue == Issue::PendingPixels,
                "Pending GPU accepted");
        for (uint64_t count : {uint64_t(0), uint64_t(255), uint64_t(257), std::numeric_limits<uint64_t>::max()})
        {
            pixels = Pixels();
            pixels.counts[0] = count;
            Require(Engine::InspectPathTracingRrFrame(Inputs(), pixels).issue == Issue::IncompleteCoverage,
                    "Incomplete/overflowed coverage accepted");
        }
        for (size_t status = 1; status < Engine::kPathTracingRrStatusCount; ++status)
        {
            pixels = Pixels();
            pixels.counts[0] = 255;
            pixels.counts[status] = 1;
            Require(Engine::InspectPathTracingRrFrame(Inputs(), pixels).issue == Issue::UnsupportedPixels,
                    "One unsupported pixel passed whole-frame gate");
        }
        pixels = Pixels();
        pixels.counts[0] = std::numeric_limits<uint64_t>::max();
        pixels.counts[1] = 257;
        Require(Engine::InspectPathTracingRrFrame(Inputs(), pixels).issue == Issue::IncompleteCoverage,
                "Wrapped sum accepted");
        std::cout << "RR input metadata, texture binding and whole-frame numeric gate passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
