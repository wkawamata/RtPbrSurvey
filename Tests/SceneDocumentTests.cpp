#include "Scene/SceneDocument.h"
#include "Scene/SceneDocumentJson.h"
#include "Scene/SceneGraph.h"

#include <DirectXMath.h>
#include <cmath>
#include <filesystem>
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

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool MatricesEqual(const DirectX::XMFLOAT4X4& lhs, const DirectX::XMFLOAT4X4& rhs)
{
    const float* lhsElements = &lhs._11;
    const float* rhsElements = &rhs._11;
    for (size_t index = 0; index < 16; ++index)
    {
        if (!NearlyEqual(lhsElements[index], rhsElements[index]))
        {
            return false;
        }
    }
    return true;
}

bool TestEmptyDocumentDefaults()
{
    const RtPbrSurvey::SceneDocument document =
        RtPbrSurvey::CreateEmptySceneDocument("scene-reflection-lab", "Reflection Lab");

    bool passed = true;
    passed &= Check(document.schemaVersion == RtPbrSurvey::SceneDocument::kSchemaVersion, "schema version default");
    passed &= Check(document.sceneId == "scene-reflection-lab", "scene ID is retained");
    passed &= Check(document.name == "Reflection Lab", "scene name is retained");
    passed &=
        Check(document.assets.empty() && document.materials.empty() && document.nodes.empty(), "new document is empty");
    passed &=
        Check(document.camera.position.z == -5.0f && document.camera.verticalFovDegrees == 60.0f, "camera defaults");
    passed &= Check(document.environment.source == RtPbrSurvey::SceneEnvironmentSource::Procedural &&
                        document.environment.iblEnabled,
                    "environment defaults");
    return passed;
}

bool TestReflectionLabFixtureData()
{
    RtPbrSurvey::SceneDocument document =
        RtPbrSurvey::CreateEmptySceneDocument("scene-reflection-lab", "Reflection Lab");
    document.renderPresetPath = "../../RenderPresets/deferred-reference.json";
    document.assets.push_back({"helmet", "../../Models/DamagedHelmet/DamagedHelmet.gltf"});
    document.materials.push_back({"metal", "Polished Metal", {0.8f, 0.8f, 0.8f, 1.0f}, 1.0f, 0.15f});
    document.materials.push_back({"floor", "Rough Floor", {0.3f, 0.3f, 0.3f, 1.0f}, 0.0f, 0.8f});

    RtPbrSurvey::SceneNode group;
    group.id = "group";
    group.name = "Test Group";

    RtPbrSurvey::SceneNode helmet;
    helmet.id = "helmet-01";
    helmet.name = "Helmet";
    helmet.parentId = group.id;
    helmet.type = RtPbrSurvey::SceneNodeType::Gltf;
    helmet.assetId = "helmet";
    helmet.transform.translation = {-1.5f, 1.0f, 0.0f};

    RtPbrSurvey::SceneNode sphere;
    sphere.id = "sphere-01";
    sphere.name = "Metal Sphere";
    sphere.parentId = group.id;
    sphere.type = RtPbrSurvey::SceneNodeType::Primitive;
    sphere.primitive.kind = RtPbrSurvey::ScenePrimitiveKind::Sphere;
    sphere.primitive.radius = 0.5f;
    sphere.materialId = "metal";
    sphere.transform.translation = {1.5f, 0.5f, 0.0f};

    RtPbrSurvey::SceneNode floor;
    floor.id = "floor-01";
    floor.name = "Floor";
    floor.type = RtPbrSurvey::SceneNodeType::Primitive;
    floor.primitive.kind = RtPbrSurvey::ScenePrimitiveKind::Plane;
    floor.primitive.width = 10.0f;
    floor.primitive.depth = 10.0f;
    floor.materialId = "floor";

    document.nodes = {group, helmet, sphere, floor};

    bool passed = true;
    passed &= Check(RtPbrSurvey::HasUniqueSceneDocumentIds(document), "fixture uses unique IDs");
    passed &= Check(document.nodes.size() == 4 && document.nodes[1].parentId == "group" &&
                        document.nodes[2].parentId == "group",
                    "fixture preserves parent relationships");
    passed &= Check(document.nodes[2].primitive.kind == RtPbrSurvey::ScenePrimitiveKind::Sphere &&
                        document.nodes[2].materialId == "metal",
                    "primitive material binding");
    passed &= Check(document.nodes[3].primitive.kind == RtPbrSurvey::ScenePrimitiveKind::Plane &&
                        document.nodes[3].primitive.width == 10.0f && document.nodes[3].primitive.depth == 10.0f,
                    "plane dimensions");
    return passed;
}

bool TestDuplicateIdsAreRejected()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("scene-duplicate", "Duplicate");
    document.assets = {{"shared", "Models/one.gltf"}, {"shared", "Models/two.gltf"}};
    return Check(!RtPbrSurvey::HasUniqueSceneDocumentIds(document), "duplicate asset IDs are rejected");
}

bool TestJsonRoundTrip()
{
    RtPbrSurvey::SceneDocument document =
        RtPbrSurvey::CreateEmptySceneDocument("scene-reflection-lab", "Reflection Lab");
    document.renderPresetPath = "../../RenderPresets/deferred-reference.json";
    document.assets.push_back({"helmet", "../../Models/DamagedHelmet/DamagedHelmet.gltf"});
    document.materials.push_back({"metal", "Polished Metal", {0.8f, 0.8f, 0.8f, 1.0f}, 1.0f, 0.15f});

    RtPbrSurvey::SceneNode helmet;
    helmet.id = "helmet-01";
    helmet.name = "Helmet";
    helmet.type = RtPbrSurvey::SceneNodeType::Gltf;
    helmet.assetId = "helmet";

    RtPbrSurvey::SceneNode sphere;
    sphere.id = "sphere-01";
    sphere.name = "Metal Sphere";
    sphere.type = RtPbrSurvey::SceneNodeType::Primitive;
    sphere.primitive.kind = RtPbrSurvey::ScenePrimitiveKind::Sphere;
    sphere.materialId = "metal";
    document.nodes = {helmet, sphere};

    std::string json;
    std::string error;
    RtPbrSurvey::SceneDocument restored;
    bool passed = Check(RtPbrSurvey::SerializeSceneDocument(document, json, &error), "scene document serializes");
    passed &= Check(error.empty(), "successful serialize clears error");
    passed &= Check(RtPbrSurvey::DeserializeSceneDocument(json, restored, &error), "scene document deserializes");
    passed &= Check(error.empty(), "successful deserialize clears error");
    passed &= Check(restored.sceneId == document.sceneId && restored.renderPresetPath == document.renderPresetPath,
                    "scene identity and preset round-trip");
    passed &= Check(restored.nodes.size() == 2 && restored.nodes[0].assetId == "helmet" &&
                        restored.nodes[1].materialId == "metal",
                    "node references round-trip");
    return passed;
}

bool TestInvalidJsonDoesNotReplaceDocument()
{
    RtPbrSurvey::SceneDocument existing = RtPbrSurvey::CreateEmptySceneDocument("existing", "Existing");
    existing.renderPresetPath = "RenderPresets/default.json";
    std::string error;
    const std::string invalid =
        R"({"schemaVersion":2,"sceneId":"invalid","name":"Invalid","renderPreset":"preset.json","assets":[],"materials":[],"nodes":[],"camera":{},"environment":{}})";
    return Check(!RtPbrSurvey::DeserializeSceneDocument(invalid, existing, &error), "unknown schema is rejected") &&
           Check(!error.empty(), "invalid scene JSON reports an error") &&
           Check(existing.sceneId == "existing", "invalid scene JSON does not replace output");
}

bool TestFixtureFileLoadSaveLoad()
{
    const std::filesystem::path fixturePath =
        std::filesystem::path(__FILE__).parent_path() / "Fixtures" / "Scenes" / "reflection-lab" / "scene.json";
    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() / "RtPbrSurvey-SceneDocumentTests-scene.json";
    std::filesystem::remove(outputPath);
    std::filesystem::remove(outputPath.string() + ".tmp");

    RtPbrSurvey::SceneDocument loaded;
    RtPbrSurvey::SceneDocument restored;
    std::string error;
    bool passed = Check(RtPbrSurvey::LoadSceneDocumentFile(fixturePath.string(), loaded, &error),
                        "fixture scene document loads");
    passed &= Check(RtPbrSurvey::SaveSceneDocumentFile(outputPath.string(), loaded, &error),
                    "scene document saves atomically");
    passed &= Check(RtPbrSurvey::LoadSceneDocumentFile(outputPath.string(), restored, &error),
                    "saved scene document reloads");
    passed &= Check(restored.sceneId == loaded.sceneId && restored.nodes.size() == loaded.nodes.size(),
                    "fixture load-save-load preserves document");
    std::filesystem::remove(outputPath);
    std::filesystem::remove(outputPath.string() + ".tmp");
    return passed;
}

bool TestSaveAsRebasesRelativePaths()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("rebase", "Rebase");
    document.renderPresetPath = "../RenderPresets/reference.json";
    document.assets = {{"helmet", "Models/Helmet.gltf"}};
    const std::filesystem::path oldDirectory = std::filesystem::path("C:/TestAssets/Scenes/Source");
    const std::filesystem::path newDirectory = std::filesystem::path("C:/TestAssets/Scenes/Archive/Copy");
    std::string error;
    bool passed = Check(RtPbrSurvey::RebaseSceneDocumentPaths(document, oldDirectory, newDirectory, &error),
                        "Save As paths rebase");
    passed &= Check(document.renderPresetPath == "../../RenderPresets/reference.json",
                    "render preset remains bound to its original file");
    passed &= Check(document.assets[0].path == "../../Source/Models/Helmet.gltf",
                    "asset remains bound to its original file");
    return passed;
}

bool TestSceneGraphEvaluatesUnorderedThreeLevelHierarchy()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("graph", "Graph");

    RtPbrSurvey::SceneNode grandchild;
    grandchild.id = "grandchild";
    grandchild.parentId = "child";
    grandchild.transform.translation = {1.0f, 0.0f, 0.0f};

    RtPbrSurvey::SceneNode child;
    child.id = "child";
    child.parentId = "root";
    child.transform.translation = {0.0f, 2.0f, 0.0f};
    child.transform.rotation.z = std::sqrt(0.5f);
    child.transform.rotation.w = std::sqrt(0.5f);

    RtPbrSurvey::SceneNode root;
    root.id = "root";
    root.transform.translation = {10.0f, 0.0f, 0.0f};
    root.transform.scale = {2.0f, 2.0f, 2.0f};

    document.nodes = {grandchild, child, root};
    RtPbrSurvey::SceneGraphEvaluation evaluation;
    std::string error;
    if (!Check(RtPbrSurvey::EvaluateSceneGraph(document, evaluation, &error), "three-level scene graph evaluates"))
    {
        return false;
    }
    const DirectX::XMFLOAT4X4* world = evaluation.FindWorld("grandchild", document);
    return Check(world != nullptr, "grandchild world is available") &&
           Check(NearlyEqual(world->_41, 10.0f) && NearlyEqual(world->_42, 6.0f) && NearlyEqual(world->_43, 0.0f),
                 "local TRS uses row-vector parent composition");
}

bool TestSceneGraphRejectsInvalidParentsWithoutChangingOutput()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("invalid", "Invalid");
    RtPbrSurvey::SceneNode node;
    node.id = "node";
    node.parentId = "missing";
    document.nodes.push_back(node);

    RtPbrSurvey::SceneGraphEvaluation evaluation;
    evaluation.worldTransforms.resize(1);
    std::string error;
    bool passed = Check(!RtPbrSurvey::EvaluateSceneGraph(document, evaluation, &error), "missing parent is rejected");
    passed &= Check(!error.empty(), "missing parent reports an error");
    passed &= Check(evaluation.worldTransforms.size() == 1, "failed graph evaluation preserves output");

    document.nodes[0].parentId = "node";
    passed &= Check(!RtPbrSurvey::EvaluateSceneGraph(document, evaluation, &error), "self parent is rejected");

    RtPbrSurvey::SceneNode second;
    second.id = "second";
    second.parentId = "node";
    document.nodes[0].parentId = "second";
    document.nodes.push_back(second);
    passed &= Check(!RtPbrSurvey::EvaluateSceneGraph(document, evaluation, &error), "parent cycle is rejected");
    return passed;
}

bool TestReparentPreservesWorldTransform()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("reparent", "Reparent");
    RtPbrSurvey::SceneNode firstParent;
    firstParent.id = "first";
    firstParent.transform.translation = {2.0f, 0.0f, 0.0f};

    RtPbrSurvey::SceneNode secondParent;
    secondParent.id = "second";
    secondParent.transform.translation = {-4.0f, 3.0f, 0.0f};

    RtPbrSurvey::SceneNode child;
    child.id = "child";
    child.parentId = "first";
    child.transform.translation = {1.0f, 2.0f, 0.0f};
    document.nodes = {firstParent, secondParent, child};

    RtPbrSurvey::SceneGraphEvaluation before;
    std::string error;
    bool passed = Check(RtPbrSurvey::EvaluateSceneGraph(document, before, &error), "original graph evaluates");
    const DirectX::XMFLOAT4X4 originalWorld = *before.FindWorld("child", document);
    passed &= Check(RtPbrSurvey::ReparentSceneNodePreservingWorld(document, "child", std::string("second"), &error),
                    "reparent succeeds");
    RtPbrSurvey::SceneGraphEvaluation after;
    passed &= Check(RtPbrSurvey::EvaluateSceneGraph(document, after, &error), "reparented graph evaluates");
    const DirectX::XMFLOAT4X4* reparentedWorld = after.FindWorld("child", document);
    passed &= Check(document.nodes[2].parentId == "second", "new parent is stored");
    passed &= Check(reparentedWorld != nullptr && MatricesEqual(originalWorld, *reparentedWorld),
                    "reparent preserves child world transform");
    return passed;
}

} // namespace

int main()
{
    const bool passed = TestEmptyDocumentDefaults() && TestReflectionLabFixtureData() && TestDuplicateIdsAreRejected() &&
                        TestJsonRoundTrip() && TestInvalidJsonDoesNotReplaceDocument() && TestFixtureFileLoadSaveLoad() &&
                        TestSaveAsRebasesRelativePaths() &&
                        TestSceneGraphEvaluatesUnorderedThreeLevelHierarchy() &&
                        TestSceneGraphRejectsInvalidParentsWithoutChangingOutput() && TestReparentPreservesWorldTransform();
    if (passed)
    {
        std::cout << "SceneDocument tests passed.\n";
        return 0;
    }

    return 1;
}
