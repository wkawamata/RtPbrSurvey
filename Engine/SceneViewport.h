#pragma once

#include <algorithm>

namespace Engine
{
    // A zero width or height restores full-window scene output. UI stays full-window.
    struct SceneViewport
    {
        unsigned int x = 0;
        unsigned int y = 0;
        unsigned int width = 0;
        unsigned int height = 0;
    };

    inline SceneViewport ResolveSceneViewport(SceneViewport requested, unsigned int windowWidth,
                                             unsigned int windowHeight)
    {
        if (windowWidth == 0 || windowHeight == 0 || requested.width == 0 || requested.height == 0)
        {
            return {0, 0, windowWidth, windowHeight};
        }
        requested.x = (std::min)(requested.x, windowWidth - 1);
        requested.y = (std::min)(requested.y, windowHeight - 1);
        requested.width = (std::min)(requested.width, windowWidth - requested.x);
        requested.height = (std::min)(requested.height, windowHeight - requested.y);
        return requested;
    }
}
