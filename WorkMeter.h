#pragma once

#include <chrono>
#include <vector>
#include <string>
#include <assert.h>
#include <wrl/client.h>
#include "Shared/Error.h"
#include "MyDx12Utils.h"

using Microsoft::WRL::ComPtr;

namespace MyDx12Util
{

class WorkMeter
{
public:
    class CheckPoint
    {
    public:
        CheckPoint(const std::string& name, const std::chrono::steady_clock::time_point& timePoint)
            : name(name), timePoint(timePoint)
        {
        }
        std::string name;
        std::chrono::steady_clock::time_point timePoint;
    };

    void Start()
    {
        m_timePoints.clear();
        m_timePoints.emplace_back(CheckPoint{"StartCpu", std::chrono::steady_clock::now()});
    }

    void End()
    {
        m_timePoints.emplace_back(CheckPoint{"EndCpu", std::chrono::steady_clock::now()});
        auto cpuStart = m_timePoints.front().timePoint;
        auto cpuEnd = m_timePoints.back().timePoint;
        assert(m_timePoints.front().name == "StartCpu" && m_timePoints.back().name == "EndCpu");
        m_cpuFrameTime = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
    }

    float GetCpuFrameTimeMs() const
    {
        return m_cpuFrameTime;
    }

private:
    float m_cpuFrameTime = 0.0f;

    std::vector<CheckPoint> m_timePoints;
};

class GpuWorkMeter
{
public:
    class CheckPoint
    {
    public:
        CheckPoint(const std::string& name, int passIndex = -1) : name(name), timeStamp(0.f), passIndex(passIndex) {}
        std::string name;
        float timeStamp;
        int passIndex;
    };

    void Init(ID3D12Device* device, UINT maxQueryCount, UINT frameCount)
    {
        m_maxQueryCount = maxQueryCount;
        m_frames.resize(frameCount);
        for (FrameQueries& frame : m_frames)
        {
            D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
            queryHeapDesc.Count = maxQueryCount;
            queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            ThrowIfFailed(device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&frame.queryHeap)));
            ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
                                                          D3D12_HEAP_FLAG_NONE,
                                                          &CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT64) * maxQueryCount),
                                                          D3D12_RESOURCE_STATE_COPY_DEST,
                                                          nullptr,
                                                          IID_PPV_ARGS(&frame.queryReadback)));
        }
    }

    void Term()
    {
        m_frames.clear();
    }

    void StartGpu(ID3D12GraphicsCommandList* commandList, UINT frameIndex, std::vector<CheckPoint>& checkPoints)
    {
        assert(frameIndex < m_frames.size());
        m_activeFrameIndex = frameIndex;
        FrameQueries& frame = m_frames[frameIndex];
        frame.queryIndex = 0;
        frame.checkPoints = &checkPoints;
        frame.checkPoints->clear();
        query(commandList, frame, frame.queryIndex, std::string("StartGpu"));
        frame.queryIndex++;
    }

    void SetCheckPoint(ID3D12GraphicsCommandList* commandList, const std::string& name, int passIndex = -1)
    {
        FrameQueries& frame = m_frames[m_activeFrameIndex];
        if (frame.queryIndex >= m_maxQueryCount)
        {
            // Handle error: too many queries
            assert(false && "Exceeded maximum query count");
            return;
        }
        query(commandList, frame, frame.queryIndex, name, passIndex);
        frame.queryIndex++;
    }

    void EndGpu(ID3D12GraphicsCommandList* commandList)
    {

        FrameQueries& frame = m_frames[m_activeFrameIndex];
        if (frame.queryIndex >= m_maxQueryCount)
        {
            // Handle error: too many queries
            assert(false && "Exceeded maximum query count");
            return;
        }
        query(commandList, frame, frame.queryIndex, std::string("EndGpu"), -1);
        frame.queryIndex++;
        // resolve query data to readback buffer
        commandList->ResolveQueryData(
            frame.queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, frame.queryIndex, frame.queryReadback.Get(), 0);
    }

    bool ReadbackData(ID3D12CommandQueue* commandQueue, UINT frameIndex)
    {
        assert(frameIndex < m_frames.size());
        FrameQueries& frame = m_frames[frameIndex];
        if (frame.queryIndex == 0 || frame.checkPoints == nullptr)
        {
            return false;
        }
        UINT64* queryData = nullptr;
        D3D12_RANGE readRange = {0, sizeof(UINT64) * frame.queryIndex};
        ThrowIfFailed(frame.queryReadback->Map(0, &readRange, reinterpret_cast<void**>(&queryData)));

        UINT64 freq = 0;
        commandQueue->GetTimestampFrequency(&freq);

        for (int i = 0; i < frame.queryIndex; i++)
        {
            frame.checkPoints->at(i).timeStamp = ((queryData[i] - queryData[0]) / static_cast<float>(freq)) * 1000.0f;
            // DBG_PRINT("Gpu CheckPoint: %s, Time: %f ms\n", m_pCheckPoints->at(i).name.c_str(),
            // m_pCheckPoints->at(i).timeStamp);
        }

        frame.queryReadback->Unmap(0, nullptr);
        return true;
    }

private:
    struct FrameQueries
    {
        ComPtr<ID3D12QueryHeap> queryHeap;
        ComPtr<ID3D12Resource> queryReadback;
        std::vector<CheckPoint>* checkPoints = nullptr;
        int queryIndex = 0;
    };

    std::vector<FrameQueries> m_frames;
    UINT m_activeFrameIndex = 0;
    int m_maxQueryCount = 0;

    void query(ID3D12GraphicsCommandList* commandList,
               FrameQueries& frame,
               UINT queryIndex,
               const std::string& name,
               int passIndex = -1)
    {
        commandList->EndQuery(frame.queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
        frame.checkPoints->emplace_back(CheckPoint(name, passIndex));
    }
};

} // namespace MyDx12Util
