#pragma once

#include <memory>

namespace Engine
{
    class SampleScene;

    class SceneFactory
    {
    public:
        static std::unique_ptr<SampleScene> CreateCornellBox();
        static std::unique_ptr<SampleScene> CreateHostPrimitiveMeshes();
        static std::unique_ptr<SampleScene> CreateHybridReflectionEstimatorTest();
    };
}
