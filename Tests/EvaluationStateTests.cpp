#include "stdafx.h"

#include "Runtime/EvaluationState.h"

#include <nlohmann/json.hpp>

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool TestRoundTripPreservesJapaneseAndTypedJudgments()
{
    RtPbrSurvey::EvaluationState state;
    state.id = 7;
    state.name = "DLSS RR 評価";
    state.sceneIndex = 3;
    state.sceneName = "DamagedHelmet";
    state.sceneConfig = {{"camera", {{"mode", "arcball"}}}, {"renderingPath", 1}};
    state.roi = {true, 0.1f, 0.2f, 0.4f, 0.5f};
    state.testItems = {
        {1, "反射ノイズが時間方向に安定している", RtPbrSurvey::EvaluationJudgmentKind::Score1To5, 4, false},
        {2, "移動中に残像がない", RtPbrSurvey::EvaluationJudgmentKind::Boolean, 3, true},
    };

    const std::string serialized = RtPbrSurvey::SerializeEvaluationStates({state});
    const nlohmann::json json = nlohmann::json::parse(serialized);
    const nlohmann::json& testItems = json.at("states").at(0).at("testItems");
    std::vector<RtPbrSurvey::EvaluationState> restored;
    std::string error;

    bool passed = Check(testItems.at(0).at("value").is_number_integer(), "score is stored as an integer");
    passed &= Check(testItems.at(1).at("value").is_boolean(), "boolean result is stored as a JSON boolean");
    passed &= Check(serialized.find("反射ノイズ") != std::string::npos, "Japanese prompt remains readable UTF-8");
    passed &=
        Check(RtPbrSurvey::DeserializeEvaluationStates(serialized, restored, &error), "evaluation states deserialize");
    passed &= Check(error.empty(), "successful deserialize clears error");
    passed &= Check(restored.size() == 1 && restored[0].name == state.name, "state identity round-trips");
    passed &= Check(restored[0].sceneConfig == state.sceneConfig, "captured scene config round-trips");
    passed &=
        Check(restored[0].testItems.size() == 2 && restored[0].testItems[0].score == 4, "score result round-trips");
    passed &= Check(restored[0].testItems[1].booleanValue, "boolean result round-trips");
    return passed;
}

bool TestRoiIsClampedToNormalizedViewport()
{
    RtPbrSurvey::EvaluationRoi roi{true, -0.2f, 0.8f, 1.4f, 0.8f};
    roi.Sanitize();
    return Check(roi.x == 0.0f && roi.y == 0.8f, "ROI origin is clamped") &&
           Check(roi.width == 1.0f && roi.height <= 0.200001f, "ROI extent remains inside the viewport");
}

bool TestInvalidDocumentIsNonDestructive()
{
    RtPbrSurvey::EvaluationState existing;
    existing.id = 9;
    std::vector<RtPbrSurvey::EvaluationState> states = {existing};
    std::string error;
    return Check(!RtPbrSurvey::DeserializeEvaluationStates("{invalid", states, &error), "invalid JSON is rejected") &&
           Check(!error.empty(), "invalid JSON reports an error") &&
           Check(states.size() == 1 && states[0].id == 9, "invalid JSON does not replace existing states");
}

bool TestStoreSavesAndLoadsEvaluationStates()
{
    char temporaryDirectory[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, temporaryDirectory) == 0)
    {
        return Check(false, "temporary directory is available");
    }
    const std::string path = std::string(temporaryDirectory) + "RtPbrSurvey-EvaluationStateTests-" +
                             std::to_string(GetCurrentProcessId()) + ".json";
    DeleteFileA(path.c_str());

    RtPbrSurvey::EvaluationStateStore source;
    source.SetPath(path);
    RtPbrSurvey::EvaluationState state;
    state.id = 12;
    state.name = "日本語保存テスト";
    source.States().push_back(state);
    std::string error;
    bool passed = Check(source.Save(&error), "evaluation store saves an atomic JSON file");

    RtPbrSurvey::EvaluationStateStore restored;
    restored.SetPath(path);
    passed &= Check(restored.Load(&error), "evaluation store loads the saved JSON file");
    passed &= Check(restored.States().size() == 1 && restored.States()[0].name == state.name,
                    "saved UTF-8 state is restored");
    DeleteFileA(path.c_str());
    DeleteFileA((path + ".tmp").c_str());
    return passed;
}
} // namespace

int main()
{
    const bool passed = TestRoundTripPreservesJapaneseAndTypedJudgments() && TestRoiIsClampedToNormalizedViewport() &&
                        TestInvalidDocumentIsNonDestructive() && TestStoreSavesAndLoadsEvaluationStates();
    if (passed)
    {
        std::cout << "EvaluationState tests passed.\n";
        return 0;
    }
    return 1;
}
