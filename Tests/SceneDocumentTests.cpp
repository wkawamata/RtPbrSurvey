#include "Scene/SceneDocument.h"

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

} // namespace

int main()
{
    const bool passed = TestEmptyDocumentDefaults() && TestReflectionLabFixtureData() && TestDuplicateIdsAreRejected();
    if (passed)
    {
        std::cout << "SceneDocument tests passed.\n";
        return 0;
    }

    return 1;
}
