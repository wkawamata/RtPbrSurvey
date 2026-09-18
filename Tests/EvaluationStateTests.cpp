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
    state.name = "DLSS RR \xE6\x97\xA5";
    state.comment = "\xE6\x9C\xAC\xE8\xAA\x9E";
    state.sceneIndex = -1;
    state.sceneName = "Test Scene";
    state.sceneReference = {"scene-test-001", "Assets/Scenes/TestScene/scene.json"};
    state.sceneConfig = {{"camera", {{"mode", "arcball"}}}, {"renderingPath", 1}};
    state.roi = {true, 0.1f, 0.2f, 0.4f, 0.5f};
    state.testItems = {
        {1, "\xE5\x8F\x8D\xE5\xB0\x84", RtPbrSurvey::EvaluationJudgmentKind::Score1To5},
        {2, "\xE6\xAE\x8B\xE5\x83\x8F", RtPbrSurvey::EvaluationJudgmentKind::Boolean},
    };
    state.runs = {{1, "\xE5\x88\x9D\xE5\x9B\x9E", {{1, 4, false}, {2, 3, true}}}};

    const std::string serialized = RtPbrSurvey::SerializeEvaluationStates({state});
    const nlohmann::json json = nlohmann::json::parse(serialized);
    const nlohmann::json& results = json.at("states").at(0).at("runs").at(0).at("results");
    std::vector<RtPbrSurvey::EvaluationState> restored;
    std::string error;

    bool passed = Check(results.at(0).at("score").is_number_integer(), "score is stored as an integer");
    passed &= Check(results.at(1).at("booleanValue").is_boolean(), "boolean result is stored as a JSON boolean");
    passed &= Check(serialized.find("\xE5\x8F\x8D\xE5\xB0\x84") != std::string::npos,
                    "Japanese prompt remains readable UTF-8");
    passed &=
        Check(RtPbrSurvey::DeserializeEvaluationStates(serialized, restored, &error), "evaluation states deserialize");
    passed &= Check(error.empty(), "successful deserialize clears error");
    passed &= Check(restored.size() == 1 && restored[0].name == state.name, "state identity round-trips");
    passed &= Check(restored[0].comment == state.comment, "Japanese comment round-trips");
    passed &= Check(restored[0].sceneConfig == state.sceneConfig, "captured scene config round-trips");
    passed &= Check(restored[0].sceneReference.id == state.sceneReference.id, "stable scene ID round-trips");
    passed &= Check(restored[0].sceneReference.path == state.sceneReference.path,
                    "file-backed scene path round-trips");
    passed &= Check(restored[0].testItems.size() == 2 && restored[0].runs.size() == 1 &&
                        restored[0].runs[0].results[0].score == 4,
                    "score result round-trips");
    passed &= Check(restored[0].runs[0].results[1].booleanValue, "boolean result round-trips");
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

bool TestLegacyStateMigratesToFirstRun()
{
    const std::string document =
        R"({"schemaVersion":1,"states":[{"id":1,"name":"Legacy","scene":{"index":0,"name":"Scene","config":{}},"roi":{},"testItems":[{"id":9,"prompt":"Legacy item","judgment":"score1To5","value":5}]}]})";
    std::vector<RtPbrSurvey::EvaluationState> states;
    std::string error;
    return Check(RtPbrSurvey::DeserializeEvaluationStates(document, states, &error),
                 "legacy evaluation state deserializes") &&
           Check(states.size() == 1 && states[0].comment.empty(), "missing comment defaults to empty") &&
           Check(states[0].runs.size() == 1 && states[0].runs[0].results[0].score == 5,
                 "legacy result migrates to the first run");
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
    state.name = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
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
                        TestInvalidDocumentIsNonDestructive() && TestLegacyStateMigratesToFirstRun() &&
                        TestStoreSavesAndLoadsEvaluationStates();
    if (passed)
    {
        std::cout << "EvaluationState tests passed.\n";
        return 0;
    }
    return 1;
}
