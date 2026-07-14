# Project Vision

RtPbrSurvey is a benchmark and survey application for the transition from
rasterizer-centered real-time rendering pipelines to path tracing-centered
pipelines.

The project exists to compare the two pipeline families from practical,
measurable viewpoints: image quality, frame time, latency, memory usage, GPU
feature requirements, scene authoring cost, debugability, scalability, and
robustness across representative scenes.

## Current Status

The vision is larger than the current implementation. Today, RtPbrSurvey should
be understood as a foundation of test code: renderer experiments, scenes, debug
views, profiling hooks, and early comparison tools. It is not yet a complete
benchmark suite.

Important validation work is still missing. The project needs stronger
repeatability checks, reference-image comparisons, automated benchmark runs,
hardware and driver coverage, documented scene workloads, result export, and
clear pass/fail criteria before its measurements should be treated as
authoritative.

## Core Questions

- Where does a rasterizer pipeline still provide the best quality, latency, or
  engineering tradeoff?
- Where does a path tracing pipeline become simpler, more accurate, or more
  scalable than layered raster techniques?
- Which hybrid techniques provide useful intermediate steps between the two?
- How much do modern AI-enhanced techniques improve quality, stability, or
  performance for the same rendering budget?
- Which results can be shown through repeatable benchmarks rather than visual
  impressions alone?

## Benchmark Direction

RtPbrSurvey should grow benchmark tools that make these comparisons concrete:

- Side-by-side raster, hybrid ray tracing, and path tracing outputs.
- Per-pass GPU/CPU timing, memory, and resource-pressure reporting.
- Scene sets designed around known rendering stress cases.
- Image-quality comparisons against higher-quality references.
- AI-enhancement comparison modes for denoising, reconstruction, upscaling, and
  temporal stability.
- Exportable benchmark reports that make results easy to reproduce and share.

## Near-Term Scope

The current renderer already contains deferred/forward PBR, RayQuery shadows,
hybrid reflections, image-based lighting, debug views, and profiling
infrastructure. Near-term work should connect those features into clearer
benchmark workflows before expanding into a full path tracing pipeline.
