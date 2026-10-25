#include "PathTracingReconstructionPolicy.h"

#include <cmath>

namespace Engine
{
const char* PathTracingReconstructionBackendName(PathTracingReconstructionBackend backend)
{
    switch (backend)
    {
        case PathTracingReconstructionBackend::Legacy:
            return "legacy";
        case PathTracingReconstructionBackend::Native:
            return "native";
        case PathTracingReconstructionBackend::Nrd:
            return "nrd";
        case PathTracingReconstructionBackend::Rr:
            return "rr";
    }
    return "invalid";
}

PathTracingReconstructionBackend EffectivePathTracingBackend(PathTracingReconstructionBackend backend, bool legacyNrd)
{
    if (backend == PathTracingReconstructionBackend::Legacy)
        return legacyNrd ? PathTracingReconstructionBackend::Nrd : PathTracingReconstructionBackend::Native;
    return backend;
}

PathTracingReconstructionPlan PlanPathTracingReconstruction(
    PathTracingReconstructionBackend backend, bool legacyNrd, bool diagnostics, bool inspectNrd, bool inspectRr)
{
    PathTracingReconstructionPlan plan;
    plan.backend = EffectivePathTracingBackend(backend, legacyNrd);
    plan.runNrd = plan.backend == PathTracingReconstructionBackend::Nrd || diagnostics || inspectNrd;
    plan.runRrConversion = plan.backend == PathTracingReconstructionBackend::Rr || diagnostics || inspectRr;
    plan.diagnosticWork = diagnostics || (plan.runNrd && plan.backend != PathTracingReconstructionBackend::Nrd) ||
                          (plan.runRrConversion && plan.backend != PathTracingReconstructionBackend::Rr);
    return plan;
}

PathTracingReconstructionPresentation ResolvePathTracingReconstructionPresentation(
    PathTracingReconstructionBackend requested, bool built, bool backendReady, bool inputsCompatible, bool outputValid)
{
    using Backend = PathTracingReconstructionBackend;
    using Status = PathTracingReconstructionStatus;
    if (requested == Backend::Native || requested == Backend::Legacy)
    {
        return {Backend::Native, Backend::Native, Status::Native};
    }
    if (requested != Backend::Nrd && requested != Backend::Rr)
    {
        return {requested, Backend::Native, Status::BackendUnavailable};
    }
    Status status = Status::Ready;
    if (!built)
    {
        status = Status::NotBuilt;
    }
    else if (!backendReady)
    {
        status = Status::BackendUnavailable;
    }
    else if (!inputsCompatible)
    {
        status = Status::IncompatibleInputs;
    }
    else if (!outputValid)
    {
        status = Status::AwaitingEvaluation;
    }
    return {requested, status == Status::Ready ? requested : Backend::Native, status};
}

const char* PathTracingReconstructionStatusName(PathTracingReconstructionStatus status)
{
    switch (status)
    {
        case PathTracingReconstructionStatus::Native:
            return "native";
        case PathTracingReconstructionStatus::NotBuilt:
            return "not-built";
        case PathTracingReconstructionStatus::BackendUnavailable:
            return "backend-unavailable";
        case PathTracingReconstructionStatus::IncompatibleInputs:
            return "incompatible-inputs";
        case PathTracingReconstructionStatus::AwaitingEvaluation:
            return "awaiting-evaluation";
        case PathTracingReconstructionStatus::Ready:
            return "ready";
    }
    return "invalid";
}

void PathTracingReconstructionHistoryState::Invalidate(PathTracingReconstructionBackend preserved)
{
    for (PathTracingReconstructionBackend backend :
         {PathTracingReconstructionBackend::Nrd, PathTracingReconstructionBackend::Rr})
    {
        if (backend != preserved)
        {
            RequestReset(backend);
        }
    }
}

void PathTracingReconstructionHistoryState::RequestReset(PathTracingReconstructionBackend backend)
{
    if (backend == PathTracingReconstructionBackend::Nrd || backend == PathTracingReconstructionBackend::Rr)
    {
        const size_t index = backend == PathTracingReconstructionBackend::Nrd ? 0 : 1;
        m_reset[index] = true;
        ++m_epoch[index];
    }
}

void PathTracingReconstructionHistoryState::AcknowledgeReset(PathTracingReconstructionBackend backend)
{
    if (backend == PathTracingReconstructionBackend::Nrd || backend == PathTracingReconstructionBackend::Rr)
    {
        m_reset[backend == PathTracingReconstructionBackend::Nrd ? 0 : 1] = false;
    }
}

bool PathTracingReconstructionHistoryState::ResetRequired(PathTracingReconstructionBackend backend) const
{
    return (backend == PathTracingReconstructionBackend::Nrd || backend == PathTracingReconstructionBackend::Rr) &&
           m_reset[backend == PathTracingReconstructionBackend::Nrd ? 0 : 1];
}

uint64_t PathTracingReconstructionHistoryState::Epoch(PathTracingReconstructionBackend backend) const
{
    if (backend != PathTracingReconstructionBackend::Nrd && backend != PathTracingReconstructionBackend::Rr)
    {
        return 0;
    }
    return m_epoch[backend == PathTracingReconstructionBackend::Nrd ? 0 : 1];
}

void AddPathTracingReconstructionTiming(PathTracingReconstructionTimings& timings,
                                        std::string_view pass,
                                        float milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 0)
        return;
    PathTracingStageTiming* stage = nullptr;
    if (pass == "PT NRD Conversion Diagnostics")
        stage = &timings.nrdConversion;
    else if (pass == "PT NRD Hit Distance Reconstruction 3x3" || pass == "PT NRD Reconstruction Input Admission")
        stage = &timings.nrdReconstruction;
    else if (pass == "PT NRD RELAX GPU Diagnostics")
        stage = &timings.nrdSdk;
    else if (pass == "PT NRD Material Restore and Composition")
        stage = &timings.nrdComposition;
    else if (pass == "PT RR Input Conversion Diagnostics")
        stage = &timings.rrConversion;
    else if (pass == "PT DLSS RR Native")
        stage = &timings.rrSdk;
    if (stage)
    {
        stage->available = true;
        stage->milliseconds += milliseconds;
    }
}
} // namespace Engine
