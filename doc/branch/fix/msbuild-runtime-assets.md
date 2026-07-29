# MSBuild runtime assets

The standalone Visual Studio project copies `Assets/**` beside
`RtPbrSurvey.exe` after each build. This matches the executable-relative
runtime asset contract used by the CMake host helper.

Both build paths therefore provide:

```text
<executable directory>\Assets\Environment\default_environment.hdr
```

The renderer does not depend on the process working directory.
