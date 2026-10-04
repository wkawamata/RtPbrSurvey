#include "stdafx.h"

#include "Runtime/EvaluationState.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace RtPbrSurvey
{
namespace
{
using json = nlohmann::json;

constexpr int kEvaluationStateSchemaVersion = 2;

const char* JudgmentKindName(EvaluationJudgmentKind kind)
{
    return kind == EvaluationJudgmentKind::Boolean ? "boolean" : "score1To5";
}

EvaluationJudgmentKind JudgmentKindFromJson(const json& value)
{
    return value.is_string() && value.get<std::string>() == "boolean" ? EvaluationJudgmentKind::Boolean
                                                                      : EvaluationJudgmentKind::Score1To5;
}

json RoiToJson(const EvaluationRoi& roi)
{
    return {
        {"enabled", roi.enabled},
        {"x", roi.x},
        {"y", roi.y},
        {"width", roi.width},
        {"height", roi.height},
    };
}

EvaluationRoi RoiFromJson(const json& value)
{
    EvaluationRoi roi;
    if (value.is_object())
    {
        roi.enabled = value.value("enabled", roi.enabled);
        roi.x = value.value("x", roi.x);
        roi.y = value.value("y", roi.y);
        roi.width = value.value("width", roi.width);
        roi.height = value.value("height", roi.height);
    }
    roi.Sanitize();
    return roi;
}

json TestItemToJson(const EvaluationTestItem& item)
{
    json value;
    value["id"] = item.id;
    value["prompt"] = item.prompt;
    value["judgment"] = JudgmentKindName(item.judgmentKind);
    return value;
}

EvaluationTestItem TestItemFromJson(const json& value)
{
    EvaluationTestItem item;
    item.id = value.value("id", uint64_t{0});
    item.prompt = value.value("prompt", std::string{});
    item.judgmentKind = JudgmentKindFromJson(value.value("judgment", json("score1To5")));
    return item;
}

json TestResultToJson(const EvaluationTestResult& result)
{
    return {
        {"testItemId", result.testItemId},
        {"score", std::clamp(result.score, 1, 5)},
        {"booleanValue", result.booleanValue},
    };
}

EvaluationTestResult TestResultFromJson(const json& value)
{
    EvaluationTestResult result;
    result.testItemId = value.value("testItemId", uint64_t{0});
    result.score = std::clamp(value.value("score", result.score), 1, 5);
    result.booleanValue = value.value("booleanValue", result.booleanValue);
    return result;
}

json RunToJson(const EvaluationRun& run)
{
    json results = json::array();
    for (const EvaluationTestResult& result : run.results)
    {
        results.push_back(TestResultToJson(result));
    }
    return {{"id", run.id}, {"comment", run.comment}, {"results", std::move(results)}};
}

EvaluationRun RunFromJson(const json& value)
{
    EvaluationRun run;
    run.id = value.value("id", uint64_t{0});
    run.comment = value.value("comment", std::string{});
    if (value.contains("results") && value.at("results").is_array())
    {
        for (const json& result : value.at("results"))
        {
            if (result.is_object())
            {
                run.results.push_back(TestResultFromJson(result));
            }
        }
    }
    return run;
}

json StateToJson(const EvaluationState& state)
{
    json testItems = json::array();
    for (const EvaluationTestItem& item : state.testItems)
    {
        testItems.push_back(TestItemToJson(item));
    }
    json runs = json::array();
    for (const EvaluationRun& run : state.runs)
    {
        runs.push_back(RunToJson(run));
    }

    return {
        {"id", state.id},
        {"name", state.name},
        {"comment", state.comment},
        {"scene",
         {{"id", state.sceneReference.id},
          {"path", state.sceneReference.path},
          {"index", state.sceneIndex},
          {"name", state.sceneName},
          {"config", state.sceneConfig}}},
        {"roi", RoiToJson(state.roi)},
        {"testItems", std::move(testItems)},
        {"runs", std::move(runs)},
    };
}

EvaluationState StateFromJson(const json& value, int schemaVersion)
{
    EvaluationState state;
    state.id = value.value("id", uint64_t{0});
    state.name = value.value("name", std::string{});
    state.comment = value.value("comment", std::string{});
    if (value.contains("scene") && value.at("scene").is_object())
    {
        const json& scene = value.at("scene");
        state.sceneReference.id = scene.value("id", std::string{});
        state.sceneReference.path = scene.value("path", std::string{});
        state.sceneIndex = scene.value("index", -1);
        state.sceneName = scene.value("name", std::string{});
        if (scene.contains("config") && scene.at("config").is_object())
        {
            state.sceneConfig = scene.at("config");
        }
    }
    if (value.contains("roi"))
    {
        state.roi = RoiFromJson(value.at("roi"));
    }
    if (value.contains("testItems") && value.at("testItems").is_array())
    {
        for (const json& item : value.at("testItems"))
        {
            if (item.is_object())
            {
                state.testItems.push_back(TestItemFromJson(item));
            }
        }
    }
    if (schemaVersion >= 2 && value.contains("runs") && value.at("runs").is_array())
    {
        for (const json& run : value.at("runs"))
        {
            if (run.is_object())
            {
                state.runs.push_back(RunFromJson(run));
            }
        }
    }
    else if (schemaVersion == 1 && !state.testItems.empty())
    {
        EvaluationRun run;
        run.id = 1;
        for (const json& itemValue : value.at("testItems"))
        {
            if (!itemValue.is_object())
            {
                continue;
            }
            const EvaluationTestItem item = TestItemFromJson(itemValue);
            EvaluationTestResult result;
            result.testItemId = item.id;
            if (item.judgmentKind == EvaluationJudgmentKind::Boolean && itemValue.contains("value") &&
                itemValue.at("value").is_boolean())
            {
                result.booleanValue = itemValue.at("value").get<bool>();
            }
            else if (item.judgmentKind == EvaluationJudgmentKind::Score1To5 && itemValue.contains("value") &&
                     itemValue.at("value").is_number_integer())
            {
                result.score = std::clamp(itemValue.at("value").get<int>(), 1, 5);
            }
            run.results.push_back(result);
        }
        state.runs.push_back(std::move(run));
    }
    return state;
}
} // namespace

void EvaluationRoi::Sanitize()
{
    x = std::isfinite(x) ? x : 0.0f;
    y = std::isfinite(y) ? y : 0.0f;
    width = std::isfinite(width) ? width : 1.0f;
    height = std::isfinite(height) ? height : 1.0f;
    x = std::clamp(x, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);
    width = std::clamp(width, 0.0f, 1.0f - x);
    height = std::clamp(height, 0.0f, 1.0f - y);
}

std::string SerializeEvaluationStates(const std::vector<EvaluationState>& states, int indent)
{
    json values = json::array();
    for (const EvaluationState& state : states)
    {
        values.push_back(StateToJson(state));
    }
    const json root = {{"schemaVersion", kEvaluationStateSchemaVersion}, {"states", std::move(values)}};
    return root.dump(indent, ' ', false, json::error_handler_t::replace);
}

bool DeserializeEvaluationStates(std::string_view jsonText, std::vector<EvaluationState>& states, std::string* error)
{
    try
    {
        const json root = json::parse(jsonText);
        const int schemaVersion = root.value("schemaVersion", 0);
        if (!root.is_object() || (schemaVersion != 1 && schemaVersion != kEvaluationStateSchemaVersion) ||
            !root.contains("states") || !root.at("states").is_array())
        {
            if (error != nullptr)
            {
                *error = "Unsupported or invalid evaluation state document.";
            }
            return false;
        }

        std::vector<EvaluationState> parsedStates;
        for (const json& value : root.at("states"))
        {
            if (value.is_object())
            {
                parsedStates.push_back(StateFromJson(value, schemaVersion));
            }
        }
        states = std::move(parsedStates);
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error != nullptr)
        {
            *error = exception.what();
        }
        return false;
    }
}

bool EvaluationStateStore::Load(std::string* error)
{
    std::ifstream file(m_path, std::ios::binary);
    if (!file.is_open())
    {
        m_states.clear();
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return DeserializeEvaluationStates(text, m_states, error);
}

bool EvaluationStateStore::Save(std::string* error) const
{
    if (m_path.empty())
    {
        if (error != nullptr)
        {
            *error = "Evaluation state path is empty.";
        }
        return false;
    }

    const std::string temporaryPath = m_path + ".tmp";
    std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        if (error != nullptr)
        {
            *error = "Could not open the temporary evaluation state file.";
        }
        return false;
    }
    file << SerializeEvaluationStates(m_states);
    file.close();
    if (!file)
    {
        if (error != nullptr)
        {
            *error = "Could not write the evaluation state file.";
        }
        return false;
    }
    if (!MoveFileExA(temporaryPath.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileA(temporaryPath.c_str());
        if (error != nullptr)
        {
            *error = "Could not replace the evaluation state file.";
        }
        return false;
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

uint64_t EvaluationStateStore::NextStateId() const
{
    uint64_t nextId = 1;
    for (const EvaluationState& state : m_states)
    {
        nextId = (std::max)(nextId, state.id + 1);
    }
    return nextId;
}

uint64_t EvaluationStateStore::NextTestItemId(const EvaluationState& state) const
{
    uint64_t nextId = 1;
    for (const EvaluationTestItem& item : state.testItems)
    {
        nextId = (std::max)(nextId, item.id + 1);
    }
    return nextId;
}

uint64_t EvaluationStateStore::NextRunId(const EvaluationState& state) const
{
    uint64_t nextId = 1;
    for (const EvaluationRun& run : state.runs)
    {
        nextId = (std::max)(nextId, run.id + 1);
    }
    return nextId;
}
} // namespace RtPbrSurvey
