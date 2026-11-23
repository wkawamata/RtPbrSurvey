#include "stdafx.h"

#include "ImGuiSystem.h"

#include "Ui/DebugUiPreferences.h"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include <string>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine
{

namespace
{
SimpleDescriptorHeapAllocator* g_allocator = nullptr;

float DecodeSrgb(ImU32 color, unsigned shift)
{
    const float value = static_cast<float>((color >> shift) & 0xffu) / 255.0f;
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

ImU32 EncodePq(float normalizedLuminance)
{
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    const float l = std::pow(normalizedLuminance, m1);
    return static_cast<ImU32>(std::lround(255.0f * std::pow((c1 + c2 * l) / (1.0f + c3 * l), m2)));
}

ImU32 ConvertUiRgbToHdr10(ImU32 color, float whiteScale)
{
    const float r = DecodeSrgb(color, IM_COL32_R_SHIFT) * whiteScale;
    const float g = DecodeSrgb(color, IM_COL32_G_SHIFT) * whiteScale;
    const float b = DecodeSrgb(color, IM_COL32_B_SHIFT) * whiteScale;
    // Same Rec.709 to Rec.2020 matrix as the scene tone-map shader.
    return IM_COL32(EncodePq(0.6274040f * r + 0.3292820f * g + 0.0433136f * b),
                    EncodePq(0.0690970f * r + 0.9195400f * g + 0.0113612f * b),
                    EncodePq(0.0163916f * r + 0.0880132f * g + 0.8955950f * b), 0);
}

void RenderHdrUi(ImDrawData* drawData, ID3D12GraphicsCommandList* commandList)
{
    // The standard DX12 backend multiplies sampled texture RGB by vertex RGB.
    // The monochrome atlas supplies coverage. Application textures have color
    // of their own and need a texture-aware conversion in the shader.
    const ImTextureData* atlas = ImGui::GetIO().Fonts->TexData;
    const float whiteScale = static_cast<float>(RtPbrSurvey::GetDebugUiPreferences().hdrImGuiWhiteNits) / 10000.0f;
    constexpr unsigned atlasVertex = 1;
    constexpr unsigned otherVertex = 2;
    std::unordered_map<ImU32, ImU32> convertedColors;
    std::vector<unsigned char> vertexUsage;
    std::vector<std::pair<ImDrawVert*, ImU32>> originalColors;
    originalColors.reserve(static_cast<size_t>(drawData->TotalVtxCount));
    for (ImDrawList* list : drawData->CmdLists)
    {
        vertexUsage.assign(static_cast<size_t>(list->VtxBuffer.Size), 0);
        for (const ImDrawCmd& command : list->CmdBuffer)
        {
            if (command.UserCallback != nullptr)
            {
                continue;
            }
            const bool usesAtlas = atlas != nullptr && command.TexRef._TexData == atlas;
            const unsigned char usage = usesAtlas ? atlasVertex : otherVertex;
            for (unsigned i = 0; i < command.ElemCount; ++i)
            {
                vertexUsage[command.VtxOffset + list->IdxBuffer[command.IdxOffset + i]] |= usage;
            }
        }

        for (int i = 0; i < list->VtxBuffer.Size; ++i)
        {
            if (vertexUsage[static_cast<size_t>(i)] != atlasVertex)
            {
                continue;
            }
            ImDrawVert& vertex = list->VtxBuffer[i];
            const ImU32 rgb = vertex.col & ~IM_COL32_A_MASK;
            auto [converted, inserted] = convertedColors.try_emplace(rgb, 0);
            if (inserted)
            {
                converted->second = ConvertUiRgbToHdr10(rgb, whiteScale);
            }
            originalColors.emplace_back(&vertex, vertex.col);
            vertex.col = converted->second | (vertex.col & IM_COL32_A_MASK);
        }
    }

    ImGui_ImplDX12_RenderDrawData(drawData, commandList);
    for (const auto& [vertex, originalColor] : originalColors)
    {
        vertex->col = originalColor;
    }
}

void AddDefaultUiFont()
{
    ImGuiIO& io = ImGui::GetIO();
    char windowsDirectory[MAX_PATH] = {};
    const UINT windowsDirectoryLength = GetWindowsDirectoryA(windowsDirectory, MAX_PATH);
    if (windowsDirectoryLength > 0 && windowsDirectoryLength < MAX_PATH)
    {
        static constexpr const char* fontNames[] = {"YuGothM.ttc", "meiryo.ttc", "msgothic.ttc"};
        for (const char* fontName : fontNames)
        {
            const std::string fontPath = std::string(windowsDirectory) + "\\Fonts\\" + fontName;
            if (GetFileAttributesA(fontPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
                io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 14.0f, nullptr, io.Fonts->GetGlyphRangesJapanese()) !=
                    nullptr)
            {
                return;
            }
        }
    }

    io.Fonts->AddFontDefault();
    OutputDebugStringA("[WARNING] Japanese UI font was unavailable; using the default ImGui font.\n");
}
}

void ImGuiSystem::Initialize(HWND hwnd,
                             GraphicsDevice& device,
                             ID3D12DescriptorHeap* srvHeap,
                             UINT frameCount,
                             DXGI_FORMAT rtvFormat)
{
    m_srvHeap = srvHeap;
    m_device = device.Device();
    m_descriptorHeapAllocator.Init(device.Device(), m_srvHeap);
    g_allocator = &m_descriptorHeapAllocator;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    AddDefaultUiFont();
    RtPbrSurvey::RegisterDebugUiPreferencesSettingsHandler();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device.Device();
    initInfo.CommandQueue = device.CommandQueue();
    initInfo.NumFramesInFlight = frameCount;
    initInfo.RTVFormat = rtvFormat;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = m_srvHeap;
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
    { g_allocator->Alloc(outCpuHandle, outGpuHandle); };
    initInfo.SrvDescriptorFreeFn =
        [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
    { g_allocator->Free(&cpuHandle, &gpuHandle); };
    ImGui_ImplDX12_Init(&initInfo);

    m_initialized = true;
}

void ImGuiSystem::BeginFrame()
{
    if (!m_initialized)
    {
        return;
    }

    // A graph without a UI pass may leave the previous CPU frame unrendered.
    EndFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    m_frameOpen = true;
}

void ImGuiSystem::EndFrame()
{
    if (!m_initialized || !m_frameOpen)
    {
        return;
    }

    ImGui::Render();
    m_frameOpen = false;
}

void ImGuiSystem::Render(ID3D12GraphicsCommandList* commandList, bool hdr10Output)
{
    if (!m_initialized)
    {
        return;
    }

    // Diagnostic frame loops also use Render directly, without the normal
    // application's late-overlay callback. Finalize exactly once in both paths.
    EndFrame();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr)
        return;
    ID3D12DescriptorHeap* imguiHeaps[] = {m_srvHeap};
    commandList->SetDescriptorHeaps(1, imguiHeaps);
    if (hdr10Output)
    {
        RenderHdrUi(drawData, commandList);
    }
    else
    {
        ImGui_ImplDX12_RenderDrawData(drawData, commandList);
    }
}

void ImGuiSystem::SetDisplaySize(UINT width, UINT height)
{
    if (!m_initialized)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

uint64_t ImGuiSystem::UpdateTexture(UINT textureIndex, ID3D12Resource* resource, DXGI_FORMAT format)
{
    if (!m_initialized || textureIndex >= m_textureBindings.size() || resource == nullptr)
    {
        ClearTexture(textureIndex);
        return 0;
    }

    TextureBinding& binding = m_textureBindings[textureIndex];
    if (!binding.descriptor.IsValid())
    {
        binding.descriptor = m_descriptorHeapAllocator.Allocate();
    }
    if (binding.resource.Get() != resource || binding.format != format)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(resource, &srvDesc, binding.descriptor.Cpu());
        binding.resource = resource;
        binding.format = format;
    }
    return binding.descriptor.Gpu().ptr;
}

void ImGuiSystem::ClearTexture(UINT textureIndex)
{
    if (textureIndex >= m_textureBindings.size())
    {
        return;
    }

    TextureBinding& binding = m_textureBindings[textureIndex];
    binding.descriptor.Reset();
    binding.resource.Reset();
    binding.format = DXGI_FORMAT_UNKNOWN;
}

void ImGuiSystem::ClearTextures()
{
    for (UINT i = 0; i < m_textureBindings.size(); ++i)
    {
        ClearTexture(i);
    }
}

void ImGuiSystem::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    ClearTextures();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
    m_srvHeap = nullptr;
    m_device = nullptr;
    if (g_allocator == &m_descriptorHeapAllocator)
    {
        g_allocator = nullptr;
    }
}

} // namespace Engine
