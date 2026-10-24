#include "Renderer/NrdReconstructionInputs.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <set>

namespace
{
using Signal = Engine::PathTracingInputSignal;
using Issue = Engine::NrdInputMetadataIssue;

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

Engine::PathTracingReconstructionInputs MakeInputs()
{
    Engine::PathTracingReconstructionInputs inputs;
    auto& frame = inputs.frame;
    frame.renderWidth = frame.renderHeight = frame.outputWidth = frame.outputHeight = 16;
    frame.pixelCenterPrimary = true;
    frame.samplingSignature = 7;
    const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    frame.worldToView = frame.viewToClip = frame.previousViewProjection = identity;
    for (size_t i = 0; i < inputs.signals.size(); ++i)
    {
        DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        if (i == static_cast<size_t>(Signal::ViewZ))
        {
            format = DXGI_FORMAT_R32_FLOAT;
        }
        else if (i == static_cast<size_t>(Signal::MotionVectors))
        {
            format = DXGI_FORMAT_R16G16_FLOAT;
        }
        inputs.signals[i] = {true, format, 16, 16, 0, 0, 1, 7};
    }
    return inputs;
}

bool CheckShaderBindings()
{
    // Read the actual shader declaration rather than testing the C++ table
    // against a second table of register numbers.
    std::ifstream stream(RTPBRSURVEY_NRD_INPUT_SHADER_PATH);
    if (!stream)
    {
        return Check(false, "NRD conversion shader source is available");
    }
    const std::string shader{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const std::map<std::string, Signal> meanings = {{"g_normal", Signal::NormalRoughness},
                                                    {"g_albedo", Signal::BaseColor},
                                                    {"g_material", Signal::MaterialF0Metallic},
                                                    {"g_diffuse", Signal::PhysicalDiffuse},
                                                    {"g_specular", Signal::PhysicalSpecular},
                                                    {"g_diffuseDistance", Signal::DiffuseDistance},
                                                    {"g_specularDistance", Signal::SpecularDistance},
                                                    {"g_primary", Signal::PrimaryPosition},
                                                    {"g_diffuseDirection", Signal::DiffuseDirection},
                                                    {"g_specularDirection", Signal::SpecularDirection},
                                                    {"g_viewZ", Signal::ViewZ}};
    const std::regex declaration(R"(Texture2D<[^>]+>\s+(\w+)\s*:\s*register\(t(\d+)\))");
    std::set<size_t> slots;
    bool passed = true;
    for (auto entry = std::sregex_iterator(shader.begin(), shader.end(), declaration); entry != std::sregex_iterator();
         ++entry)
    {
        const size_t slot = std::stoul((*entry)[2].str());
        const auto meaning = meanings.find((*entry)[1].str());
        if (slot >= Engine::kNrdRelaxConversionSignals.size() || meaning == meanings.end() ||
            !slots.insert(slot).second)
        {
            passed &= Check(false, "unexpected or duplicate NRD input shader slot");
            continue;
        }
        passed &= Check(Engine::kNrdRelaxConversionSignals[slot] == meaning->second,
                        "common semantic matches actual shader register");
    }
    return Check(slots.size() == meanings.size(), "all NRD shader inputs accounted for") && passed;
}
} // namespace

int main()
{
    bool passed = CheckShaderBindings();
    passed &= Check(Engine::InspectNrdReconstructionInputs(MakeInputs(), true).MetadataReady(), "full NRD inventory");
    for (Signal signal : Engine::kNrdRelaxConversionSignals)
    {
        auto inputs = MakeInputs();
        inputs.signals[static_cast<size_t>(signal)].present = false;
        const auto result = Engine::InspectNrdReconstructionInputs(inputs, false);
        const bool commonRequired = static_cast<size_t>(signal) <= static_cast<size_t>(Signal::MaterialF0Metallic);
        passed &= Check(result.issue == (commonRequired ? Issue::CommonContract : Issue::MissingConversionSignal) &&
                            result.signal == signal,
                        "missing conversion capability rejected with exact signal");
    }
    for (Signal signal : {Signal::StableRadiance, Signal::BackgroundRadiance})
    {
        auto inputs = MakeInputs();
        inputs.signals[static_cast<size_t>(signal)].present = false;
        passed &= Check(Engine::InspectNrdReconstructionInputs(inputs, false).MetadataReady(),
                        "conversion-only profile does not require composition inputs");
        const auto result = Engine::InspectNrdReconstructionInputs(inputs, true);
        passed &= Check(result.issue == Issue::MissingCompositionSignal && result.signal == signal,
                        "composition profile requires stable and background signals");
    }
    auto inputs = MakeInputs();
    inputs.signals[static_cast<size_t>(Signal::SpecularDistance)].samplingSignature = 8;
    passed &= Check(Engine::InspectNrdReconstructionInputs(inputs, true).common.issue ==
                        Engine::PathTracingInputIssue::SamplingPolicyMismatch,
                    "backend requirements do not mask common provenance failure");
    inputs = MakeInputs();
    for (size_t i = static_cast<size_t>(Signal::PhysicalDiffuse); i < inputs.signals.size(); ++i)
    {
        inputs.signals[i] = {};
    }
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).Passed() &&
                        !Engine::InspectNrdReconstructionInputs(inputs, false).MetadataReady(),
                    "common pass does not imply NRD conversion capability");
    if (passed)
    {
        std::cout << "NRD common-input capability and shader binding contract passed\n";
    }
    return passed ? 0 : 1;
}
