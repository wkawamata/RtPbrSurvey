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
#include <cstdio>

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
                m.m[row][col] = static_cast<float>(node.matrix[row * 4 + col]);
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
    const float transformHandedness = XMVectorGetX(det) < 0.0f ? -1.0f : 1.0f;
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
            tangent = XMVector3Normalize(XMVector3TransformNormal(tangent, nodeTransform));
            XMFLOAT3 t = {};
            XMStoreFloat3(&t, tangent);
            v.tangent = ConvertGltfTangentToEngineLH(
                t.x, t.y, t.z, static_cast<float>(tangents[i * 4 + 3]) * transformHandedness);
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

    // RH-to-LH conversion flips winding unless the baked node transform already mirrors it.
    for (size_t i = baseIndex; transformHandedness > 0.0f && i + 2 < outMesh.indices.size(); i += 3)
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

static bool ValidateGltfImport(const tinygltf::Model& model, std::vector<GltfImportDiagnostic>& diagnostics)
{
    bool valid = true;
    const auto report = [&](GltfDiagnosticSeverity severity, const char* code,
                            const std::string& location, const char* message)
    {
        diagnostics.push_back({severity, code, location, message});
        valid = valid && severity != GltfDiagnosticSeverity::Error;
    };
    const auto warning = [&](const char* code, const std::string& location, const char* message)
    {
        report(GltfDiagnosticSeverity::Warning, code, location, message);
    };
    const auto error = [&](const char* code, const std::string& location, const char* message)
    {
        report(GltfDiagnosticSeverity::Error, code, location, message);
    };
    const auto accessorValid = [&](int index, int type, int componentType, size_t count,
                                    const std::string& location)
    {
        if (index < 0 || index >= static_cast<int>(model.accessors.size()))
        {
            error("InvalidAccessor", location, "Accessor index is outside the asset.");
            return false;
        }
        const tinygltf::Accessor& accessor = model.accessors[index];
        if (accessor.type != type || accessor.componentType != componentType || accessor.normalized ||
            accessor.sparse.isSparse || (count != 0 && accessor.count != count))
        {
            error("UnsupportedAccessor", location, "Expected dense, unnormalized attributes with matching counts and supported types.");
            return false;
        }
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        {
            error("InvalidBufferView", location, "Accessor has no valid dense buffer view.");
            return false;
        }
        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        const size_t elementSize = static_cast<size_t>(tinygltf::GetComponentSizeInBytes(componentType)) *
                                   static_cast<size_t>(tinygltf::GetNumComponentsInType(type));
        if (view.byteStride != 0 && view.byteStride != elementSize)
        {
            error("UnsupportedStride", location, "Interleaved attributes are not supported by the packed-data importer.");
            return false;
        }
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
        {
            error("InvalidBuffer", location, "Buffer view references an invalid buffer.");
            return false;
        }
        const size_t size = model.buffers[view.buffer].data.size();
        if (view.byteOffset > size || view.byteLength > size - view.byteOffset ||
            accessor.byteOffset > view.byteLength || accessor.count == 0 ||
            accessor.count > (view.byteLength - accessor.byteOffset) / elementSize ||
            (view.byteOffset + accessor.byteOffset) % tinygltf::GetComponentSizeInBytes(componentType) != 0)
        {
            error("InvalidAccessorBounds", location, "Accessor data is empty, unaligned or outside its buffer view.");
            return false;
        }
        if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
        {
            const unsigned char* data = GetAccessorData(model, accessor);
            for (size_t offset = 0; offset < accessor.count * elementSize; offset += sizeof(float))
            {
                float value = 0.0f;
                std::memcpy(&value, data + offset, sizeof(float));
                if (!std::isfinite(value))
                {
                    error("NonfiniteAttribute", location, "Float attributes must contain finite values.");
                    return false;
                }
            }
        }
        return true;
    };
    for (const std::string& extension : model.extensionsUsed)
    {
        warning("UnsupportedExtension", "extensionsUsed." + extension, "Extension behavior is not implemented; only core data will be imported.");
    }
    for (const std::string& extension : model.extensionsRequired)
    {
        error("RequiredExtension", "extensionsRequired." + extension, "Required extensions are not supported.");
    }
    if (!model.animations.empty())
    {
        warning("AnimationIgnored", "animations", "Animations are ignored; the static node pose is imported.");
    }
    for (size_t i = 0; i < model.materials.size(); ++i)
    {
        const tinygltf::Material& material = model.materials[i];
        const std::string location = "materials[" + std::to_string(i) + "]";
        if (material.alphaMode != "OPAQUE")
        {
            warning("AlphaModeIgnored", location, "Alpha MASK/BLEND is rendered as opaque; cutoff and blending are ignored.");
        }
        if (material.doubleSided)
        {
            warning("DoubleSidedIgnored", location, "Material doubleSided is ignored; ray paths use back-face culling.");
        }
        for (const auto& extension : material.extensions)
        {
            warning("MaterialExtensionIgnored", location + ".extensions." + extension.first,
                    "Material extension behavior is not implemented; core material fields are used.");
        }
        const auto checkTexture = [&](int index, int texCoord, const tinygltf::ExtensionMap& extensions, const char* name)
        {
            if (index >= static_cast<int>(model.textures.size()) || index < -1)
            {
                error("InvalidTexture", location + "." + name, "Texture index is outside the asset.");
            }
            if (index >= 0 && texCoord != 0)
            {
                warning("TextureCoordinateIgnored", location + "." + name, "Only TEXCOORD_0 is used; the requested coordinate set is ignored.");
            }
            if (!extensions.empty())
            {
                warning("TextureExtensionIgnored", location + "." + name, "Texture-info extensions, including texture transforms, are ignored.");
            }
        };
        checkTexture(material.pbrMetallicRoughness.baseColorTexture.index, material.pbrMetallicRoughness.baseColorTexture.texCoord,
                     material.pbrMetallicRoughness.baseColorTexture.extensions, "baseColorTexture");
        checkTexture(material.pbrMetallicRoughness.metallicRoughnessTexture.index, material.pbrMetallicRoughness.metallicRoughnessTexture.texCoord,
                     material.pbrMetallicRoughness.metallicRoughnessTexture.extensions, "metallicRoughnessTexture");
        checkTexture(material.normalTexture.index, material.normalTexture.texCoord, material.normalTexture.extensions, "normalTexture");
        checkTexture(material.emissiveTexture.index, material.emissiveTexture.texCoord, material.emissiveTexture.extensions, "emissiveTexture");
        checkTexture(material.occlusionTexture.index, material.occlusionTexture.texCoord, material.occlusionTexture.extensions, "occlusionTexture");
    }
    for (size_t i = 0; i < model.textures.size(); ++i)
    {
        const tinygltf::Texture& texture = model.textures[i];
        const std::string location = "textures[" + std::to_string(i) + "]";
        if (texture.source < 0 || texture.source >= static_cast<int>(model.images.size()))
        {
            error("InvalidTextureSource", location, "Texture source must reference a decoded core image.");
        }
        if (texture.sampler >= 0)
        {
            warning("SamplerIgnored", location, "Per-texture sampler settings are ignored; the renderer's shared sampler is used.");
        }
    }
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
    {
        const tinygltf::Mesh& mesh = model.meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
        {
            const tinygltf::Primitive& primitive = mesh.primitives[primitiveIndex];
            const std::string location = "meshes[" + std::to_string(meshIndex) + "].primitives[" + std::to_string(primitiveIndex) + "]";
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1)
            {
                error("UnsupportedPrimitiveMode", location, "Only indexed TRIANGLES primitives are supported.");
            }
            if (primitive.material < -1 || primitive.material >= static_cast<int>(model.materials.size()))
            {
                error("InvalidMaterial", location, "Primitive material index is outside the asset.");
            }
            if (!primitive.targets.empty())
            {
                warning("MorphTargetsIgnored", location, "Morph targets are ignored; base vertex data is imported.");
            }
            const auto position = primitive.attributes.find("POSITION");
            if (position == primitive.attributes.end() ||
                !accessorValid(position->second, TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_FLOAT, 0, location + ".POSITION"))
            {
                error("MissingOrInvalidPosition", location, "A supported POSITION accessor is required.");
                continue;
            }
            const size_t vertexCount = model.accessors[position->second].count;
            for (const auto& attribute : primitive.attributes)
            {
                int type = 0;
                if (attribute.first == "NORMAL")
                {
                    type = TINYGLTF_TYPE_VEC3;
                }
                if (attribute.first == "TANGENT")
                {
                    type = TINYGLTF_TYPE_VEC4;
                }
                if (attribute.first == "TEXCOORD_0")
                {
                    type = TINYGLTF_TYPE_VEC2;
                }
                if (type != 0)
                {
                    accessorValid(attribute.second, type, TINYGLTF_COMPONENT_TYPE_FLOAT, vertexCount, location + "." + attribute.first);
                }
                else if (attribute.first != "POSITION")
                {
                    warning("AttributeIgnored", location + "." + attribute.first, "Vertex attribute is not consumed by the renderer.");
                }
            }
            if (!primitive.attributes.contains("NORMAL"))
            {
                warning("MissingNormals", location, "Missing normals use a fixed fallback, not generated smooth normals.");
            }
            if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size()) &&
                model.materials[primitive.material].normalTexture.index >= 0 &&
                !primitive.attributes.contains("TANGENT"))
            {
                warning("MissingTangents", location, "PT ignores the normal map without tangents; GBuffer uses a fallback frame.");
            }
            if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size()) &&
                !primitive.attributes.contains("TEXCOORD_0"))
            {
                const tinygltf::Material& material = model.materials[primitive.material];
                if (material.pbrMetallicRoughness.baseColorTexture.index >= 0 ||
                    material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0 ||
                    material.normalTexture.index >= 0 || material.emissiveTexture.index >= 0 || material.occlusionTexture.index >= 0)
                {
                    warning("MissingTexcoords", location, "Textured material has no TEXCOORD_0; a constant zero UV is used.");
                }
            }
            if (primitive.indices < 0 || primitive.indices >= static_cast<int>(model.accessors.size()))
            {
                error("MissingIndices", location, "A supported index accessor is required.");
                continue;
            }
            const tinygltf::Accessor& indices = model.accessors[primitive.indices];
            const int component = indices.componentType;
            if (component != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE && component != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                component != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                error("UnsupportedIndices", location, "Indices must be unsigned 8-, 16-, or 32-bit values.");
                continue;
            }
            if (!accessorValid(primitive.indices, TINYGLTF_TYPE_SCALAR, component, 0, location + ".indices"))
            {
                continue;
            }
            if (indices.count % 3 != 0)
            {
                error("InvalidTriangleCount", location, "Triangle index count must be a multiple of three.");
            }
            const unsigned char* data = GetAccessorData(model, indices);
            const size_t bytes = tinygltf::GetComponentSizeInBytes(component);
            for (size_t i = 0; i < indices.count; ++i)
            {
                uint32_t index = 0;
                std::memcpy(&index, data + i * bytes, bytes);
                if (index >= vertexCount)
                {
                    error("IndexOutOfRange", location, "Triangle index references a vertex outside POSITION.");
                    break;
                }
            }
        }
    }
    for (size_t i = 0; i < model.nodes.size(); ++i)
    {
        const tinygltf::Node& node = model.nodes[i];
        if (node.mesh < -1 || node.mesh >= static_cast<int>(model.meshes.size()))
        {
            error("InvalidMesh", "nodes[" + std::to_string(i) + "]", "Node mesh index is outside the asset.");
        }
        if (node.skin >= 0)
        {
            warning("SkinIgnored", "nodes[" + std::to_string(i) + "]", "Skinning is ignored; undeformed vertices are imported.");
        }
    }
    std::vector<uint8_t> nodeState(model.nodes.size(), 0);
    std::function<void(int)> visitNode;
    visitNode = [&](int index)
    {
        if (index < 0 || index >= static_cast<int>(model.nodes.size()))
        {
            error("InvalidNode", "nodes", "Scene or child node index is outside the asset.");
            return;
        }
        if (nodeState[index] == 1)
        {
            error("NodeCycle", "nodes[" + std::to_string(index) + "]", "Cyclic node hierarchies cannot be imported.");
            return;
        }
        if (nodeState[index] == 2)
        {
            return;
        }
        nodeState[index] = 1;
        for (int child : model.nodes[index].children)
        {
            visitNode(child);
        }
        nodeState[index] = 2;
    };
    for (size_t i = 0; i < model.nodes.size(); ++i)
    {
        visitNode(static_cast<int>(i));
    }
    for (const tinygltf::Scene& scene : model.scenes)
    {
        for (int root : scene.nodes)
        {
            visitNode(root);
        }
    }
    if (model.defaultScene < -1 || model.defaultScene >= static_cast<int>(model.scenes.size()))
    {
        error("InvalidScene", "scene", "Default scene index is outside the asset.");
    }
    return valid;
}

static bool LoadGltfModel(const std::string& path, tinygltf::Model& model, std::string& message,
                          std::vector<GltfImportDiagnostic>& diagnostics)
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
    if (!loaded)
    {
        return false;
    }
    const bool valid = ValidateGltfImport(model, diagnostics);
    for (const GltfImportDiagnostic& diagnostic : diagnostics)
    {
        const std::string text = "[glTF][" + std::string(diagnostic.severity == GltfDiagnosticSeverity::Error ? "ERROR" : "WARNING") +
                                 "][" + diagnostic.code + "] " + path + " " + diagnostic.location + ": " + diagnostic.message + "\n";
        OutputDebugStringA(text.c_str());
        std::fputs(text.c_str(), stderr);
        if (diagnostic.severity == GltfDiagnosticSeverity::Error && valid == false)
        {
            message = diagnostic.code + ": " + diagnostic.location + ": " + diagnostic.message;
        }
    }
    return valid;
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
        destination.normalTextureScale = static_cast<float>(material.normalTexture.scale);
        for (int channel = 0; channel < 3; ++channel)
        {
            destination.emissiveFactor[channel] = static_cast<float>(material.emissiveFactor[channel]);
        }
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
    int whiteEmissiveTexture = -1;
    for (GltfMaterial& material : outMesh.materials)
    {
        if (material.emissiveTexIndex < 0 &&
            (material.emissiveFactor[0] > 0.0f || material.emissiveFactor[1] > 0.0f || material.emissiveFactor[2] > 0.0f))
        {
            if (whiteEmissiveTexture < 0)
            {
                whiteEmissiveTexture = static_cast<int>(outMesh.textures.size());
                GltfTextureData texture = {};
                texture.width = texture.height = 1;
                texture.component = 4;
                texture.pixels = {255, 255, 255, 255};
                outMesh.textures.push_back(std::move(texture));
            }
            material.emissiveTexIndex = whiteEmissiveTexture;
        }
    }
}

bool LoadGltfMesh(const std::string& path, GltfMeshData& outMesh, std::vector<GltfImportDiagnostic>* diagnostics)
{
    outMesh = {};
    tinygltf::Model model;
    std::string message;
    std::vector<GltfImportDiagnostic> importDiagnostics;
    const bool loaded = LoadGltfModel(path, model, message, importDiagnostics);
    if (diagnostics != nullptr)
    {
        *diagnostics = importDiagnostics;
    }
    if (!loaded)
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
    if (!LoadGltfModel(path, impl->model, result.message, result.diagnostics))
    {
        result.status = GltfSceneAssetLoadStatus::FileLoadFailed;
        for (const GltfImportDiagnostic& diagnostic : result.diagnostics)
        {
            if (diagnostic.severity == GltfDiagnosticSeverity::Error)
            {
                result.status = GltfSceneAssetLoadStatus::UnsupportedData;
                break;
            }
        }
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

Engine::GltfNodeMeshStatus
Engine::GltfSceneAsset::ExtractNodeMesh(uint32_t requestedNodeIndex, GltfMeshData& outMesh, std::string& message) const
{
    outMesh = {};
    message.clear();
    if (!m_impl)
    {
        message = "The glTF scene asset is invalid.";
        return GltfNodeMeshStatus::InvalidAsset;
    }

    const tinygltf::Model& model = m_impl->model;
    if (requestedNodeIndex >= model.nodes.size())
    {
        message = "The glTF node index is outside the asset.";
        return GltfNodeMeshStatus::NodeNotFound;
    }

    bool found = false;
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
        if (static_cast<uint32_t>(nodeIndex) == requestedNodeIndex)
        {
            found = true;
            if (node.mesh < 0 || node.mesh >= static_cast<int>(model.meshes.size()))
            {
                conversionSucceeded = false;
                return;
            }
            for (const tinygltf::Primitive& primitive : model.meshes[node.mesh].primitives)
            {
                if (!AppendPrimitive(model, primitive, nodeTransform, outMesh, firstMaterialIndex))
                {
                    conversionSucceeded = false;
                    break;
                }
            }
            return;
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
    if (!found)
    {
        message = "The glTF node is not reachable from the default scene.";
        return GltfNodeMeshStatus::NodeNotFound;
    }
    if (!conversionSucceeded || outMesh.vertices.empty() || outMesh.indices.empty())
    {
        outMesh = {};
        message = "The glTF node mesh could not be converted.";
        return GltfNodeMeshStatus::MeshConversionFailed;
    }
    outMesh.materialIndex = firstMaterialIndex;
    CopyMaterialsAndTextures(model, outMesh);
    return GltfNodeMeshStatus::Success;
}
