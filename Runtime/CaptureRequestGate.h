#pragma once

#include <string>

namespace RtPbrSurvey
{
    // Host operation that must wait until capture output is fully processed.
    enum class PendingHostAction
    {
        None,
        SceneEditorNewDocument,
        SceneEditorLoadDocument,
        SceneEditorReturnToTopMenu,
        OpenSelectedScene,
        CloseRunningScene,
        CloseApplication,
    };

    // Capture request exclusivity and deferred exit or scene switch.
    // The gate stores host state only; it never waits for the GPU.
    class CaptureRequestGate
    {
    public:
        struct Inputs
        {
            bool captureSessionActive = false;
            bool singleScreenshotInFlight = false;
            // Future reservation or in-flight output, not a completed or failed automation.
            bool automatedCaptureBlocking = false;
            bool diagnosticCaptureInFlight = false;
            bool sceneEditorDecisionRequired = false;
        };

        void Update(const Inputs& inputs);

        bool IsWorkPending() const;

        // Consume a deferred preview once output is processed; leaving edit mode cancels it.
        bool TakePendingPreviewRebuild(bool& pending, bool editing) const;

        // Shared start decision used by the GUI button and the F8 shortcut.
        bool CanStart(std::string& reason) const;

        // Records a request. Application close outranks a scene switch, and a duplicate request
        // keeps the first recorded action of the same rank.
        PendingHostAction RequestPendingAction(PendingHostAction action);

        // Releases the recorded action when the user cancels a pending scene change.
        void ClearPendingAction();

        // True when the recorded action can run: capture output is processed and no confirmation
        // decision is still required.
        bool CanExecutePendingAction() const;

        // Returns the recorded action once and clears it.
        PendingHostAction TakeResolvedAction();

        PendingHostAction GetPendingAction() const;
        const char* GetPendingActionName() const;
        const Inputs& GetInputs() const;

    private:
        static int ActionRank(PendingHostAction action);

        Inputs m_inputs;
        PendingHostAction m_pendingAction = PendingHostAction::None;
    };
} // namespace RtPbrSurvey
