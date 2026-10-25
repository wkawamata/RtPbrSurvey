#include "Renderer/PathTracingReconstructionPolicy.h"

#include <iostream>
#include <limits>
#include <stdexcept>

int main()
{
    using Backend = Engine::PathTracingReconstructionBackend;
    const auto check = [](bool value, const char* reason)
    {
        if (!value)
            throw std::runtime_error(reason);
    };
    try
    {
        using Status = Engine::PathTracingReconstructionStatus;
        for (Backend backend : {Backend::Nrd, Backend::Rr})
        {
            for (unsigned int flags = 0; flags < 16; ++flags)
            {
                const bool built = (flags & 1) != 0;
                const bool ready = (flags & 2) != 0;
                const bool compatible = (flags & 4) != 0;
                const bool validOutput = (flags & 8) != 0;
                const auto result = Engine::ResolvePathTracingReconstructionPresentation(
                    backend, built, ready, compatible, validOutput);
                check(result.requested == backend, "Request must remain visible during fallback");
                check(result.output == (flags == 15 ? backend : Backend::Native),
                      "Only current, admitted output from an available backend may be displayed");
                const Status reason = !built         ? Status::NotBuilt
                                      : !ready       ? Status::BackendUnavailable
                                      : !compatible  ? Status::IncompatibleInputs
                                      : !validOutput ? Status::AwaitingEvaluation
                                                     : Status::Ready;
                check(result.status == reason, "Fallback reason priority is shared by NRD and RR");
            }
        }
        check(
            Engine::ResolvePathTracingReconstructionPresentation(Backend::Native, false, false, false, false).status ==
                Status::Native,
            "Native does not depend on reconstruction readiness");
        check(Engine::ResolvePathTracingReconstructionPresentation(static_cast<Backend>(99), true, true, true, true)
                      .output == Backend::Native,
              "Unknown backend cannot expose an output");

        Engine::PathTracingReconstructionHistoryState history;
        check(history.ResetRequired(Backend::Nrd) && history.ResetRequired(Backend::Rr), "First history needs reset");
        history.AcknowledgeReset(Backend::Nrd);
        history.AcknowledgeReset(Backend::Rr);
        const uint64_t nrdEpoch = history.Epoch(Backend::Nrd);
        const uint64_t rrEpoch = history.Epoch(Backend::Rr);
        history.Invalidate(Backend::Nrd);
        check(!history.ResetRequired(Backend::Nrd) && history.Epoch(Backend::Nrd) == nrdEpoch &&
                  history.ResetRequired(Backend::Rr) && history.Epoch(Backend::Rr) == rrEpoch + 1,
              "NRD camera history preservation must not preserve RR history");
        history.Invalidate();
        check(history.ResetRequired(Backend::Nrd) && history.ResetRequired(Backend::Rr) &&
                  history.Epoch(Backend::Nrd) == nrdEpoch + 1 && history.Epoch(Backend::Rr) == rrEpoch + 2,
              "Backend switch or scene reset invalidates both history epochs");
        history.AcknowledgeReset(Backend::Nrd);
        const uint64_t unaffectedRr = history.Epoch(Backend::Rr);
        history.RequestReset(Backend::Nrd);
        check(history.ResetRequired(Backend::Nrd) && history.Epoch(Backend::Rr) == unaffectedRr,
              "SDK-local failure must not reset another SDK history");
        check(!history.ResetRequired(Backend::Native) && history.Epoch(Backend::Native) == 0,
              "Native has no reconstruction SDK history");

        auto plan = Engine::PlanPathTracingReconstruction(Backend::Native, true, false, false, false);
        check(!plan.runNrd && !plan.runRrConversion && !plan.diagnosticWork, "Explicit Native overrides legacy NRD");
        plan = Engine::PlanPathTracingReconstruction(Backend::Nrd, false, false, false, false);
        check(plan.runNrd && !plan.runRrConversion && !plan.diagnosticWork, "NRD must not run RR conversion");
        plan = Engine::PlanPathTracingReconstruction(Backend::Rr, true, false, false, false);
        check(!plan.runNrd && plan.runRrConversion && !plan.diagnosticWork, "RR must not run NRD");
        plan = Engine::PlanPathTracingReconstruction(Backend::Legacy, true, false, false, false);
        check(plan.backend == Backend::Nrd && plan.runNrd && !plan.runRrConversion, "Legacy NRD selection preserved");
        plan = Engine::PlanPathTracingReconstruction(Backend::Native, false, false, true, false);
        check(plan.runNrd && !plan.runRrConversion && plan.diagnosticWork, "Inspector override must be identified");
        plan = Engine::PlanPathTracingReconstruction(Backend::Rr, false, true, false, false);
        check(plan.runNrd && plan.runRrConversion && plan.diagnosticWork, "Explicit validation mode may run both");
        Engine::PathTracingReconstructionTimings timings;
        check(!timings.rrSdk.available && !timings.nrdSdk.available,
              "Unexecuted work is unavailable, not measured zero");
        Engine::AddPathTracingReconstructionTiming(timings, "PT NRD Hit Distance Reconstruction 3x3", .25f);
        Engine::AddPathTracingReconstructionTiming(timings, "PT NRD Reconstruction Input Admission", .5f);
        Engine::AddPathTracingReconstructionTiming(timings, "PT NRD RELAX GPU Diagnostics", 2);
        Engine::AddPathTracingReconstructionTiming(timings, "PT RR Input Conversion Diagnostics", .125f);
        Engine::AddPathTracingReconstructionTiming(timings, "DLSS Ray Reconstruction Pass", 7);
        Engine::AddPathTracingReconstructionTiming(timings, "PT DLSS RR Native", -1);
        Engine::AddPathTracingReconstructionTiming(
            timings, "PT DLSS RR Native", std::numeric_limits<float>::quiet_NaN());
        check(timings.nrdReconstruction.available && timings.nrdReconstruction.milliseconds == .75f &&
                  timings.nrdSdk.available && timings.nrdSdk.milliseconds == 2 && timings.rrConversion.available &&
                  !timings.rrSdk.available,
              "GPU stages must be distinct; Hybrid and invalid intervals do not count as PT RR");
        std::cout << "Backend scheduling and GPU timing attribution passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
