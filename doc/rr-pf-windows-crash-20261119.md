# PF GPU validation hold: Windows freeze / crash, 2026-11-19

Status: GPU validation stopped. PF-04 cohort and PF-05 performance conclusions are
not complete. Do not automatically resume after reboot or rerun/maximize the app
to reproduce this incident. The committed pf_gpu_validation_hold.json blocks the
PF runner before launching the renderer or nvidia-smi. Other validation commands
must not be launched for this investigation until the hold is explicitly cleared.

## User report

The screen went black/froze and the user manually restarted Windows. The user
subsequently clarified that a maximization-related dark-screen/freeze occurred
shortly BEFORE the PF measurements started. This is recalled chronology, not a
controlled reproduction or proof of a resize root cause. It weakens the hypothesis
that the newly enabled PF measurement alone introduced the issue; existing RR,
resize and driver interactions remain candidates. When the user proposed launching
the glass scene with RR and maximizing it again, the recommendation was to retain
the hold and avoid repeating a whole-system freeze.

## Confirmed local evidence

- System WER Event 1001 at approximately 09:30 reports bugcheck 0x133, argument 1=1,
  argument 2=0x1e00 and a minidump. Dump debug-session time is 09:28:57 (UTC+9).
- The subsequent boot around 15:46 has Kernel-Power 41 / EventLog 6008. The latest
  recorded LastBootUpTime was 15:46:40. These record an unclean restart; they do not
  establish the same bugcheck or root cause for the later freeze/manual restart.
- The known dump, 111926-14296-01.dmp, was copied with explicit user-approved UAC
  from Windows/Minidump into ignored build/pf-crash-incident-20261119/dumps.
  It remains PC-local; no dump, memory contents or user configuration was uploaded.
- Dump SHA256: 91297935d90f63a8fa079a9d8b7a3636eb71d72cceac02a5666b8b13fff0602a.
- Offline reading with the installed Windows DbgEng API, the local matching kernel
  image and Microsoft public kernel symbols shows the stack through:
  nt!KiIdleLoop -> nt!KiRetireDpcList -> nt!KiExecuteAllDpcs ->
  nvlddmkm+0xef8d3 -> nvlddmkm+0x109204 -> nvlddmkm+0x278764 ->
  nvlddmkm+0xe3b6f -> interrupt/clock accounting -> nt!KeBugCheckEx.
- The NVIDIA module identifies version 621.24 / 32.0.16.2124. The local kernel image
  matches dump image size and reproducible image hash; its matching PDB GUID/age
  was obtained from the PE CodeView record before downloading public symbols.

This is evidence that the captured DPC path involves the NVIDIA kernel driver.
It does not prove that the driver alone is defective, exonerate the application,
or prove that the later manual-reboot incident has the same cause. With cumulative
0x133 argument 1=1 the stopped stack is not necessarily the complete offending
history. No NVIDIA private symbols or full !analyze extension were available;
the result is a manual stack/module inspection, not a full WinDbg automatic analysis.

Microsoft describes 0x133 as a DPC watchdog timeout. Argument 1=1 represents a
cumulative prolonged interval at DISPATCH_LEVEL or above:
[Bug Check 0x133](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x133-dpc-watchdog-violation).
Only public OS symbols were fetched from the
[Microsoft public symbol server](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/microsoft-public-symbols).
No debugger/driver software was installed and no driver/TDR/registry configuration
was changed. The small dump reader and downloaded headers/symbols are local build
artifacts, not part of the product or Git.

## Process and data handling

PF measurement controllers and renderer/monitor processes are not running. Prior
partial cohorts remain preserved with incident-stop/interruption notes. They are
not accepted as completed balanced performance cohorts. Individual completed traces
may be examined offline, but provisional rate differences are not final findings.

Earlier process disappearances were retried before checking for OS reboot evidence.
That was inappropriate; absence of an app process is not evidence of an ordinary
harness interruption. The investigation now prioritizes crash evidence and code
inspection over additional load. The hold must not auto-clear after a restart.

## Maximization / resize audit, without reproduction

OnWindowSizeChanged requests deferred resize. ApplyResize currently drains the GPU,
releases RR SDK resolution resources via slFreeResources, releases swapchain
backbuffers, calls ResizeBuffers, and rebuilds size-dependent textures/descriptors.
That order is present in the code; it is not proof that every lifetime/SDK condition
is correct. Review pending RR tickets, epoch invalidation, old/new frame fences,
ComPtr/tagged resources, descriptors and SDK feature allocation lifetime across this
boundary. Repeated app startup/shutdown also deserves review because cohort failures
occurred during repeated process runs; the exact freezing call is not preserved.

Other candidates for review include the new clock-calibration/query instrumentation,
GPU telemetry sampling, and the existing RR/driver interaction. None is established
as the cause. Do not disable a candidate and immediately restart stress tests to
infer causation on this PC while the incident remains unresolved.

The follow-up [offline lifetime and trace audit](rr-crash-offline-audit-20261119.md)
records checked paths, evidence limitations and the next diagnostic design.

## Next work permitted without GPU execution

1. Preserve System/WER events, known dump hash, driver/SDK/build identities and
   incomplete-run timestamps. Distinguish new incidents from older WER reports
   requeued after boot; several reports mention much older dump dates.
2. Continue offline dump and source-lifetime analysis. The copied 09:28 dump is
   available; no corresponding dump for the latest forced reboot is confirmed.
3. Review resize/feature-release and fence ownership paths, plus trace hooks,
   without starting RtPbrSurvey, nvidia-smi or another GPU test.
4. Write a specific suspected mechanism and minimal change before proposing any
   controlled reproduction. Resumption requires explicit agreement on a safe test
   procedure. No automatic driver rollback, TDR extension or reboot.

## Local evidence

All raw material stays under build/pf-crash-incident-20261119:

- system-events.json and relevant-wer.json.
- dump-copy-result.json and dumps/111926-14296-01.dmp.
- kernel-image-info.json, dump-symbolized.txt and dump-triage.txt.
- tools/dump_read.cpp, Microsoft interface header, local dump reader and symbols.

Existing PF data stays under build/pf-static-20261119, build/pf-study-20261119 and
build/pf-resumable-static-20261119 with incident-stop notes. These folders and all
dumps/logs remain excluded from Git. Only this summary and the automation hold are
reviewable source changes.
