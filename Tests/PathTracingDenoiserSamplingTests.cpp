#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>

#include "RrInputConversionValidation.h"

using Microsoft::WRL::ComPtr;

namespace
{
constexpr unsigned int kSamplesPerCase = 1024;
constexpr unsigned int kSampleCount = 28 * kSamplesPerCase;
constexpr double kPi = 3.141592653589793;

void Check(HRESULT result)
{
    if (FAILED(result))
    {
        throw std::runtime_error("D3D12 operation failed: " + std::to_string(result));
    }
}

struct Vector
{
    double x, y, z;
    Vector operator+(Vector other) const
    {
        return {x + other.x, y + other.y, z + other.z};
    }
    Vector operator*(double scale) const
    {
        return {x * scale, y * scale, z * scale};
    }
};

double Dot(Vector a, Vector b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vector Cross(Vector a, Vector b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vector Normalize(Vector a)
{
    return a * (1.0 / std::sqrt(Dot(a, a)));
}

Vector ReferenceNormal(Vector view, double alpha, double u, double v)
{
    const Vector stretched = Normalize({alpha * view.x, alpha * view.y, view.z});
    const double planarLength = std::hypot(stretched.x, stretched.y);
    const Vector tangent = planarLength > 0.0 ? Vector{-stretched.y / planarLength, stretched.x / planarLength, 0.0}
                                              : Vector{1.0, 0.0, 0.0};
    const Vector bitangent = Cross(stretched, tangent);
    const double diskX = std::sqrt(u) * std::cos(2.0 * kPi * v);
    const double blend = (1.0 + stretched.z) / 2.0;
    const double diskY =
        (1.0 - blend) * std::sqrt(std::max(0.0, 1.0 - diskX * diskX)) + blend * std::sqrt(u) * std::sin(2.0 * kPi * v);
    const Vector normal =
        tangent * diskX + bitangent * diskY + stretched * std::sqrt(std::max(0.0, 1.0 - diskX * diskX - diskY * diskY));
    return Normalize({alpha * normal.x, alpha * normal.y, std::max(0.0, normal.z)});
}

ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = heap;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags =
        heap == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(
        &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&result)));
    return result;
}

bool Near(double actual, double expected, double absolute = 0.0002, double relative = 0.002)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= absolute + relative * std::abs(expected);
}

void Validate(const float* values)
{
    constexpr double roughnesses[] = {0.0, 0.1, 0.35, 0.7, 1.0};
    constexpr double viewZs[] = {1.0, 0.8, 0.1, 0.0001};
    unsigned int belowSurface = 0;
    unsigned int normalIncidenceRoughInvalid = 0;
    double maxDirectionError = 0.0;
    for (unsigned int index = 0; index < kSampleCount; ++index)
    {
        const float* value = values + index * 16;
        for (unsigned int channel = 0; channel < 16; ++channel)
        {
            if (!std::isfinite(value[channel]))
                throw std::runtime_error("Non-finite shader output");
        }
        const unsigned int caseIndex = index / kSamplesPerCase;
        if (caseIndex >= 20)
        {
            for (unsigned int channel = 0; channel < 8; ++channel)
            {
                if (value[channel] != 0.0f)
                    throw std::runtime_error("Invalid VNDF input accepted");
            }
            if (value[12] != 0.0f)
                throw std::runtime_error("Invalid VNDF PDF accepted");
            continue;
        }
        const unsigned int localIndex = index % kSamplesPerCase;
        const double u = localIndex == 0      ? 0.0
                         : localIndex == 1023 ? static_cast<double>(0.99999f)
                                              : (localIndex % 32 + 0.5) / 32.0;
        const double v = localIndex == 0      ? 0.0
                         : localIndex == 1023 ? static_cast<double>(0.99999f)
                                              : (localIndex / 32 + 0.5) / 32.0;
        const double alpha = std::max(roughnesses[caseIndex / 4] * roughnesses[caseIndex / 4], 0.001);
        const double z = viewZs[caseIndex % 4];
        const Vector view = {std::sqrt(1.0 - z * z), 0.0, z};
        const Vector normal = ReferenceNormal(view, alpha, u, v);
        const Vector reflected = normal * (2.0 * Dot(view, normal)) + view * -1.0;
        const double denominator = normal.x * normal.x + normal.y * normal.y + alpha * alpha * normal.z * normal.z;
        const double distribution = alpha * alpha / (kPi * denominator * denominator);
        const double smithRatio = 2.0 / (z + std::sqrt(alpha * alpha + (1.0 - alpha * alpha) * z * z));
        const double normalPdf = distribution * smithRatio * Dot(view, normal);
        const bool valid = reflected.z > 0.0;
        const double directionPdf = valid ? normalPdf / (4.0 * Dot(view, normal)) : 0.0;
        const double expected[] = {normal.x,
                                   normal.y,
                                   normal.z,
                                   normalPdf,
                                   reflected.x,
                                   reflected.y,
                                   reflected.z,
                                   valid ? 1.0 : 0.0,
                                   std::sqrt(u) * std::cos(2.0 * kPi * v),
                                   std::sqrt(u) * std::sin(2.0 * kPi * v),
                                   std::sqrt(1.0 - u),
                                   std::sqrt(1.0 - u) / kPi,
                                   directionPdf,
                                   0.0,
                                   0.0,
                                   0.0};
        for (unsigned int channel = 0; channel < 16; ++channel)
        {
            if (!Near(value[channel], expected[channel]))
            {
                throw std::runtime_error("Reference mismatch at sample " + std::to_string(index) + " channel " +
                                         std::to_string(channel) + " actual " + std::to_string(value[channel]) +
                                         " expected " + std::to_string(expected[channel]));
            }
            if (channel >= 4 && channel <= 6)
                maxDirectionError = std::max(maxDirectionError, std::abs(value[channel] - expected[channel]));
        }
        const Vector gpuNormal = {value[0], value[1], value[2]};
        const Vector gpuDirection = {value[4], value[5], value[6]};
        if (!Near(Dot(gpuNormal, gpuNormal), 1.0) || !Near(Dot(gpuDirection, gpuDirection), 1.0) ||
            gpuNormal.z <= 0.0 || Dot(view, gpuNormal) <= 0.0 || value[7] != (value[6] > 0.0f ? 1.0f : 0.0f))
            throw std::runtime_error("VNDF support or validity mismatch");
        if (caseIndex % 4 == 0)
        {
            const double analyticZ = std::sqrt((1.0 - u) / (1.0 + (alpha * alpha - 1.0) * u));
            if (!Near(value[2], analyticZ))
                throw std::runtime_error("Normal-incidence CDF mismatch");
        }
        if (!valid)
            ++belowSurface;
        if (caseIndex == 16 && !valid)
            ++normalIncidenceRoughInvalid;
    }
    if (belowSurface == 0 || normalIncidenceRoughInvalid != 512)
        throw std::runtime_error("Below-surface rejection distribution mismatch");
    std::cout << "Validated " << kSampleCount << " HLSL samples; below-surface=" << belowSurface
              << "; maximum direction error=" << maxDirectionError << '\n';
}

void ValidatePacking(const float* values)
{
    const float expected[8][4] = {
        {1, 2, 3, 2},
        {1, 2, 3, 65504},
        {1, 2, 3, 0},
        {1, 2, 3, 0},
        {1, 2, 3, 0},
        {0, 0, 0, 2},
        {0, 65504, 4, 2},
        {1, 2, 3, 0},
    };
    for (unsigned int index = 0; index < kSampleCount; ++index)
    {
        for (unsigned int channel = 0; channel < 16; ++channel)
        {
            const float target = channel < 4 ? expected[index % 8][channel] : 0.0f;
            if (values[index * 16 + channel] != target)
            {
                throw std::runtime_error("Pinned SDK packing mismatch at case " + std::to_string(index % 8));
            }
        }
    }
    std::cout << "Validated pinned SDK RELAX packing: eight exact contract cases.\n";
}

void ValidateMaterialFactors(const float* values)
{
    const double cases[8][6] = {{.8, .6, .2, 0, .5, 1},
                                {0, 0, 0, 0, .5, .8},
                                {.9, .5, .1, 1, .2, .3},
                                {0, 0, 0, 1, .25, .001},
                                {.7, .3, .1, .35, .55, .75},
                                {.4, .6, .8, 0, 0, .5},
                                {1, 1, 1, 0, 1, 1},
                                {1, 1, 1, 0, .5, 0}};
    const double m1[2][2] = {{.99044, -1.28514}, {1.29678, -.755907}};
    const double m2[3][3] = {{1, 2.92338, 59.4188}, {20.3225, -27.0302, 222.592}, {121.563, 626.13, 316.627}};
    const double m3[2][2] = {{.0365463, 3.32707}, {9.0632, -9.04756}};
    const double m4[3][3] = {{1, 3.59685, -1.36772}, {9.04401, -16.3174, 9.22949}, {5.56589, 19.7886, -20.2123}};
    for (unsigned int index = 0; index < kSampleCount; ++index)
    {
        const double* material = cases[index % 8];
        const double metallic = material[3], roughness = material[4], noV = material[5];
        const double m = roughness * roughness;
        const double x2[] = {1, noV}, y2[] = {1, m};
        const double x3a[] = {1, noV, noV * noV * noV}, x3b[] = {1, noV * noV, noV * noV * noV};
        const double y3[] = {1, m, m * m * m};
        const auto evaluate = [](const auto& matrix, const double* x, const double* y, size_t count)
        {
            double result = 0;
            for (size_t row = 0; row < count; ++row)
                for (size_t column = 0; column < count; ++column)
                    result += matrix[row][column] * x[column] * y[row];
            return result;
        };
        const double bias = evaluate(m1, x2, y2, 2) / std::max(evaluate(m2, x3a, y3, 3), 1e-6);
        const double scale = evaluate(m3, x2, y2, 2) / std::max(evaluate(m4, x3b, y3, 3), 1e-6);
        const float* actual = values + index * 16;
        for (unsigned int channel = 0; channel < 16; ++channel)
            if (!std::isfinite(actual[channel]))
                throw std::runtime_error("Nonfinite material conversion");
        for (unsigned int channel = 0; channel < 3; ++channel)
        {
            const double reflectance = material[channel] * (1 - metallic);
            const double f0 = .04 * (1 - metallic) + material[channel] * metallic;
            const double environment = std::max(0.0, std::min(1.0, f0 * scale + bias));
            const double diffuseFactor = .02 + .98 * (1 - environment) * reflectance;
            const double specularFactor = .02 + .98 * environment * (.1 + .9 * roughness);
            const double incident[] = {.25, 2, 6}, specular[] = {.5, 1, 3};
            if (actual[channel] < .01999f || actual[4 + channel] < .01999f ||
                !Near(actual[channel], diffuseFactor, 0.00003, .0005) ||
                !Near(actual[4 + channel], specularFactor, 0.00003, .0005) ||
                !Near(actual[8 + channel], reflectance * incident[channel], .00001, .00001) ||
                !Near(actual[12 + channel], specular[channel], .00001, .00001))
                throw std::runtime_error("Material factor or round-trip mismatch at case " + std::to_string(index % 8));
        }
        if (actual[3] != 0 || actual[7] != 0 || actual[11] != 2 || actual[15] != 4)
            throw std::runtime_error("Material conversion reserved fields or distance changed");
    }
    std::cout << "Validated eight pinned SDK material-factor and packing round-trip cases.\n";
}

void ValidateInputConversion(const float* values)
{
    const float diffuse[4][3] = {{.2f, 1.2f, 1.2f}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    const float statuses[8][2] = {{0, 0}, {3, 0}, {3, 0}, {3, 0}, {2, 0}, {1, 1}, {0, 1}, {1, 0}};
    for (unsigned int index = 0; index < kSampleCount; ++index)
    {
        const unsigned int test = index % 8;
        const float* result = values + index * 16;
        for (unsigned int channel = 0; channel < 16; ++channel)
            if (!std::isfinite(result[channel]))
                throw std::runtime_error("Nonfinite input conversion output");
        if (result[11] != statuses[test][0] || result[15] != statuses[test][1])
            throw std::runtime_error("Input conversion status mismatch");
        if (test >= 4)
        {
            for (unsigned int channel = 0; channel < 8; ++channel)
                if (result[channel] != 0)
                    throw std::runtime_error("Rejected pixel leaked packed radiance");
            continue;
        }
        for (unsigned int channel = 0; channel < 3; ++channel)
        {
            const float specular[] = {.5f, 1, 3};
            if (result[8 + channel] < .01999f || result[12 + channel] < .01999f ||
                !Near(result[channel] * result[8 + channel], diffuse[test][channel], .00001, .00001) ||
                !Near(result[4 + channel] * result[12 + channel], specular[channel], .00001, .00001))
                throw std::runtime_error("Converted RGB round trip failed");
        }
        if (result[3] != (test == 0 ? 2 : 0) || result[7] != 4)
            throw std::runtime_error("Conversion changed hit distance or inactive lobe");
    }
    std::cout << "Validated eight guarded SDK input conversion cases.\n";
}
void ValidateThinTransmission(const float* values)
{
    const double iors[] = {1, 1.1, 1.33, 1.5, 2, 4, 100};
    const double cosines[] = {1, .8, .1, 0};
    const double tint[] = {.2, .4, .8};
    for (unsigned int test = 0; test < 28; ++test)
    {
        const double eta = iors[test / 4];
        const double cosine = cosines[test % 4];
        const double cosT = std::sqrt(1 - (1 - cosine * cosine) / (eta * eta));
        double fresnel = 0;
        if (eta != 1)
        {
            const double s = (cosine - eta * cosT) / (cosine + eta * cosT);
            const double p = (eta * cosine - cosT) / (eta * cosine + cosT);
            fresnel = (s * s + p * p) * .5;
        }
        unsigned int reflected = 0;
        double mean[3] = {};
        for (unsigned int index = 0; index < kSamplesPerCase; ++index)
        {
            const float* sample = values + (test * kSamplesPerCase + index) * 16;
            for (unsigned int channel = 0; channel < 16; ++channel)
            {
                if (!std::isfinite(sample[channel]))
                {
                    throw std::runtime_error("Nonfinite thin transmission sample");
                }
            }
            const bool reflection = (double(index) + .5) / kSamplesPerCase < fresnel;
            if (!Near(sample[0], fresnel, 1e-5, 1e-5) || sample[7] != (reflection ? 1.0f : 0.0f) ||
                !Near(sample[1], std::sqrt(1 - cosine * cosine), 1e-5, 1e-5) || sample[2] != 0 ||
                !Near(sample[3], reflection ? cosine : -cosine, 1e-5, 1e-5))
            {
                throw std::runtime_error("Thin transmission Fresnel or direction mismatch");
            }
            reflected += reflection ? 1u : 0u;
            for (unsigned int channel = 0; channel < 3; ++channel)
            {
                if (!Near(sample[4 + channel], reflection ? 1 : tint[channel], 1e-6, 1e-6))
                {
                    throw std::runtime_error("Thin transmission throughput weight mismatch");
                }
                mean[channel] += sample[4 + channel] / kSamplesPerCase;
            }
            for (unsigned int channel = 8; channel < 16; ++channel)
            {
                if (sample[channel] != 0)
                {
                    throw std::runtime_error("Thin transmission reserved output changed");
                }
            }
        }
        if (std::abs(double(reflected) / kSamplesPerCase - fresnel) > 1.0 / kSamplesPerCase)
        {
            throw std::runtime_error("Thin transmission reflection probability mismatch");
        }
        for (unsigned int channel = 0; channel < 3; ++channel)
        {
            if (std::abs(mean[channel] - (fresnel + (1 - fresnel) * tint[channel])) > 1.0 / kSamplesPerCase)
            {
                throw std::runtime_error("Thin transmission expected energy mismatch");
            }
        }
    }
    std::cout << "Validated 28672 thin transmission GPU samples against double-precision Fresnel and energy.\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
            throw std::runtime_error("Shader path required");
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<char> shader((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (shader.empty())
            throw std::runtime_error("Missing shader bytecode");
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;
        bool hardware = false;
        bool packing = false;
        bool materialFactors = false;
        bool inputConversion = false;
        bool rrInputConversion = false;
        bool rrLiveDistance = false;
        bool thinTransmission = false;
        const char* inputPath = nullptr;
        const char* outputPath = nullptr;
        for (int index = 2; index < argc; ++index)
        {
            if (std::strcmp(argv[index], "--hardware") == 0)
            {
                hardware = true;
            }
            else if (std::strcmp(argv[index], "--packing") == 0)
            {
                packing = true;
            }
            else if (std::strcmp(argv[index], "--thin-transmission") == 0)
            {
                thinTransmission = true;
            }
            else if (std::strcmp(argv[index], "--material-factors") == 0)
            {
                materialFactors = true;
            }
            else if (std::strcmp(argv[index], "--input-conversion") == 0)
            {
                inputConversion = true;
            }
            else if (std::strcmp(argv[index], "--rr-input-conversion") == 0)
            {
                rrInputConversion = true;
            }
            else if (std::strcmp(argv[index], "--rr-live-distance") == 0)
            {
                rrLiveDistance = true;
            }
            else if (std::strcmp(argv[index], "--input") == 0 && index + 1 < argc)
            {
                inputPath = argv[++index];
            }
            else if (std::strcmp(argv[index], "--output") == 0 && index + 1 < argc)
            {
                outputPath = argv[++index];
            }
            else
            {
                throw std::runtime_error("Unknown test option");
            }
        }
        if (int(packing) + int(materialFactors) + int(inputConversion) + int(rrInputConversion) + int(rrLiveDistance) +
                int(thinTransmission) >
            1)
            throw std::runtime_error("Shader validation modes are mutually exclusive");
        if ((inputPath != nullptr) != (outputPath != nullptr) || (inputPath && !inputConversion))
            throw std::runtime_error("Captured conversion requires both input and output paths");
        std::vector<char> captured;
        UINT sampleCount = kSampleCount;
        if (inputPath)
        {
            if (std::filesystem::weakly_canonical(inputPath) == std::filesystem::weakly_canonical(outputPath) ||
                (std::filesystem::exists(outputPath) && std::filesystem::equivalent(inputPath, outputPath)))
                throw std::runtime_error("Output must not overwrite captured input");
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            const std::streamoff size = input.tellg();
            constexpr UINT kInputStride = 7 * 4 * sizeof(float);
            if (size <= 0 || size % kInputStride != 0 || size / kInputStride > 65535 * 64)
                throw std::runtime_error("Invalid captured input size");
            sampleCount = UINT(size / kInputStride);
            captured.resize(static_cast<size_t>(size));
            input.seekg(0);
            input.read(captured.data(), size);
            if (!input)
                throw std::runtime_error("Could not read captured input");
        }
        if (!hardware)
            Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        ComPtr<ID3D12Device> device;
        Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        ComPtr<ID3D12InfoQueue> info;
        Check(device.As(&info));
        D3D12_ROOT_PARAMETER parameter = {};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_PARAMETER parameters[3] = {parameter, {}, {}};
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.Num32BitValues = 1;
        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.NumParameters = inputPath ? 3 : 1;
        rootDesc.pParameters = parameters;
        ComPtr<ID3DBlob> blob, errors;
        Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
        pipelineDesc.pRootSignature = root.Get();
        pipelineDesc.CS = {shader.data(), shader.size()};
        ComPtr<ID3D12PipelineState> pipeline;
        Check(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)));
        const UINT64 byteCount = UINT64(sampleCount) * (rrInputConversion || rrLiveDistance ? 24 : 16) * sizeof(float);
        ComPtr<ID3D12Resource> inputBuffer;
        if (inputPath)
        {
            inputBuffer =
                Buffer(device.Get(), captured.size(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            void* mapped = nullptr;
            const D3D12_RANGE noRead = {0, 0};
            Check(inputBuffer->Map(0, &noRead, &mapped));
            std::memcpy(mapped, captured.data(), captured.size());
            inputBuffer->Unmap(0, nullptr);
        }
        const auto output =
            Buffer(device.Get(), byteCount, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto readback = Buffer(device.Get(), byteCount, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        ComPtr<ID3D12CommandQueue> queue;
        Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> allocator;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> commands;
        Check(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands)));
        commands->SetComputeRootSignature(root.Get());
        commands->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
        if (inputPath)
        {
            commands->SetComputeRootShaderResourceView(1, inputBuffer->GetGPUVirtualAddress());
            commands->SetComputeRoot32BitConstant(2, sampleCount, 0);
        }
        commands->Dispatch((sampleCount + 63) / 64, 1, 1);
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = output.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1, &barrier);
        commands->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, byteCount);
        Check(commands->Close());
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        Check(queue->Signal(fence.Get(), 1));
        const HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            throw std::runtime_error("Could not create fence event");
        const HRESULT eventResult = fence->SetEventOnCompletion(1, event);
        const DWORD wait = SUCCEEDED(eventResult) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
        CloseHandle(event);
        Check(eventResult);
        if (wait != WAIT_OBJECT_0)
            throw std::runtime_error("GPU sampling test timed out");
        for (UINT64 index = 0; index < info->GetNumStoredMessages(); ++index)
        {
            SIZE_T length = 0;
            Check(info->GetMessage(index, nullptr, &length));
            std::vector<char> messageBytes(length);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(messageBytes.data());
            Check(info->GetMessage(index, message, &length));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                throw std::runtime_error(message->pDescription);
        }
        const D3D12_RANGE range = {0, static_cast<SIZE_T>(byteCount)};
        void* data = nullptr;
        Check(readback->Map(0, &range, &data));
        if (inputPath)
        {
            std::ofstream outputFile(outputPath, std::ios::binary);
            outputFile.write(static_cast<const char*>(data), byteCount);
            if (!outputFile)
                throw std::runtime_error("Could not write converted output");
        }
        else if (rrInputConversion)
        {
            RrInputTests::ValidateRrInputConversion(static_cast<const float*>(data), sampleCount);
        }
        else if (rrLiveDistance)
        {
            RrInputTests::ValidateRrLiveDistance(static_cast<const float*>(data), sampleCount);
        }
        else if (thinTransmission)
        {
            ValidateThinTransmission(static_cast<const float*>(data));
        }
        else if (inputConversion)
        {
            ValidateInputConversion(static_cast<const float*>(data));
        }
        else if (materialFactors)
        {
            ValidateMaterialFactors(static_cast<const float*>(data));
        }
        else if (packing)
        {
            ValidatePacking(static_cast<const float*>(data));
        }
        else
        {
            Validate(static_cast<const float*>(data));
        }
        const D3D12_RANGE written = {0, 0};
        readback->Unmap(0, &written);
        std::cout << (hardware ? "Hardware" : "WARP") << ": no D3D12 errors.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
