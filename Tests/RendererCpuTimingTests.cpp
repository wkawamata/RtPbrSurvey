#include "Runtime/RendererCpuTiming.h"

#include <cmath>
#include <iostream>

int main()
{
    using namespace RtPbrSurvey;
    RendererCpuTimer timer;
    const auto start = RendererCpuTimer::TimePoint{};
    const auto at = [start](int ms) { return start + std::chrono::milliseconds(ms); };
    if (timer.Completed().available)
    {
        return 1;
    }
    timer.Begin(start);
    timer.Switch(RendererCpuPhase::RecordSubmit, at(2));
    timer.Switch(RendererCpuPhase::GpuWait, at(5));
    timer.Switch(RendererCpuPhase::RecordSubmit, at(9));
    timer.Switch(RendererCpuPhase::NrdBounds, at(10));
    timer.Switch(RendererCpuPhase::Present, at(11));
    timer.End(at(12));
    const auto completed = timer.Completed();
    const std::array<float, kRendererCpuPhaseCount> expected = {2, 4, 4, 1, 1};
    if (!completed.available || std::abs(completed.totalMs - 12.0f) > 0.001f)
    {
        return 1;
    }
    for (size_t i = 0; i < expected.size(); ++i)
    {
        if (std::abs(completed.ms[i] - expected[i]) > 0.001f)
        {
            return 1;
        }
    }
    timer.Begin(at(20));
    timer.Switch(RendererCpuPhase::RecordSubmit, at(21));
    if (!timer.Completed().available || timer.Completed().ms != completed.ms)
    {
        return 1;
    }
    timer.End(at(23));
    if (std::abs(timer.Completed().totalMs - 3.0f) > 0.001f)
    {
        return 1;
    }
    std::cout << "Renderer CPU timing tests passed.\n";
    return 0;
}
