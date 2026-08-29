// GltfLoader.cpp
#include "stdafx.h"

#include "GltfLoader.h"

#include "MyDx12Utils.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <tiny_gltf.h>

static const unsigned char* GetAccessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
{
    const auto& view = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[view.buffer];

    return buffer.data.data() + view.byteOffset + accessor.byteOffset;
}

static DirectX::XMFLOAT3 ConvertGltfVectorToEngineLH(float x, float y, float z)
{
    return {x, y, -z};
}

static DirectX::XMFLOAT4 ConvertGltfTangentToEngineLH(float x, float y, float z, float w)
{
    return {x, y, -z, -w};
}

static DirectX::XMMATRIX GetNodeLocalTransform(const tinygltf::Node& node)
{
    using namespace DirectX;

    if (node.matrix.size() == 16)
    {
        XMFLOAT4X4 m = {};
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 4; col++)
            {
                m.m[row][col] = static_cast<float>(node.matrix[col * 4 + row]);
            }
        }
        return XMLoadFloat4x4(&m);
    }

    XMVECTOR scale = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    XMVECTOR rotation = XMQuaternionIdentity();
    XMVECTOR translation = XMVectorZero();

    if (node.scale.size() == 3)
    {
        scale = XMVectorSet(static_cast<float>(node.scale[0]),
                            static_cast<float>(node.scale[1]),
                            static_cast<float>(node.scale[2]),
                            0.0f);
    }

    if (node.rotation.size() == 4)
    {
        rotation = XMVectorSet(static_cast<float>(node.rotation[0]),
                               static_cast<float>(node.rotation[1]),
                               static_cast<float>(node.rotation[2]),
                               static_cast<float>(node.rotation[3]));
    }

    if (node.translation.size() == 3)
    {
        translation = XMVectorSet(static_cast<float>(node.translation[0]),
                                  static_cast<float>(node.translation[1]),
                                  static_cast<float>(node.translation[2]),
                                  0.0f);
    }

    return XMMatrixScalingFromVector(scale) * XMMatrixRotationQuaternion(rotation) *
           XMMatrixTranslationFromVector(translation);
}

static bool AppendPrimitive(const tinygltf::Model& model,
                            const tinygltf::Primitive& prim,
                            const DirectX::XMMATRIX& nodeTransform,
                            GltfMeshData& outMesh,
                            int& firstMaterialIndex)
{
    using namespace DirectX;

    auto posIt = prim.attributes.find("POSITION");
    auto normalIt = prim.attributes.find("NORMAL");
    auto uvIt = prim.attributes.find("TEXCOORD_0");
    auto tangentIt = prim.attributes.find("TANGENT");

    if (posIt == prim.attributes.end())
        return false;

    const auto& posAccessor = model.accessors[posIt->second];
    const float* positions = reinterpret_cast<const float*>(GetAccessorData(model, posAccessor));

    const float* normals = nullptr;
    if (normalIt != prim.attributes.end())
    {
        const auto& normalAccessor = model.accessors[normalIt->second];
        normals = reinterpret_cast<const float*>(GetAccessorData(model, normalAccessor));
    }

    const float* uvs = nullptr;
    if (uvIt != prim.attributes.end())
    {
        const auto& uvAccessor = model.accessors[uvIt->second];
        uvs = reinterpret_cast<const float*>(GetAccessorData(model, uvAccessor));
    }

    const float* tangents = nullptr;
    if (tangentIt != prim.attributes.end())
    {
        const auto& tangentAccessor = model.accessors[tangentIt->second];
        tangents = reinterpret_cast<const float*>(GetAccessorData(model, tangentAccessor));
        DBG_PRINT("glTF TANGENT attribute found.\n");
    }
    else
    {
        DBG_PRINT("glTF TANGENT attribute not found. Shader fallback tangent frame will be used.\n");
    }

    XMMATRIX normalTransform = nodeTransform;
    normalTransform.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

    XMVECTOR det = XMMatrixDeterminant(normalTransform);
    if (std::abs(XMVectorGetX(det)) > 0.000001f)
    {
        normalTransform = XMMatrixTranspose(XMMatrixInverse(&det, normalTransform));
    }
    else
    {
        normalTransform = XMMatrixIdentity();
    }

    const uint32_t baseVertex = static_cast<uint32_t>(outMesh.vertices.size());
    outMesh.vertices.resize(outMesh.vertices.size() + posAccessor.count);

    for (size_t i = 0; i < posAccessor.count; ++i)
    {
        GltfVertex v = {};

        XMVECTOR position = XMVectorSet(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2], 1.0f);
        position = XMVector3TransformCoord(position, nodeTransform);
        XMFLOAT3 p = {};
        XMStoreFloat3(&p, position);
        v.position = ConvertGltfVectorToEngineLH(p.x, p.y, p.z);

        if (normals)
        {
            XMVECTOR normal = XMVectorSet(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2], 0.0f);
            normal = XMVector3Normalize(XMVector3TransformNormal(normal, normalTransform));
            XMFLOAT3 n = {};
            XMStoreFloat3(&n, normal);
            v.normal = ConvertGltfVectorToEngineLH(n.x, n.y, n.z);
        }
        else
        {
            v.normal = {0.0f, 1.0f, 0.0f};
        }

        if (uvs)
        {
            v.uv = {uvs[i * 2 + 0], uvs[i * 2 + 1]};
        }
        else
        {
            v.uv = {0.0f, 0.0f};
        }

        if (tangents)
        {
            XMVECTOR tangent = XMVectorSet(tangents[i * 4 + 0], tangents[i * 4 + 1], tangents[i * 4 + 2], 0.0f);
            tangent = XMVector3Normalize(XMVector3TransformNormal(tangent, normalTransform));
            XMFLOAT3 t = {};
            XMStoreFloat3(&t, tangent);
            v.tangent = ConvertGltfTangentToEngineLH(t.x, t.y, t.z, static_cast<float>(tangents[i * 4 + 3]));
        }

        if (prim.material >= 0)
        {
            v.materialId = static_cast<uint32_t>(prim.material);
        }

        outMesh.vertices[baseVertex + i] = v;
    }

    if (prim.indices < 0)
        return false;

    const auto& indexAccessor = model.accessors[prim.indices];
    const auto* indexData = GetAccessorData(model, indexAccessor);
    const size_t baseIndex = outMesh.indices.size();
    outMesh.indices.resize(outMesh.indices.size() + indexAccessor.count);

    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
    {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(indexData);
        for (size_t i = 0; i < indexAccessor.count; ++i)
            outMesh.indices[baseIndex + i] = baseVertex + src[i];
    }
    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(indexData);
        for (size_t i = 0; i < indexAccessor.count; ++i)
            outMesh.indices[baseIndex + i] = baseVertex + src[i];
    }
    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
    {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(indexData);
        for (size_t i = 0; i < indexAccessor.count; ++i)
            outMesh.indices[baseIndex + i] = baseVertex + src[i];
    }
    else
    {
        return false;
    }

    // The Z mirror above flips triangle winding, so restore front faces for the LH renderer.
    for (size_t i = baseIndex; i + 2 < outMesh.indices.size(); i += 3)
    {
        std::swap(outMesh.indices[i + 1], outMesh.indices[i + 2]);
    }

    if (firstMaterialIndex < 0)
    {
        firstMaterialIndex = prim.material;
    }
    else if (prim.material != firstMaterialIndex)
    {
        DBG_PRINT(
            "glTF primitive material differs from first material. Current mesh path uses one material per instance.\n");
    }

    return true;
}

static std::string ResolveGltfPath(const std::string& path)
{
    const std::filesystem::path requestedPath(path);
    if (requestedPath.is_absolute() || std::filesystem::exists(requestedPath))
    {
        return requestedPath.string();
    }

    WCHAR executablePath[MAX_PATH] = {};
    const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, _countof(executablePath));
    if (pathLength == 0 || pathLength >= _countof(executablePath))
    {
        return path;
    }

    const std::filesystem::path runtimePath =
        std::filesystem::path(executablePath).parent_path() / requestedPath;
    return std::filesystem::exists(runtimePath) ? runtimePath.string() : path;
}

static bool LoadGltfModel(const std::string& path, tinygltf::Model& model, std::string& message)
{
    tinygltf::TinyGLTF loader;
    std::string warn;
    std::string error;
    const std::string resolvedPath = ResolveGltfPath(path);
    const bool isGlb = resolvedPath.size() >= 4 && resolvedPath.substr(resolvedPath.size() - 4) == ".glb";
    const bool loaded = isGlb ? loader.LoadBinaryFromFile(&model, &error, &warn, resolvedPath)
                              : loader.LoadASCIIFromFile(&model, &error, &warn, resolvedPath);

    if (!warn.empty())
    {
        OutputDebugStringA(("glTF warning: " + warn + "\n").c_str());
    }
    if (!error.empty())
    {
        OutputDebugStringA(("glTF error: " + error + "\n").c_str());
    }

    message = !error.empty() ? error : warn;
    return loaded;
}

static void CopyMaterialsAndTextures(const tinygltf::Model& model, GltfMeshData& outMesh)
{
    outMesh.materials.resize(model.materials.size());
    for (size_t materialIndex = 0; materialIndex < model.materials.size(); materialIndex++)
    {
        const auto& material = model.materials[materialIndex];
        const auto& pbr = material.pbrMetallicRoughness;
        GltfMaterial& destination = outMesh.materials[materialIndex];
        destination.albedoTexIndex = pbr.baseColorTexture.index;
        destination.metallicRoughnessTexIndex = pbr.metallicRoughnessTexture.index;
        destination.emissiveTexIndex = material.emissiveTexture.index;
        destination.occlusionTexIndex = material.occlusionTexture.index;
        destination.normalTexIndex = material.normalTexture.index;
        destination.roughnessFactor = static_cast<float>(pbr.roughnessFactor);
        destination.metallicFactor = static_cast<float>(pbr.metallicFactor);
        destination.occlusionStrength = static_cast<float>(material.occlusionTexture.strength);
        DBG_PRINT("model.materials[%zu].name = %s\n", materialIndex, material.name.c_str());
        DBG_PRINT("baseColorTexture.index: %d\n", destination.albedoTexIndex);
        DBG_PRINT("metallicRoughnessTexture.index: %d\n", destination.metallicRoughnessTexIndex);
        DBG_PRINT("emissiveTexture.index: %d\n", destination.emissiveTexIndex);
        DBG_PRINT("occlusionTexture.index: %d\n", destination.occlusionTexIndex);
        DBG_PRINT("normalTexture.index: %d\n", destination.normalTexIndex);
        for (int factorIndex = 0; factorIndex < 4; factorIndex++)
        {
            destination.baseColorFactor[factorIndex] = static_cast<float>(pbr.baseColorFactor[factorIndex]);
            DBG_PRINT("baseColorFactor[%d]: %f\n", factorIndex, destination.baseColorFactor[factorIndex]);
        }
        DBG_PRINT("roughnessFactor: %f\n", destination.roughnessFactor);
        DBG_PRINT("metallicFactor: %f\n", destination.metallicFactor);
        DBG_PRINT("occlusionStrength: %f\n", destination.occlusionStrength);
    }

    outMesh.textures.reserve(model.textures.size());
    for (const tinygltf::Texture& texture : model.textures)
    {
        if (texture.source < 0 || texture.source >= static_cast<int>(model.images.size()))
        {
            continue;
        }

        const tinygltf::Image& image = model.images[texture.source];
        GltfTextureData destination = {};
        DBG_PRINT("image.name = %s\n", image.name.c_str());
        destination.width = image.width;
        destination.height = image.height;
        destination.component = image.component;
        destination.pixels = image.image;
        outMesh.textures.push_back(std::move(destination));
    }
}

bool LoadGltfMesh(const std::string& path, GltfMeshData& outMesh)
{
    tinygltf::Model model;
    std::string message;
    if (!LoadGltfModel(path, model, message))
        return false;

    if (model.meshes.empty())
        return false;

    int matIndex = -1;
    std::function<bool(int, DirectX::XMMATRIX)> appendNode;
    appendNode = [&](int nodeIndex, DirectX::XMMATRIX parentTransform)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
            return false;

        const tinygltf::Node& node = model.nodes[nodeIndex];
        const DirectX::XMMATRIX nodeTransform = GetNodeLocalTransform(node) * parentTransform;

        if (node.mesh >= 0)
        {
            const tinygltf::Mesh& mesh = model.meshes[node.mesh];
            for (const tinygltf::Primitive& primitive : mesh.primitives)
            {
                if (!AppendPrimitive(model, primitive, nodeTransform, outMesh, matIndex))
                    return false;
            }
        }

        for (const int childIndex : node.children)
        {
            if (!appendNode(childIndex, nodeTransform))
                return false;
        }

        return true;
    };

    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (!model.scenes.empty() && sceneIndex >= 0 && sceneIndex < static_cast<int>(model.scenes.size()))
    {
        for (const int nodeIndex : model.scenes[sceneIndex].nodes)
        {
            if (!appendNode(nodeIndex, DirectX::XMMatrixIdentity()))
                return false;
        }
    }
    else
    {
        for (const tinygltf::Mesh& mesh : model.meshes)
        {
            for (const tinygltf::Primitive& primitive : mesh.primitives)
            {
                if (!AppendPrimitive(model, primitive, DirectX::XMMatrixIdentity(), outMesh, matIndex))
                    return false;
            }
        }
    }

    if (outMesh.vertices.empty() || outMesh.indices.empty())
        return false;

    outMesh.materialIndex = matIndex;

    DBG_PRINT("Material index: %d\n", matIndex);

    CopyMaterialsAndTextures(model, outMesh);

    return true;
}

struct Engine::GltfSceneAsset::Impl
{
    tinygltf::Model model;
};

static std::vector<int> GetGltfSceneRootNodes(const tinygltf::Model& model)
{
    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (!model.scenes.empty() && sceneIndex >= 0 && sceneIndex < static_cast<int>(model.scenes.size()))
    {
        return model.scenes[sceneIndex].nodes;
    }

    std::vector<bool> isChild(model.nodes.size(), false);
    for (const tinygltf::Node& node : model.nodes)
    {
        for (int childIndex : node.children)
        {
            if (childIndex >= 0 && childIndex < static_cast<int>(isChild.size()))
            {
                isChild[childIndex] = true;
            }
        }
    }

    std::vector<int> roots;
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); nodeIndex++)
    {
        if (!isChild[nodeIndex])
        {
            roots.push_back(static_cast<int>(nodeIndex));
        }
    }
    return roots;
}

Engine::GltfSceneAsset::GltfSceneAsset(std::shared_ptr<const Impl> impl) : m_impl(std::move(impl)) {}

bool Engine::GltfSceneAsset::IsValid() const
{
    return m_impl != nullptr;
}

Engine::GltfSceneAssetLoadResult Engine::LoadGltfSceneAsset(const std::string& path)
{
    GltfSceneAssetLoadResult result = {};
    auto impl = std::make_shared<GltfSceneAsset::Impl>();
    if (!LoadGltfModel(path, impl->model, result.message))
    {
        result.status = GltfSceneAssetLoadStatus::FileLoadFailed;
        return result;
    }
    if (impl->model.meshes.empty())
    {
        result.status = GltfSceneAssetLoadStatus::NoMeshes;
        result.message = "The glTF asset contains no meshes.";
        return result;
    }

    result.status = GltfSceneAssetLoadStatus::Success;
    result.asset = GltfSceneAsset(std::move(impl));
    result.message.clear();
    return result;
}

std::vector<std::string> Engine::GetGltfMeshNodeNames(const GltfSceneAsset& asset)
{
    std::vector<std::string> names;
    if (!asset.m_impl)
    {
        return names;
    }

    const tinygltf::Model& model = asset.m_impl->model;
    std::function<void(int)> appendNames;
    appendNames = [&](int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
        {
            return;
        }

        const tinygltf::Node& node = model.nodes[nodeIndex];
        if (node.mesh >= 0 && !node.name.empty())
        {
            names.push_back(node.name);
        }
        for (int childIndex : node.children)
        {
            appendNames(childIndex);
        }
    };

    for (int rootNodeIndex : GetGltfSceneRootNodes(model))
    {
        appendNames(rootNodeIndex);
    }
    return names;
}

Engine::GltfNodeMeshStatus
Engine::GltfSceneAsset::ExtractNodeMesh(const std::string& nodeName, GltfMeshData& outMesh, std::string& message) const
{
    outMesh = {};
    message.clear();
    if (!m_impl)
    {
        message = "The glTF scene asset is invalid.";
        return GltfNodeMeshStatus::InvalidAsset;
    }

    const tinygltf::Model& model = m_impl->model;
    int matchCount = 0;
    bool conversionSucceeded = true;
    int firstMaterialIndex = -1;
    std::function<void(int, DirectX::XMMATRIX)> findNode;
    findNode = [&](int nodeIndex, DirectX::XMMATRIX parentTransform)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
        {
            conversionSucceeded = false;
            return;
        }

        const tinygltf::Node& node = model.nodes[nodeIndex];
        const DirectX::XMMATRIX nodeTransform = GetNodeLocalTransform(node) * parentTransform;
        if (node.mesh >= 0 && node.name == nodeName)
        {
            matchCount++;
            if (matchCount == 1 && node.mesh < static_cast<int>(model.meshes.size()))
            {
                const tinygltf::Mesh& mesh = model.meshes[node.mesh];
                for (const tinygltf::Primitive& primitive : mesh.primitives)
                {
                    if (!AppendPrimitive(model, primitive, nodeTransform, outMesh, firstMaterialIndex))
                    {
                        conversionSucceeded = false;
                        break;
                    }
                }
            }
        }

        for (int childIndex : node.children)
        {
            findNode(childIndex, nodeTransform);
        }
    };

    for (int rootNodeIndex : GetGltfSceneRootNodes(model))
    {
        findNode(rootNodeIndex, DirectX::XMMatrixIdentity());
    }

    if (matchCount == 0)
    {
        message = "No mesh node named '" + nodeName + "' exists in the default glTF scene.";
        return GltfNodeMeshStatus::NodeNotFound;
    }
    if (matchCount > 1)
    {
        outMesh = {};
        message = "More than one mesh node is named '" + nodeName + "' in the default glTF scene.";
        return GltfNodeMeshStatus::DuplicateNodeName;
    }
    if (!conversionSucceeded || outMesh.vertices.empty() || outMesh.indices.empty())
    {
        outMesh = {};
        message = "The mesh node named '" + nodeName + "' could not be converted.";
        return GltfNodeMeshStatus::MeshConversionFailed;
    }

    outMesh.materialIndex = firstMaterialIndex;
    CopyMaterialsAndTextures(model, outMesh);
    return GltfNodeMeshStatus::Success;
}
