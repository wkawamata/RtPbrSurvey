# Windows GUI Automation

Minimal procedure verified with RtPbrSurvey on 2026-10-08. This is a project
checklist, not a replacement for the installed Computer Use skill or its safety
rules. Read that skill's `SKILL.md`, `docs/guidance.md`, `docs/api.md`, and
`docs/confirmations.md` before operating Windows apps. Installed paths and tool
availability can change; use the skill/tool inventory supplied by the session.

## Select the Correct API

- Native Windows apps: `@oai/sky` through the persistent `node_repl` JavaScript tool
  (verified tool name: `mcp__node_repl__js`).
- Browser automation: use the available browser API separately.
- A `cua` connection reporting "Native computer APIs are disabled" describes that
  connection, not every Windows automation capability. Check the Windows skill
  and `sky` before reporting native apps unavailable.
- Do not replace `sky` with custom SendInput, PowerShell UI Automation, or helper
  executable protocols.

## Initialize and Observe

Run in `node_repl`, not a terminal. Keep persistent state on `globalThis`.

```javascript
if (!globalThis.sky) {
    const { sky } = await import("@oai/sky");
    globalThis.sky = sky;
}
globalThis.apps = await sky.list_apps();
nodeRepl.write(JSON.stringify(apps, null, 2));
```

Inspect the returned inventory, then select the intended executable's app ID.
Use the exact ID returned by the inventory; multiple workspaces may run apps
with the same display name. Never guess a window handle or silently pick the
first of multiple windows.

```javascript
globalThis.targetApp = apps.find(a => a.id === "<returned app ID>");
if (!targetApp || targetApp.windows.length !== 1) {
    throw new Error("Select exactly one returned target window");
}
globalThis.targetWindow = await sky.get_window(targetApp.windows[0]);
await sky.activate_window({ window: targetWindow });
globalThis.state = await sky.get_window_state({ window: targetWindow });
globalThis.targetWindow = state.window;
```

The screenshot is displayed automatically. Inspect it before choosing an action.
ImGui apps may expose little accessibility text; use the observed screenshot.

## One Action, Then Refresh

Use window-relative coordinates and the latest screenshot ID. Replace example
coordinates only after inspecting the current screenshot. Stop and inspect each
refreshed state before the next action.

```javascript
await sky.click({
    window: state.window,
    screenshotId: state.screenshots[0].id,
    x: 100, y: 130
});
globalThis.state = await sky.get_window_state({ window: targetWindow });
globalThis.targetWindow = state.window;
```

- For text: click the field, refresh and verify focus, then use `press_key` for
  `Control_L+a`, refresh, and `type_text` in a separate action. Reobserve if the
  change is not yet visible; never blindly retry typing.
- Use `scroll` for panels and `drag` for ROI/canvas gestures. Refresh after each.
- Do not reuse screenshot IDs, coordinates, or accessibility indexes after a
  layout/focus/modal change. A snapshot may precede processing of input; observe
  again before deciding the operation failed.

## Recovery and Validation

- If a click is blocked by another window, activate the returned target window,
  refresh, and retry once with the new observation.
- After app restart, refresh `list_apps`/`list_windows` and obtain a fresh returned
  window. Do not reuse the old handle. A launch timeout may precede window
  creation; inspect the inventory before launching a duplicate process.
- On repeated helper failure or a locked desktop, stop and report the exact
  condition. Follow the installed skill's bounded recovery procedure.
- GUI launch working directory is not necessarily the executable or repository
  directory. Use absolute paths under the current workspace's `bin/CapturePort`
  for validation outputs; do not overwrite user assets.
- GUI evidence, CLI evidence, and unit tests are distinct. Report which was
  actually exercised, including failures and remaining acceptance items.
- SDK-present CLI launch may require normal host permissions outside the agent
  sandbox. On 2026-10-09, Streamline plugin `weakly_canonical` received Access
  denied inside the sandbox and `slInit` crashed before D3D12 log creation.
  The unchanged executable completed a short GPU capture with an approved
  `require_escalated` launch. Request that execution permission; do not weaken
  Windows security or disable DLSS as a workaround. Capture native SDK diagnostics
  and distinguish this condition from renderer failures.
- Follow the skill's confirmation rules. Do not automate terminals, Run dialogs,
  authentication/security UI, or Codex UI. Do not use Windows-key shortcuts.
