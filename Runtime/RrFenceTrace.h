#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <stdexcept>
#include <vector>

namespace RtPbrSurvey
{
// Process-local diagnostics. No fence ordering or lifetime decision depends on it.
class RrFenceTrace
{
public:
    struct Event
    {
        uint64_t qpc, frame, tick, submission, production, epoch;
        const char* name;
        uint64_t a, b, c, d;
    };
    struct GpuPoint
    {
        uint64_t submission, tick, frequency, node;
        int index;
        std::string name;
    };
    struct Calibration
    {
        uint64_t before, after, gpu, cpu, frequency;
        int64_t result;
    };
    static uint64_t Now()
    {
        LARGE_INTEGER value;
        QueryPerformanceCounter(&value);
        return static_cast<uint64_t>(value.QuadPart);
    }
    void Configure(const std::filesystem::path& path, double warmup, double duration, bool timingOnly)
    {
        m_path = path;
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        m_frequency = static_cast<uint64_t>(frequency.QuadPart);
        m_warmup = warmup;
        m_duration = duration;
        m_running = true;
        m_capture = !timingOnly;
        if (m_capture)
        {
            m_events.reserve(500000);
            m_gpu.reserve(200000);
            m_calibrations.reserve(256);
        }
    }
    bool Running() const
    {
        return m_running;
    }
    double ElapsedSeconds() const
    {
        return m_start ? double(Now() - m_start) / m_frequency : 0.0;
    }
    double MeasurementElapsedSeconds() const
    {
        return m_windowStart ? double(Now() - m_windowStart) / m_frequency : 0.0;
    }
    bool Capturing() const
    {
        return m_capture && !m_finished;
    }
    void Tick()
    {
        if (!m_running || m_finished)
            return;
        const uint64_t now = Now();
        if (!m_start)
            m_start = now;
        ++m_tick;
        if (!m_windowStart && double(now - m_start) / m_frequency >= m_warmup)
        {
            m_windowStart = now;
            m_startPresent = m_presents;
            m_startFresh = m_freshPresents;
            m_startPublished = m_published;
        }
        if (m_windowStart && double(now - m_windowStart) / m_frequency >= m_duration)
        {
            m_windowEnd = now;
            m_finished = true;
        }
    }
    bool Finished() const
    {
        return m_running && m_finished;
    }
    void BeginFrame(ID3D12CommandQueue* queue)
    {
        if (!m_running)
            return;
        ++m_frame;
        m_submission = 0;
        Record("frame_begin");
        if (!Capturing())
            return;
        const uint64_t before = Now();
        if (!m_lastCalibration || before - m_lastCalibration >= m_frequency)
        {
            UINT64 gpu = 0, cpu = 0, frequency = 0;
            const HRESULT result = queue->GetClockCalibration(&gpu, &cpu);
            queue->GetTimestampFrequency(&frequency);
            const uint64_t after = Now();
            if (m_calibrations.size() < 256)
                m_calibrations.push_back({before, after, gpu, cpu, frequency, result});
            else
                m_overflow = true;
            m_lastCalibration = before;
        }
    }
    void Submission(uint64_t id)
    {
        if (m_running)
            m_submission = id;
    }
    void Production(uint64_t id, uint64_t epoch)
    {
        if (m_running)
        {
            m_production = id;
            m_epoch = epoch;
        }
    }
    void Record(const char* name, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0)
    {
        if (!Capturing())
            return;
        if (m_events.size() == 500000)
        {
            m_overflow = true;
            return;
        }
        m_events.push_back({Now(), m_frame, m_tick, m_submission, m_production, m_epoch, name, a, b, c, d});
    }
    void Presented(uint64_t id)
    {
        if (!m_running || m_finished)
            return;
        ++m_presents;
        if (id && id != m_lastPresented)
            ++m_freshPresents;
        m_lastPresented = id;
    }
    void VerifiedProductionSubmitted()
    {
        if (m_running && !m_finished)
        {
            ++m_verifiedProductionSubmissions;
            m_verifiedSubmitQpc = Now();
        }
    }
    void VerifiedPoseReady()
    {
        if (m_running && !m_finished)
        {
            m_verifiedPoseQpc = Now();
            m_verifiedSubmitQpc = 0;
            m_verifiedReadyQpc = 0;
            m_verifiedGpuMs.fill(-1.0);
        }
    }
    void VerifiedProductionReady()
    {
        if (m_running && !m_finished)
        {
            m_verifiedReadyQpc = Now();
        }
    }
    template <class Points> void VerifiedProductionGpu(const Points& points)
    {
        if (!m_running || !m_windowStart || m_finished || points.size() < 2)
            return;
        const uint64_t frequency = points.front().timestampFrequency;
        if (!frequency)
            return;
        for (size_t i = 1; i < points.size(); ++i)
        {
            const auto end = points[i].rawTimestamp;
            const auto begin = points[i - 1].rawTimestamp;
            if (end < begin)
                continue;
            for (size_t pass = 0; pass < kVerifiedGpuPasses.size(); ++pass)
            {
                if (points[i].name == kVerifiedGpuPasses[pass].checkpoint)
                {
                    const auto from = points[i].name == "EndGpu" ? points.front().rawTimestamp : begin;
                    m_verifiedGpuMs[pass] = double(end - from) * 1000.0 / frequency;
                    break;
                }
            }
        }
    }
    void VerifiedPresentation(bool newRr, uint64_t presentQpc)
    {
        if (!m_running || m_finished)
            return;
        if (!newRr)
        {
            ++m_verifiedFallbackPresentations;
            return;
        }
        ++m_verifiedFreshPresentations;
        if (m_windowStart && presentQpc >= m_windowStart && m_verifiedPoseQpc &&
            presentQpc >= m_verifiedPoseQpc)
        {
            if (m_verifiedPoseToPresentTicks.size() == 500000)
            {
                m_overflow = true;
                return;
            }
            m_verifiedPoseToPresentTicks.push_back(presentQpc - m_verifiedPoseQpc);
            if (m_verifiedSubmitQpc >= m_verifiedPoseQpc && m_verifiedReadyQpc >= m_verifiedSubmitQpc &&
                presentQpc >= m_verifiedReadyQpc)
            {
                m_verifiedTimings.push_back({m_verifiedSubmitQpc - m_verifiedPoseQpc,
                                             m_verifiedReadyQpc - m_verifiedSubmitQpc,
                                             presentQpc - m_verifiedReadyQpc,
                                             m_verifiedGpuMs});
            }
        }
    }
    void Published(uint64_t id)
    {
        if (!m_running || m_finished)
            return;
        ++m_published;
        Record("published", id);
    }
    template <class Points> void Gpu(const Points& points)
    {
        if (!m_capture || points.empty() || points.front().frameId <= m_lastGpu)
            return;
        m_lastGpu = points.front().frameId;
        for (const auto& point : points)
        {
            if (m_gpu.size() == 200000)
            {
                m_overflow = true;
                return;
            }
            m_gpu.push_back({point.frameId,
                             point.rawTimestamp,
                             point.timestampFrequency,
                             point.passNodeId,
                             point.passIndex,
                             point.name});
        }
    }
    void MarkGpu(const char* name)
    {
        if (Capturing() && gpuMarker)
            gpuMarker(name);
    }
    void Write()
    {
        if (!m_running)
            return;
        nlohmann::json events = nlohmann::json::array(), gpu = nlohmann::json::array(),
                       calibrations = nlohmann::json::array();
        for (const auto& e : m_events)
            events.push_back({{"qpc", e.qpc},
                              {"frame", e.frame},
                              {"tick", e.tick},
                              {"submission", e.submission},
                              {"production", e.production},
                              {"epoch", e.epoch},
                              {"event", e.name},
                              {"a", e.a},
                              {"b", e.b},
                              {"c", e.c},
                              {"d", e.d}});
        for (const auto& p : m_gpu)
            gpu.push_back({{"submission", p.submission},
                           {"tick", p.tick},
                           {"frequency", p.frequency},
                           {"node", p.node},
                           {"index", p.index},
                           {"name", p.name}});
        for (const auto& c : m_calibrations)
            calibrations.push_back({{"before", c.before},
                                    {"after", c.after},
                                    {"gpu", c.gpu},
                                    {"cpu", c.cpu},
                                    {"frequency", c.frequency},
                                    {"result", c.result}});
        auto verifiedTimings = nlohmann::json::array();
        for (const auto& timing : m_verifiedTimings)
        {
            auto gpuMs = nlohmann::json::object();
            for (size_t i = 0; i < kVerifiedGpuPasses.size(); ++i)
            {
                if (timing.gpuMs[i] >= 0.0)
                    gpuMs[kVerifiedGpuPasses[i].key] = timing.gpuMs[i];
            }
            verifiedTimings.push_back({{"poseToSubmitTicks", timing.poseToSubmit},
                                       {"submitToReadyTicks", timing.submitToReady},
                                       {"readyToPresentTicks", timing.readyToPresent},
                                       {"gpuMs", std::move(gpuMs)}});
        }
        const nlohmann::json report = {{"version", 1},
                                       {"qpcFrequency", m_frequency},
                                       {"start", m_start},
                                       {"windowStart", m_windowStart},
                                       {"windowEnd", m_windowEnd},
                                       {"finished", m_finished},
                                       {"overflow", m_overflow},
                                       {"timingOnly", !m_capture},
                                       {"warmupSeconds", m_warmup},
                                       {"measureSeconds", m_duration},
                                       {"presents", m_presents - m_startPresent},
                                       {"freshPresents", m_freshPresents - m_startFresh},
                                       {"published", m_published - m_startPublished},
                                       {"verifiedProductionSubmissions", m_verifiedProductionSubmissions},
                                       {"verifiedFreshPresentations", m_verifiedFreshPresentations},
                                       {"verifiedFallbackPresentations", m_verifiedFallbackPresentations},
                                       {"verifiedPoseToPresentTicks", m_verifiedPoseToPresentTicks},
                                       {"verifiedTimings", std::move(verifiedTimings)},
                                       {"metadata", metadata},
                                       {"events", std::move(events)},
                                       {"gpu", std::move(gpu)},
                                       {"calibrations", std::move(calibrations)}};
        std::ofstream output(m_path, std::ios::binary);
        output << report.dump();
        if (!output)
            throw std::runtime_error("Failed to write RR fence trace");
        m_running = false;
        gpuMarker = {};
    }
    nlohmann::json metadata;
    std::function<void(const char*)> gpuMarker;

private:
    struct VerifiedGpuPass
    {
        const char* checkpoint;
        const char* key;
    };
    inline static constexpr std::array<VerifiedGpuPass, 5> kVerifiedGpuPasses = {{
        {"PathTracingPass", "ptPass"},
        {"PT RR Input Conversion Diagnostics", "rrConversionPass"},
        {"PT RR Input Admission", "rrAdmissionPass"},
        {"PT DLSS RR Native", "rrNativePass"},
        {"EndGpu", "productionPacket"},
    }};
    struct VerifiedTiming
    {
        uint64_t poseToSubmit, submitToReady, readyToPresent;
        std::array<double, kVerifiedGpuPasses.size()> gpuMs;
    };
    std::filesystem::path m_path;
    bool m_running = false, m_capture = false, m_finished = false, m_overflow = false;
    double m_warmup = 0, m_duration = 0;
    uint64_t m_frequency = 0, m_start = 0, m_windowStart = 0, m_windowEnd = 0, m_lastCalibration = 0;
    uint64_t m_frame = 0, m_tick = 0, m_submission = 0, m_production = 0, m_epoch = 0, m_lastGpu = 0;
    uint64_t m_presents = 0, m_freshPresents = 0, m_published = 0, m_lastPresented = 0;
    uint64_t m_verifiedProductionSubmissions = 0, m_verifiedFreshPresentations = 0, m_verifiedFallbackPresentations = 0;
    uint64_t m_verifiedPoseQpc = 0, m_verifiedSubmitQpc = 0, m_verifiedReadyQpc = 0;
    std::array<double, kVerifiedGpuPasses.size()> m_verifiedGpuMs = {};
    std::vector<uint64_t> m_verifiedPoseToPresentTicks;
    std::vector<VerifiedTiming> m_verifiedTimings;
    uint64_t m_startPresent = 0, m_startFresh = 0, m_startPublished = 0;
    std::vector<Event> m_events;
    std::vector<GpuPoint> m_gpu;
    std::vector<Calibration> m_calibrations;
};
inline RrFenceTrace g_rrFenceTrace;
} // namespace RtPbrSurvey
