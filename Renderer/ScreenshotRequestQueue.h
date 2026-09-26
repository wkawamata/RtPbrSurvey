#pragma once

#include "Shared/Screenshot.h"

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

    bool CanBeginNextCapture() const
    {
        return !m_capturePending && !m_requests.empty();
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
        return true;
    }

    bool CompletePending(RtPbrSurvey::ScreenshotResult result)
    {
        if (!m_capturePending)
        {
            return false;
        }

        m_capturePending = false;
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
            m_results.push_back({request.path, false, error});
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

private:
    std::deque<RtPbrSurvey::ScreenshotRequest> m_requests;
    std::deque<RtPbrSurvey::ScreenshotResult> m_results;
    bool m_capturePending = false;
};
} // namespace Engine
