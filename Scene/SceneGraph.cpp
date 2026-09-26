#include "stdafx.h"

#include "Scene/SceneGraph.h"

#include <cmath>
#include <functional>
#include <unordered_map>

namespace RtPbrSurvey
{
namespace
{
bool IsFinite(float value)
{
    return std::isfinite(value);
}

bool IsFinite(const SceneFloat3& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const SceneFloat4& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

bool IsPositiveScale(const SceneFloat3& value)
{
    return IsFinite(value) && value.x > 0.0f && value.y > 0.0f && value.z > 0.0f;
}

DirectX::XMMATRIX CreateLocalMatrix(const SceneTransform& transform)
{
    using namespace DirectX;
    const XMVECTOR rotation = XMQuaternionNormalize(
        XMVectorSet(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w));
    return XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z) * XMMatrixRotationQuaternion(rotation) *
           XMMatrixTranslation(transform.translation.x, transform.translation.y, transform.translation.z);
}

bool HasValidTransform(const SceneTransform& transform)
{
    if (!IsFinite(transform.translation) || !IsFinite(transform.rotation) || !IsPositiveScale(transform.scale))
    {
        return false;
    }
    const float rotationLengthSquared = transform.rotation.x * transform.rotation.x +
                                        transform.rotation.y * transform.rotation.y +
                                        transform.rotation.z * transform.rotation.z +
                                        transform.rotation.w * transform.rotation.w;
    return IsFinite(rotationLengthSquared) && rotationLengthSquared > 0.000001f;
}

bool IsFiniteMatrix(DirectX::FXMMATRIX matrix)
{
    DirectX::XMFLOAT4X4 value;
    DirectX::XMStoreFloat4x4(&value, matrix);
    const float* elements = &value._11;
    for (size_t index = 0; index < 16; ++index)
    {
        if (!std::isfinite(elements[index]))
        {
            return false;
        }
    }
    return true;
}

} // namespace

const DirectX::XMFLOAT4X4* SceneGraphEvaluation::FindWorld(std::string_view nodeId,
                                                            const SceneDocument& document) const
{
    if (worldTransforms.size() != document.nodes.size())
    {
        return nullptr;
    }
    for (size_t index = 0; index < document.nodes.size(); ++index)
    {
        if (document.nodes[index].id == nodeId)
        {
            return &worldTransforms[index];
        }
    }
    return nullptr;
}

bool EvaluateSceneGraph(const SceneDocument& document, SceneGraphEvaluation& evaluation, std::string* error)
{
    std::unordered_map<std::string, size_t> nodeIndices;
    for (size_t index = 0; index < document.nodes.size(); ++index)
    {
        const SceneNode& node = document.nodes[index];
        if (node.id.empty() || !nodeIndices.emplace(node.id, index).second)
        {
            if (error != nullptr)
            {
                *error = "Scene graph contains duplicate or empty node ID.";
            }
            return false;
        }
        if (!HasValidTransform(node.transform))
        {
            if (error != nullptr)
            {
                *error = "Scene graph contains an invalid transform: " + node.id;
            }
            return false;
        }
    }

    std::vector<uint8_t> visitStates(document.nodes.size(), 0);
    std::vector<DirectX::XMFLOAT4X4> worldTransforms(document.nodes.size());
    std::function<bool(size_t)> evaluateNode;
    evaluateNode = [&](size_t index)
    {
        if (visitStates[index] == 2)
        {
            return true;
        }
        const SceneNode& node = document.nodes[index];
        if (visitStates[index] == 1)
        {
            if (error != nullptr)
            {
                *error = "Scene graph contains a parent cycle at node: " + node.id;
            }
            return false;
        }
        visitStates[index] = 1;

        DirectX::XMMATRIX world = CreateLocalMatrix(node.transform);
        if (node.parentId.has_value())
        {
            if (*node.parentId == node.id)
            {
                if (error != nullptr)
                {
                    *error = "Scene graph node cannot be its own parent: " + node.id;
                }
                return false;
            }
            const auto parent = nodeIndices.find(*node.parentId);
            if (parent == nodeIndices.end())
            {
                if (error != nullptr)
                {
                    *error = "Scene graph parent is unavailable: " + *node.parentId;
                }
                return false;
            }
            if (!evaluateNode(parent->second))
            {
                return false;
            }
            world = world * DirectX::XMLoadFloat4x4(&worldTransforms[parent->second]);
        }
        if (!IsFiniteMatrix(world))
        {
            if (error != nullptr)
            {
                *error = "Scene graph world transform is invalid: " + node.id;
            }
            return false;
        }
        DirectX::XMStoreFloat4x4(&worldTransforms[index], world);
        visitStates[index] = 2;
        return true;
    };

    for (size_t index = 0; index < document.nodes.size(); ++index)
    {
        if (!evaluateNode(index))
        {
            return false;
        }
    }
    evaluation.worldTransforms = std::move(worldTransforms);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool ReparentSceneNodePreservingWorld(SceneDocument& document,
                                      std::string_view nodeId,
                                      const std::optional<std::string>& newParentId,
                                      std::string* error)
{
    SceneGraphEvaluation originalEvaluation;
    if (!EvaluateSceneGraph(document, originalEvaluation, error))
    {
        return false;
    }

    size_t nodeIndex = document.nodes.size();
    size_t parentIndex = document.nodes.size();
    for (size_t index = 0; index < document.nodes.size(); ++index)
    {
        if (document.nodes[index].id == nodeId)
        {
            nodeIndex = index;
        }
        if (newParentId.has_value() && document.nodes[index].id == *newParentId)
        {
            parentIndex = index;
        }
    }
    if (nodeIndex == document.nodes.size())
    {
        if (error != nullptr)
        {
            *error = "Scene graph node is unavailable: " + std::string(nodeId);
        }
        return false;
    }
    if (newParentId.has_value() && parentIndex == document.nodes.size())
    {
        if (error != nullptr)
        {
            *error = "Scene graph parent is unavailable: " + *newParentId;
        }
        return false;
    }
    if (newParentId.has_value() && parentIndex == nodeIndex)
    {
        if (error != nullptr)
        {
            *error = "Scene graph node cannot be its own parent: " + std::string(nodeId);
        }
        return false;
    }

    const DirectX::XMMATRIX oldWorld = DirectX::XMLoadFloat4x4(&originalEvaluation.worldTransforms[nodeIndex]);
    DirectX::XMMATRIX newLocal = oldWorld;
    if (newParentId.has_value())
    {
        const DirectX::XMMATRIX parentWorld = DirectX::XMLoadFloat4x4(&originalEvaluation.worldTransforms[parentIndex]);
        DirectX::XMVECTOR determinant;
        const DirectX::XMMATRIX inverseParentWorld = DirectX::XMMatrixInverse(&determinant, parentWorld);
        if (std::abs(DirectX::XMVectorGetX(determinant)) <= 0.000001f)
        {
            if (error != nullptr)
            {
                *error = "Scene graph parent world transform is not invertible: " + *newParentId;
            }
            return false;
        }
        newLocal = oldWorld * inverseParentWorld;
    }

    DirectX::XMVECTOR scale;
    DirectX::XMVECTOR rotation;
    DirectX::XMVECTOR translation;
    if (!DirectX::XMMatrixDecompose(&scale, &rotation, &translation, newLocal))
    {
        if (error != nullptr)
        {
            *error = "Scene graph cannot represent the reparented transform as TRS: " + std::string(nodeId);
        }
        return false;
    }

    SceneDocument candidate = document;
    SceneNode& candidateNode = candidate.nodes[nodeIndex];
    candidateNode.parentId = newParentId;
    DirectX::XMFLOAT3 storedTranslation;
    DirectX::XMFLOAT4 storedRotation;
    DirectX::XMFLOAT3 storedScale;
    DirectX::XMStoreFloat3(&storedTranslation, translation);
    DirectX::XMStoreFloat4(&storedRotation, rotation);
    DirectX::XMStoreFloat3(&storedScale, scale);
    candidateNode.transform.translation = {storedTranslation.x, storedTranslation.y, storedTranslation.z};
    candidateNode.transform.rotation = {storedRotation.x, storedRotation.y, storedRotation.z, storedRotation.w};
    candidateNode.transform.scale = {storedScale.x, storedScale.y, storedScale.z};
    if (!HasValidTransform(candidateNode.transform))
    {
        if (error != nullptr)
        {
            *error = "Scene graph reparenting produced an invalid transform: " + std::string(nodeId);
        }
        return false;
    }

    SceneGraphEvaluation candidateEvaluation;
    if (!EvaluateSceneGraph(candidate, candidateEvaluation, error))
    {
        return false;
    }
    document = std::move(candidate);
    return true;
}

} // namespace RtPbrSurvey
