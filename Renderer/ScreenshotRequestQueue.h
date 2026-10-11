#pragma once

#include "Shared/Screenshot.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

namespace Engine
{
class ScreenshotRequestQueue
{
public:
    void Enqueue(RtPbrSurvey::ScreenshotRequest request)
    {
        m_requests.push_back(std::move(request));
    }

    bool NextCaptureIsPfm() const
    {
        return !m_requests.empty() && m_requests.front().path.extension() == L".pfm";
    }

    bool CanBeginNextCapture() const
    {
        return !m_capturePending && !m_requests.empty();
    }

    bool IsIdle() const
    {
        return !m_capturePending && m_requests.empty();
    }

    const RtPbrSurvey::ScreenshotRequest* PeekNextCapture() const
    {
        return CanBeginNextCapture() ? &m_requests.front() : nullptr;
    }

    bool BeginNextCapture(RtPbrSurvey::ScreenshotRequest& request)
    {
        if (!CanBeginNextCapture())
        {
            return false;
        }

        request = std::move(m_requests.front());
        m_requests.pop_front();
        m_capturePending = true;
        m_pendingRequestId = request.requestId;
        return true;
    }

    bool CompletePending(RtPbrSurvey::ScreenshotResult result)
    {
        if (!m_capturePending)
        {
            return false;
        }

        m_capturePending = false;
        result.requestId = m_pendingRequestId;
        m_pendingRequestId = 0;
        m_results.push_back(std::move(result));
        return true;
    }

    void AddResult(RtPbrSurvey::ScreenshotResult result)
    {
        m_results.push_back(std::move(result));
    }

    void FailQueued(const std::string& error)
    {
        while (!m_requests.empty())
        {
            RtPbrSurvey::ScreenshotRequest request = std::move(m_requests.front());
            m_requests.pop_front();
            RtPbrSurvey::ScreenshotResult result = {request.path, false, error};
            result.requestId = request.requestId;
            m_results.push_back(std::move(result));
        }
    }

    std::optional<RtPbrSurvey::ScreenshotResult> ConsumeResult()
    {
        if (m_results.empty())
        {
            return std::nullopt;
        }

        RtPbrSurvey::ScreenshotResult result = std::move(m_results.front());
        m_results.pop_front();
        return result;
    }

    std::optional<RtPbrSurvey::ScreenshotResult> ConsumeResult(std::uint64_t requestId)
    {
        const auto result = std::find_if(m_results.begin(),
                                         m_results.end(),
                                         [requestId](const RtPbrSurvey::ScreenshotResult& candidate)
                                         { return candidate.requestId == requestId; });
        if (result == m_results.end())
        {
            return std::nullopt;
        }

        RtPbrSurvey::ScreenshotResult value = std::move(*result);
        m_results.erase(result);
        return value;
    }

    std::optional<RtPbrSurvey::ScreenshotResult> ConsumeResultExcept(std::uint64_t requestId)
    {
        const auto result = std::find_if(m_results.begin(),
                                         m_results.end(),
                                         [requestId](const RtPbrSurvey::ScreenshotResult& candidate)
                                         { return candidate.requestId != requestId; });
        if (result == m_results.end())
        {
            return std::nullopt;
        }

        RtPbrSurvey::ScreenshotResult value = std::move(*result);
        m_results.erase(result);
        return value;
    }

private:
    std::deque<RtPbrSurvey::ScreenshotRequest> m_requests;
    std::deque<RtPbrSurvey::ScreenshotResult> m_results;
    bool m_capturePending = false;
    std::uint64_t m_pendingRequestId = 0;
};
} // namespace Engine
