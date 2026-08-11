# Executable-relative runtime assets

`RtPbrSurveyEngine` resolves renderer-owned runtime assets from the executable
directory:

```text
<executable directory>\Assets\
```

This matches `rtpbrsurvey_copy_runtime_files(target)`, including external hosts
whose process working directory differs from the target output directory.
Changing the process working directory is not required.

The path is internal to the renderer. Host-owned relative paths, including
configuration files, continue to use the host's existing path policy.
