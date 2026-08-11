# SceneBuilder Host Textures

## Public Contract

External hosts can append decoded or procedurally generated RGBA8 pixels without file I/O:

```cpp
std::vector<uint8_t> pixels = BuildGridTileRgba8();

const uint32_t textureIndex = builder.AddTextureRGBA8(
    tileWidth,
    tileHeight,
    pixels);

const float floorSizeMeters = 200.0f;
const float tileSizeMeters = 1.0f;
const DirectX::XMFLOAT2 uvScale = {
    floorSizeMeters / tileSizeMeters,
    floorSizeMeters / tileSizeMeters,
};
const uint32_t materialIndex = builder.AddTexturedMaterial(
    textureIndex,
    uvScale);

builder.AppendCube(1.0f, materialIndex);
builder.AddInstance(
    DirectX::XMMatrixScaling(floorSizeMeters, 0.1f, floorSizeMeters),
    materialIndex);
```

`AddTextureRGBA8()` copies the supplied pixels. The host may release or reuse its source buffer after the call. Width and height must be positive, and the span size must be exactly `width * height * 4`. The returned index remains stable for the lifetime of the builder mesh.

`AddTexturedMaterial()` creates a non-metallic, fully rough material whose albedo references the supplied texture. Other texture semantics keep their renderer fallbacks. It validates the texture index before appending the material.

## UV Transform

`SceneMaterial::uvScale` and `uvOffset` apply to every texture semantic:

```text
materialUv = meshUv * uvScale + uvOffset
```

Both fields default to identity, so existing glTF, solid-color, and procedural scene behavior is unchanged. A procedural cube face spans UV 0 through 1. For a 200 m floor whose tile represents 1 m, use a scale of `(200, 200)`. A 10 m tile would use `(20, 20)`.

## Mipmaps And Filtering

Host textures added through `AddTextureRGBA8()` generate a complete mip chain by default. The default
`SceneTextureOptions` treats RGB as sRGB color: downsampling averages in linear color space and converts back to
sRGB-encoded RGBA8. Alpha is always averaged linearly.

For non-color data such as normal, metallic, roughness, or occlusion textures, request linear averaging:

```cpp
Engine::SceneTextureOptions options;
options.colorSpace = Engine::TextureColorSpace::Linear;
const uint32_t dataTexture = builder.AddTextureRGBA8(width, height, pixels, options);
```

Set `options.generateMipmaps = false` only when a single mip is intentional.

Existing glTF textures retain their previous single-mip behavior. The renderer uploads every generated mip and exposes the complete chain through the texture SRV. Scene texture samplers use wrap addressing and 8x anisotropic filtering.

Rasterized Forward and Deferred material sampling uses automatic mip selection. Hybrid Reflection material sampling currently uses explicit LOD 0 because the compute RayQuery path has no screen-space texture derivatives or ray-cone LOD estimate. The host API does not imply filtered ray-hit texture LOD until that separate policy is implemented.

## Ownership

- The host owns image decoding, procedural pixel generation, and asset paths.
- `SceneBuilder` owns validation and copies host RGBA8 data into the scene.
- The renderer owns mip generation, GPU resources, upload lifetime, SRVs, UV-transform semantics, and sampler behavior.
- The contract contains no D3D12 resource or descriptor types.
