#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RtPbrSurvey
{
enum class EvaluationJudgmentKind
{
    Score1To5,
    Boolean,
};

struct EvaluationRoi
{
    bool enabled = false;
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;

    void Sanitize();
};

struct EvaluationTestItem
{
    uint64_t id = 0;
    std::string prompt;
    EvaluationJudgmentKind judgmentKind = EvaluationJudgmentKind::Score1To5;
    int score = 3;
    bool booleanValue = false;
};

struct EvaluationState
{
    uint64_t id = 0;
    std::string name;
    int sceneIndex = -1;
    std::string sceneName;
    nlohmann::json sceneConfig = nlohmann::json::object();
    EvaluationRoi roi;
    std::vector<EvaluationTestItem> testItems;
};

std::string SerializeEvaluationStates(const std::vector<EvaluationState>& states, int indent = 2);
bool DeserializeEvaluationStates(std::string_view jsonText,
                                 std::vector<EvaluationState>& states,
                                 std::string* error = nullptr);

class EvaluationStateStore
{
public:
    void SetPath(std::string path)
    {
        m_path = std::move(path);
    }
    const std::string& Path() const
    {
        return m_path;
    }

    bool Load(std::string* error = nullptr);
    bool Save(std::string* error = nullptr) const;
    uint64_t NextStateId() const;
    uint64_t NextTestItemId(const EvaluationState& state) const;

    std::vector<EvaluationState>& States()
    {
        return m_states;
    }
    const std::vector<EvaluationState>& States() const
    {
        return m_states;
    }

private:
    std::string m_path;
    std::vector<EvaluationState> m_states;
};
} // namespace RtPbrSurvey
