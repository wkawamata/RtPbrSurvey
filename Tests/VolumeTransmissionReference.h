#pragma once

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <string>

namespace VolumeTransmissionTests
{
inline void RequireNear(double actual, double expected)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0002 * (1.0 + std::abs(expected)))
    {
        throw std::runtime_error("Volume transport contract mismatch: actual=" + std::to_string(actual) +
            " expected=" + std::to_string(expected));
    }
}

inline void Validate(const float* values)
{
    const double incidentIors[] = {1, 1, 1.5, 1.5, 1, 1.33, 2};
    const double transmittedIors[] = {1, 1.5, 1, 1.33, 2, 1, 1};
    const double cosines[] = {1, .8, .6, 0};
    const double colors[7][3] = {{1, 1, 1}, {.2, .4, .8}, {0, 0, 0}, {0, .5, 1},
        {.01, .1, .9}, {.999, .001, .5}, {1, 1, 1}};
    const double distances[] = {1, INFINITY, 2, 2, .5, 1, 1e-8};
    const double lengths[] = {0, .5, 1, 2};
    for (unsigned int test = 0; test < 28; ++test)
    {
        const unsigned int pair = test / 4;
        const unsigned int angle = test % 4;
        double cosine = cosines[angle];
        if (pair == 2 && (angle == 1 || angle == 2))
        {
            cosine = std::sqrt(5.0) / 3.0 + (angle == 1 ? .001 : -.001);
        }
        const double ni = incidentIors[pair];
        const double nt = transmittedIors[pair];
        const double sineI = std::sqrt(1 - cosine * cosine);
        const double sineT = ni / nt * sineI;
        const bool tir = ni != nt && sineT >= 1;
        const double cosineT = std::sqrt(std::max(0.0, 1 - sineT * sineT));
        double fresnel = 0;
        if (tir)
        {
            fresnel = 1;
        }
        else if (ni != nt)
        {
            const double rs = (ni * cosine - nt * cosineT) / (ni * cosine + nt * cosineT);
            const double rp = (nt * cosine - ni * cosineT) / (nt * cosine + ni * cosineT);
            fresnel = (rs * rs + rp * rp) / 2;
        }
        unsigned int reflectionCount = 0;
        for (unsigned int index = 0; index < 1024; ++index)
        {
            const float* value = values + (test * 1024 + index) * 16;
            const bool reflected = value[6] == 1;
            const double random = (index + .5) / 1024;
            if ((value[6] != 0 && value[6] != 1) || value[7] != 1 ||
                (std::abs(random - fresnel) > .0002 && reflected != (random < fresnel)))
            {
                throw std::runtime_error("Invalid volume event selection");
            }
            reflectionCount += reflected ? 1 : 0;
            RequireNear(value[0], reflected ? sineI : sineT);
            RequireNear(value[1], 0);
            RequireNear(value[2], reflected ? cosine : -cosineT);
            RequireNear(value[3], fresnel);
            RequireNear(value[4], reflected ? 1 : ni * ni / (nt * nt));
            RequireNear(value[5], reflected ? fresnel : 1 - fresnel);
            RequireNear(value[0] * value[0] + value[1] * value[1] + value[2] * value[2], 1);
            for (unsigned int channel = 0; channel < 3; ++channel)
            {
                const double expected = lengths[angle] == 0 || std::isinf(distances[pair]) ? 1 :
                    std::pow(colors[pair][channel], lengths[angle] / distances[pair]);
                RequireNear(value[8 + channel], expected);
            }
            RequireNear(value[11], 1);
            RequireNear(value[12], 1);
            if (value[13] != 0 || value[14] != 0 || value[15] != (tir ? 1 : 0))
            {
                throw std::runtime_error("Volume invalid-input or TIR contract mismatch");
            }
        }
        if (std::abs(reflectionCount / 1024.0 - fresnel) > .002)
        {
            throw std::runtime_error("Volume Fresnel event frequency mismatch");
        }
    }
    std::cout << "Validated 28672 smooth volume samples, Snell/TIR, eta cancellation and RGB absorption.\n";
}
}
