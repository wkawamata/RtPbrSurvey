// Included inside the texture replay harness's private namespace.
// Single retained production only; this is not a real-time history path.
void NativeReplay(ID3D12Device* device,
                  ID3D12CommandQueue* queue,
                  ID3D12CommandAllocator* allocator,
                  ID3D12GraphicsCommandList* commands,
                  ID3D12InfoQueue* info,
                  const std::array<ComPtr<ID3D12Resource>, 17>& textures,
                  const std::filesystem::path& input,
                  const std::filesystem::path& output,
                  const Frame& frame)
{
    const auto raw = Read(input / "native.json");
    const auto metadata = nlohmann::json::parse(raw.begin(), raw.end());
    const auto reject = [&](const char* reason)
    {
        const auto diagnostics = Engine::QueryStreamlineRayReconstructionDiagnostics();
        std::ofstream(output / "admission-result.json")
            << nlohmann::json({{"admitted", false},
                               {"reason", reason},
                               {"nativeEvaluateCallCount", diagnostics.nativeEvaluateCallCount}})
                   .dump(2);
        throw std::runtime_error(reason);
    };
    const auto status = Read(output / "out8.bin");
    if (status.size() != SIZE_T(frame.width) * frame.height * sizeof(UINT))
        reject("Incomplete numeric status coverage");
    for (size_t offset = 0; offset < status.size(); offset += sizeof(UINT))
    {
        UINT value;
        std::memcpy(&value, status.data() + offset, sizeof(value));
        if (value != 0)
            reject("Native admission rejected an unsupported pixel");
    }
    if (metadata.at("distanceSource") != "actual-continuation" || metadata.at("samplesPerFrame") != 1 ||
        metadata.at("primarySampling") != "pixel-center" || !metadata.at("staticCamera").get<bool>() ||
        !metadata.at("nativeOptIn").get<bool>())
        reject("Native replay profile/opt-in rejected");
    Engine::RayReconstructionEvaluateInputs inputs;
    inputs.pathTracingDiagnostic = true;
    inputs.commandList = commands;
    inputs.renderWidth = frame.width;
    inputs.renderHeight = frame.height;
    inputs.scalingInputColor = textures[8].Get();
    inputs.albedo = textures[9].Get();
    inputs.specularAlbedo = textures[10].Get();
    inputs.normal = textures[11].Get();
    inputs.roughness = textures[12].Get();
    inputs.depth = textures[13].Get();
    inputs.motionVectors = textures[14].Get();
    inputs.specularHitDistance = textures[15].Get();
    inputs.enableNativeEvaluation = true;
    inputs.historyReset = true;
    inputs.historyValid = false;
    inputs.frameConstants.worldToCameraView = metadata.at("worldToView").get<std::array<float, 16>>();
    inputs.frameConstants.cameraViewToWorld = metadata.at("viewToWorld").get<std::array<float, 16>>();
    auto& temporal = inputs.frameConstants.temporal;
    temporal.cameraViewToClip = metadata.at("viewToClip").get<std::array<float, 16>>();
    temporal.clipToCameraView = metadata.at("clipToView").get<std::array<float, 16>>();
    const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    temporal.clipToPrevClip = identity;
    temporal.prevClipToClip = identity;
    temporal.cameraPosition = metadata.at("cameraPosition").get<std::array<float, 3>>();
    temporal.cameraRight = metadata.at("cameraRight").get<std::array<float, 3>>();
    temporal.cameraUp = metadata.at("cameraUp").get<std::array<float, 3>>();
    temporal.cameraForward = metadata.at("cameraForward").get<std::array<float, 3>>();
    temporal.cameraNear = metadata.at("nearZ");
    temporal.cameraFar = metadata.at("farZ");
    temporal.cameraFovRadians = metadata.at("fovRadians");
    temporal.cameraAspectRatio = float(frame.width) / frame.height;
    temporal.cameraMotionIncluded = true;
    // Converter emitted normalized UV deltas; they are not pixel displacements.
    inputs.frameConstants.motionVectorScale = {1, 1};
    Check(allocator->Reset());
    Check(commands->Reset(allocator, nullptr));
    for (UINT slot = 0; slot < 8; ++slot)
        Transition(commands,
                   textures[8 + slot].Get(),
                   D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    auto desc = textures[8]->GetDesc();
    ComPtr<ID3D12Resource> nativeOutput;
    Check(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&nativeOutput)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 total, rowBytes;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, &rowBytes, &total);
    auto poison = Buffer(device, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped;
    const D3D12_RANGE noRead = {0, 0};
    Check(poison->Map(0, &noRead, &mapped));
    for (UINT row = 0; row < frame.height; ++row)
        for (UINT column = 0; column < frame.width * 4; ++column)
        {
            const UINT nan = 0x7fc00000;
            std::memcpy(static_cast<char*>(mapped) + row * footprint.Footprint.RowPitch + column * 4, &nan, 4);
        }
    poison->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION target = {};
    target.pResource = nativeOutput.Get();
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = poison.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    Transition(commands, nativeOutput.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    inputs.scalingOutputColor = nativeOutput.Get();
    const auto result = Engine::EvaluateStreamlineRayReconstruction(inputs);
    const auto diagnostics = Engine::QueryStreamlineRayReconstructionDiagnostics();
    nlohmann::json report = {{"outputAvailable", result.outputAvailable},
                             {"inputReady", result.inputReady},
                             {"inputReason", Engine::ToString(result.inputReadinessReason)},
                             {"nativeResult", diagnostics.lastEvaluateResultName},
                             {"nativeEvaluateCallCount", diagnostics.nativeEvaluateCallCount},
                             {"frameId", metadata.at("frameId")},
                             {"sampleStartIndex", metadata.at("sampleStartIndex")},
                             {"reset", true},
                             {"width", frame.width},
                             {"height", frame.height}};
    std::ofstream(output / "native-result.json") << report.dump(2);
    std::cout << "RR evaluate: " << diagnostics.lastEvaluateResultName << " output=" << result.outputAvailable
              << std::endl;
    if (!result.outputAvailable)
        throw std::runtime_error("Native RR evaluation did not produce output");
    Transition(commands, nativeOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    auto readback = Buffer(device, total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    source = {};
    source.pResource = nativeOutput.Get();
    target = {};
    target.pResource = readback.Get();
    target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    target.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    Check(commands->Close());
    ID3D12CommandList* lists[] = {commands};
    queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    Check(queue->Signal(fence.Get(), 1));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!event)
        throw std::runtime_error("Native replay event creation failed");
    const HRESULT fenceResult = fence->SetEventOnCompletion(1, event);
    const DWORD wait = SUCCEEDED(fenceResult) ? WaitForSingleObject(event, 60000) : WAIT_FAILED;
    CloseHandle(event);
    Check(fenceResult);
    if (wait != WAIT_OBJECT_0)
        throw std::runtime_error("Native RR GPU completion timed out");
    const D3D12_RANGE range = {0, SIZE_T(total)};
    Check(readback->Map(0, &range, &mapped));
    std::ofstream stream(output / "native-rgba32.bin", std::ios::binary);
    bool finiteRgb = true;
    for (UINT row = 0; row < frame.height; ++row)
    {
        const char* data = static_cast<char*>(mapped) + row * footprint.Footprint.RowPitch;
        stream.write(data, std::streamsize(rowBytes));
        for (UINT column = 0; column < frame.width; ++column)
            for (UINT channel = 0; channel < 3; ++channel)
            {
                float value;
                std::memcpy(&value, data + (column * 4 + channel) * sizeof(float), sizeof(value));
                finiteRgb &= std::isfinite(value);
            }
    }
    readback->Unmap(0, &noRead);
    if (!stream || !finiteRgb)
        throw std::runtime_error("RR output incomplete/nonfinite RGB");
    for (UINT64 index = 0; index < info->GetNumStoredMessages(); ++index)
    {
        SIZE_T size = 0;
        Check(info->GetMessage(index, nullptr, &size));
        std::vector<char> storage(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Check(info->GetMessage(index, message, &size));
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            throw std::runtime_error(message->pDescription);
    }
    report["gpuCompleted"] = true;
    report["finiteRgb"] = true;
    report["d3d12Errors"] = 0;
    std::ofstream(output / "native-result.json") << report.dump(2);
}
