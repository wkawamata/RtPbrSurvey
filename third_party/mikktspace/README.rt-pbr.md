# MikkTSpace

Upstream: https://github.com/mmikk/MikkTSpace
Revision: `3e895b49d05ea07e4c2133156cfa94369e19e409`
Retrieved: 2026-10-29

`mikktspace.c` and `mikktspace.h` are upstream sources with their license notices
preserved. Only line endings are normalized to CRLF. They are compiled as C++
without the application's precompiled header. RtPbrSurvey's adapter is separate
in `Scene/PathTracingTangents.cpp`.

Generated tangent frames are stored per triangle corner. Existing raster/Hybrid
geometry is not rewritten; only the Native PT shading view consumes them.
