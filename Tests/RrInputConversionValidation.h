#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace RrInputTests
{

inline bool Near(double actual, double expected, double absolute, double relative)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= absolute + relative * std::abs(expected);
}

// Independent double-precision polynomial evaluation of the guide's rational
// BRDF approximation. All remaining channels have explicit physical expectations.
inline void ValidateRrInputConversion(const float* values, unsigned int count)
{
    const int statuses[32] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 3, 3,
                              3, 3, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 2, 1, 2, 3};
    for (unsigned int index = 0; index < count; ++index)
    {
        const unsigned int test = index % 32;
        const float* actual = values + index * 24;
        for (unsigned int channel = 0; channel < 24; ++channel)
        {
            if (!std::isfinite(actual[channel]))
                throw std::runtime_error("Nonfinite RR conversion output");
            if (channel != 20 && statuses[test] != 0 && actual[channel] != 0)
                throw std::runtime_error("Rejected RR pixel leaked input");
        }
        if (actual[20] != statuses[test] || actual[21] != 0 || actual[22] != 0 || actual[23] != 0)
            throw std::runtime_error("RR conversion status mismatch at case " + std::to_string(test));
        if (statuses[test] != 0)
            continue;
        const double metallic = test == 1 || test == 3 ? 1 : test == 2 ? .35 : 0;
        const double roughness = test == 1 ? .2 : test == 2 ? .55 : test == 4 ? 0 : test == 5 ? 1 : .5;
        const double noV = test == 1 ? .6 : test == 3 || test == 6 ? 0 : 1;
        const double a = roughness * roughness, n = noV;
        const double biasNumerator = .99044 - 1.28514 * n + a * (1.29678 - .755907 * n);
        const double biasDenominator = 1 + 2.92338 * n + 59.4188 * n * n * n +
                                       a * (20.3225 - 27.0302 * n + 222.592 * n * n * n) +
                                       a * a * a * (121.563 + 626.13 * n + 316.627 * n * n * n);
        const double scaleNumerator = .0365463 + 3.32707 * n + a * (9.0632 - 9.04756 * n);
        const double scaleDenominator = 1 + 3.59685 * n * n - 1.36772 * n * n * n +
                                        a * (9.04401 - 16.3174 * n * n + 9.22949 * n * n * n) +
                                        a * a * a * (5.56589 + 19.7886 * n * n - 20.2123 * n * n * n);
        const double bases[4][3] = {{.8, .6, .2}, {.9, .5, .1}, {.7, .3, .1}, {0, 0, 0}};
        const double* base = bases[test < 4 ? test : 0];
        const double f0Green = test == 1 ? .5 : test == 2 ? .131 : test == 3 ? 0 : .04;
        const double bias = std::max(0.0, biasNumerator / biasDenominator * std::min(1.0, f0Green * 50));
        const double scale = std::max(0.0, scaleNumerator / scaleDenominator);
        for (unsigned int channel = 0; channel < 3; ++channel)
        {
            const double f0Partial[] = {.271, .131, .061};
            const double f0 = test == 1 ? base[channel] : test == 2 ? f0Partial[channel] : test == 3 ? 0 : .04;
            if (!Near(actual[4 + channel], base[channel] * (1 - metallic), 2e-6, 2e-5) ||
                !Near(actual[8 + channel], f0 * scale + bias, 2e-6, 2e-5))
                throw std::runtime_error("RR reflectance mismatch at case " + std::to_string(test));
        }
        if (actual[0] != .25f || actual[1] != 3 || actual[2] != 70000 || actual[3] != 1 || actual[7] != 1 ||
            actual[11] != 1 || actual[12] != 0 || actual[13] != 0 || actual[14] != 1 ||
            !Near(actual[15], roughness, 1e-7, 1e-7) || actual[16] != 12 || !Near(actual[17], .1, 1e-7, 1e-7) ||
            !Near(actual[18], .2, 1e-7, 1e-7) || actual[19] != (test == 7 ? 70000 : 2))
            throw std::runtime_error("RR radiance/normal/roughness/depth/motion/distance mismatch");
    }
    std::cout << "Validated 32 RR numeric conversion cases; no SDK evaluate or distance correlation claim.\n";
}

} // namespace RrInputTests
