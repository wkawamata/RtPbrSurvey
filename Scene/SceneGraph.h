#pragma once

#include "Scene/SceneDocument.h"

#include <DirectXMath.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RtPbrSurvey
{

struct SceneGraphEvaluation
{
    // World transforms follow the same order as SceneDocument::nodes.
    std::vector<DirectX::XMFLOAT4X4> worldTransforms;

    const DirectX::XMFLOAT4X4* FindWorld(std::string_view nodeId, const SceneDocument& document) const;
};

bool EvaluateSceneGraph(const SceneDocument& document, SceneGraphEvaluation& evaluation, std::string* error = nullptr);

bool ReparentSceneNodePreservingWorld(SceneDocument& document,
                                      std::string_view nodeId,
                                      const std::optional<std::string>& newParentId,
                                      std::string* error = nullptr);

} // namespace RtPbrSurvey
