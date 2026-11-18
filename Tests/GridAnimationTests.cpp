#include "Scene/SampleScene.h"

#include <cmath>
#include <iostream>

int main()
{
    const Engine::GltfAssetDesc asset = {"Test", nullptr, -10.0f, 0.5f};
    Engine::SampleSceneUpdateContext context;
    context.isPlaying = true;
    context.meshScale = 0.5f;
    Engine::GltfGridBenchmarkScene reference(asset, 4);
    reference.Reset();
    for (int i = 0; i < 2400; ++i)
    {
        reference.Update(1.0f / 60.0f, context);
    }
    // Zero denotes alternating 30/120 FPS frame intervals for 40 seconds.
    for (int fps : {30, 120, 240, 0})
    {
        Engine::GltfGridBenchmarkScene scene(asset, 4);
        scene.Reset();
        const int frameCount = fps == 0 ? 1920 : fps * 40;
        for (int i = 0; i < frameCount; ++i)
        {
            const float elapsed = fps == 0 ? (i % 2 == 0 ? 1.0f / 30.0f : 1.0f / 120.0f) : 1.0f / fps;
            scene.Update(elapsed, context);
        }
        for (size_t i = 0; i < scene.GetScene().instances.size(); ++i)
        {
            const auto& a = scene.GetScene().instances[i].world;
            const auto& b = reference.GetScene().instances[i].world;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    if (std::abs(a.m[row][col] - b.m[row][col]) > 0.003f)
                    {
                        return 1;
                    }
                }
            }
        }
    }
    context.isPlaying = false;
    const auto before = reference.GetScene().instances[0].world;
    reference.Update(10.0f, context);
    const auto after = reference.GetScene().instances[0].world;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            if (before.m[row][col] != after.m[row][col])
            {
                return 1;
            }
        }
    }
    std::cout << "Grid animation FPS independence and pause tests passed.\n";
    return 0;
}
