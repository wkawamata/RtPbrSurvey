#include <tinyexr.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
double CornellVisibility(double x, double y)
{
    unsigned int misses = 0;
    for (unsigned int i = 0; i < 16; ++i)
    {
        unsigned int bits = i;
        unsigned int reversed = 0;
        for (unsigned int bit = 0; bit < 32; ++bit)
        {
            reversed = (reversed << 1) | (bits & 1u);
            bits >>= 1;
        }
        const double u = (i + 0.5) / 16.0;
        const double phi = 6.283185307179586 * (reversed / 4294967296.0);
        const double distance = 2.999 / std::sqrt(1.0 - u);
        const double hitX = x - distance * std::sqrt(u) * std::cos(phi);
        const double hitY = y + distance * std::sqrt(u) * std::sin(phi);
        if (std::abs(hitX) < 1.5 && std::abs(hitY) < 1.5)
        {
            ++misses;
        }
    }
    return misses / 16.0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        const double center = CornellVisibility(0, 0);
        if (!(center > 0 && center < 1) || CornellVisibility(100, 100) != 0)
        {
            return 1;
        }
        std::cout << "Cornell aperture reference passed: " << center << '\n';
        return 0;
    }
    if (argc != 3 && argc != 4)
    {
        return 2;
    }
    const std::string mode = argv[2];
    const bool exportPixels = mode == "export-rgba32f";
    const bool composite = mode == "composite-open" || mode == "composite-half" || mode == "composite-closed";
    const bool unchanged = mode == "unchanged";
    if (mode != "open" && mode != "closed" && mode != "unoccluded" && mode != "unsupported" &&
        !composite && !unchanged && !exportPixels)
    {
        return 2;
    }
    float* pixels = nullptr;
    int width = 0, height = 0;
    const char* error = nullptr;
    if (LoadEXR(&pixels, &width, &height, argv[1], &error) != TINYEXR_SUCCESS)
    {
        if (error)
        {
            std::cerr << error << '\n';
            FreeEXRErrorMessage(error);
        }
        return 1;
    }
    if (exportPixels)
    {
        if (argc != 4 || width <= 0 || height <= 0)
        {
            free(pixels);
            return 2;
        }
        std::ofstream output(argv[3], std::ios::binary);
        output.write(reinterpret_cast<const char*>(pixels),
                     static_cast<std::streamsize>(width) * height * 4 * sizeof(float));
        const bool success = output.good();
        free(pixels);
        std::cout << width << ' ' << height << '\n';
        return success ? 0 : 1;
    }
    if (width != 640 || height != 360)
    {
        free(pixels);
        return 1;
    }
    float* baseline = nullptr;
    if (composite || unchanged)
    {
        int referenceWidth = 0, referenceHeight = 0;
        if (argc != 4 || LoadEXR(&baseline, &referenceWidth, &referenceHeight, argv[3], &error) != TINYEXR_SUCCESS ||
            referenceWidth != width || referenceHeight != height)
        {
            free(pixels);
            free(baseline);
            if (error)
                FreeEXRErrorMessage(error);
            return 1;
        }
    }
    double maximum = 0, sum = 0, mean = 0;
    unsigned int different = 0, count = 0;
    if (unchanged)
    {
        float referenceMaximum = 0;
        for (int i = 0; i < width * height * 4; ++i)
        {
            if (!std::isfinite(pixels[i]) || !std::isfinite(baseline[i]) || pixels[i] != baseline[i])
            {
                free(pixels);
                free(baseline);
                std::cerr << "Unchanged control differs at component " << i << '\n';
                return 1;
            }
            if (i % 4 != 3)
                referenceMaximum = (std::max)(referenceMaximum, baseline[i]);
        }
        if (referenceMaximum <= 0.001f)
        {
            free(pixels);
            free(baseline);
            std::cerr << "Unchanged control lacks nonblack positive evidence\n";
            return 1;
        }
    }
    const double tanFov = std::tan(42.0 * 3.141592653589793 / 360.0);
    for (int y = 120; y < 240; ++y)
    {
        for (int x = 220; x < 420; ++x)
        {
            const double worldX = ((x + 0.5) / 640.0 * 2 - 1) * (640.0 / 360) * tanFov * 2.7;
            const double worldY = (1 - (y + 0.5) / 360.0 * 2) * tanFov * 2.7;
            const double visibility = CornellVisibility(worldX, worldY);
            const double expected = mode == "open" || mode == "composite-open" ? visibility :
                mode == "closed" || mode == "composite-closed" ? 0.0 :
                mode == "composite-half" ? 0.5 + 0.5 * visibility : 1.0;
            const float* pixel = pixels + (y * width + x) * 4;
            double deviation = 0;
            for (int channel = 0; channel < 3; ++channel)
            {
                if (!std::isfinite(pixel[channel]))
                {
                    free(pixels);
                    free(baseline);
                    return 1;
                }
                const double base = baseline ? baseline[(y * width + x) * 4 + channel] : 1.0;
                if (!std::isfinite(base))
                {
                    free(pixels);
                    free(baseline);
                    return 1;
                }
                const double wanted = mode == "unsupported" && channel == 1 ? 0.0 : expected * base;
                deviation = (std::max)(deviation, std::abs(pixel[channel] - wanted));
            }
            maximum = (std::max)(maximum, deviation);
            sum += deviation;
            mean += pixel[0];
            different += deviation > 0.0001 ? 1 : 0;
            ++count;
        }
    }
    std::ofstream preview(std::string(argv[1]) + ".ppm", std::ios::binary);
    preview << "P6\n" << width << ' ' << height << "\n255\n";
    for (int i = 0; i < width * height; ++i)
    {
        for (int channel = 0; channel < 3; ++channel)
        {
            const float value = pixels[i * 4 + channel];
            if (!std::isfinite(value))
            {
                free(pixels);
                free(baseline);
                return 1;
            }
            const unsigned char display = static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255 + 0.5f);
            preview.write(reinterpret_cast<const char*>(&display), 1);
        }
    }
    free(pixels);
    free(baseline);
    std::cout << "mode=" << mode << " mean=" << mean / count << " maxError=" << maximum << " meanError=" << sum / count
              << " mismatches=" << different << '/' << count << '\n';
    // Only aperture-edge float rounding is allowed; all-hit/all-miss/sentinel controls are exact.
    if (composite)
        return maximum > 0.002;
    return mode == "open" ? (sum / count > 0.002 || different > count / 100) : maximum != 0;
}
