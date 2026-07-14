# Public Release Checklist

This repository is already visible on GitHub, so use this checklist before
announcing or sharing the project more broadly.

## Required Before Announcement

- [ ] Decide whether `feature/scene-renderer-layer` should be merged to `main`
      before public promotion.
- [ ] Confirm that the README and `doc/PROJECT_VISION.md` describe the intended
      benchmark direction clearly enough for first-time visitors.
- [ ] Verify every committed runtime asset has a source URL and compatible
      license notice.
- [ ] Resolve or remove assets currently marked as license blockers in
      `THIRD_PARTY_NOTICES.md`.
- [ ] Keep bundled assets documented as free-use validation/demo data, and do
      not present them as MIT-licensed application source.
- [ ] Decide whether the non-commercial DamagedHelmet asset is acceptable for
      the repository's public demo and validation use.
- [ ] Run a clean Debug build from a fresh checkout after `Restore-NuGet.ps1`.
- [ ] Run the D3D12 debug-layer automation from `AGENTS.md` and confirm no
      `[ERROR]` lines are logged.
- [ ] Review `doc/branch/` notes and remove internal planning tasks that should
      not be part of the public-facing history.

## Current Local Audit

- GitHub repository: `wkawamata/RtPbrSurvey`
- GitHub visibility observed by Codex: public
- Local branch: `feature/scene-renderer-layer`
- Local generated files are ignored by `.gitignore`: `.vs/`, `bin/`, `obj/`,
  `packages/`, `vcpkg_installed/`, `imgui.ini`, `d3d12_debug.log`, and
  `*.vcxproj.user`.
- No tracked generated files matching those ignore patterns were found in the
  local audit.
