#include "stdafx.h"

#include "Runtime/CaptureRequestGate.h"

namespace RtPbrSurvey
{
    namespace
    {
        const char* kPendingActionReason =
            "A pending exit or scene switch is waiting for capture output to complete.";
        const char* kSessionActiveReason = "A capture session is already active.";
        const char* kAutomatedCaptureReason =
            "Capture session is unavailable while automated capture is pending.";
        const char* kSingleRequestReason = "A single screenshot request is still saving.";
        const char* kDiagnosticReason = "A diagnostic capture is still saving.";
    } // namespace

    void CaptureRequestGate::Update(const Inputs& inputs)
    {
        m_inputs = inputs;
    }

    bool CaptureRequestGate::IsWorkPending() const
    {
        return m_inputs.captureSessionActive || m_inputs.singleScreenshotInFlight ||
               m_inputs.diagnosticCaptureInFlight;
    }

    bool CaptureRequestGate::TakePendingPreviewRebuild(bool& pending, bool editing) const
    {
        if (!editing)
        {
            pending = false;
            return false;
        }
        if (!pending || IsWorkPending())
        {
            return false;
        }
        pending = false;
        return true;
    }

    bool CaptureRequestGate::CanStart(std::string& reason) const
    {
        if (m_pendingAction != PendingHostAction::None)
        {
            reason = kPendingActionReason;
            return false;
        }
        if (m_inputs.captureSessionActive)
        {
            reason = kSessionActiveReason;
            return false;
        }
        if (m_inputs.automatedCaptureBlocking)
        {
            reason = kAutomatedCaptureReason;
            return false;
        }
        if (m_inputs.singleScreenshotInFlight)
        {
            reason = kSingleRequestReason;
            return false;
        }
        if (m_inputs.diagnosticCaptureInFlight)
        {
            reason = kDiagnosticReason;
            return false;
        }
        reason.clear();
        return true;
    }

    PendingHostAction CaptureRequestGate::RequestPendingAction(PendingHostAction action)
    {
        if (action == PendingHostAction::None)
        {
            return m_pendingAction;
        }
        if (m_pendingAction == PendingHostAction::None || ActionRank(action) > ActionRank(m_pendingAction))
        {
            m_pendingAction = action;
        }
        return m_pendingAction;
    }

    void CaptureRequestGate::ClearPendingAction()
    {
        m_pendingAction = PendingHostAction::None;
    }

    bool CaptureRequestGate::CanExecutePendingAction() const
    {
        return m_pendingAction != PendingHostAction::None && !IsWorkPending() &&
               !m_inputs.sceneEditorDecisionRequired;
    }

    PendingHostAction CaptureRequestGate::TakeResolvedAction()
    {
        if (!CanExecutePendingAction())
        {
            return PendingHostAction::None;
        }
        const PendingHostAction action = m_pendingAction;
        m_pendingAction = PendingHostAction::None;
        return action;
    }

    PendingHostAction CaptureRequestGate::GetPendingAction() const
    {
        return m_pendingAction;
    }

    const char* CaptureRequestGate::GetPendingActionName() const
    {
        switch (m_pendingAction)
        {
            case PendingHostAction::SceneEditorNewDocument:
                return "new Scene Document";
            case PendingHostAction::SceneEditorLoadDocument:
                return "Scene Document load";
            case PendingHostAction::SceneEditorReturnToTopMenu:
                return "return to TopMenu";
            case PendingHostAction::OpenSelectedScene:
                return "scene switch";
            case PendingHostAction::CloseRunningScene:
                return "scene close";
            case PendingHostAction::CloseApplication:
                return "application exit";
            default:
                return "none";
        }
    }

    const CaptureRequestGate::Inputs& CaptureRequestGate::GetInputs() const
    {
        return m_inputs;
    }

    int CaptureRequestGate::ActionRank(PendingHostAction action)
    {
        switch (action)
        {
            case PendingHostAction::CloseApplication:
                return 2;
            case PendingHostAction::CloseRunningScene:
                return 1;
            default:
                return 0;
        }
    }
} // namespace RtPbrSurvey
