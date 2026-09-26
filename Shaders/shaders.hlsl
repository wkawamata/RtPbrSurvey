//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "Material.hlsli"
#include "SceneDrawConstants.hlsli"

struct InstanceData
{
    float4x4 world;
    float4x4 prevWorld;
    uint materialId;
    uint meshId;
    float padding[2]; //16 byte alignment
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4x4 invViewProj;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    uint instanceId : SV_InstanceID;
    nointerpolation uint materialId : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
};


Texture2D g_texture[] : register(t0, space0);
SamplerState g_sampler : register(s0);
StructuredBuffer<InstanceData> g_instanceData : register(t0, space1);
StructuredBuffer<Material> g_materialData : register(t0, space2);

#include "DirectLights.hlsli"

PSInput VSMain(float4 position : POSITION,
               float2 uv : TEXCOORD,
               float3 normal : NORMAL,
               float4 tangent : TANGENT,
               uint vertexMaterialId : MATERIALID,
               uint instanceId : SV_InstanceID)
{
    PSInput result;

    instanceId += sceneInstanceOffset;
    InstanceData inst = g_instanceData[instanceId];    

    float4x4 worldViewProj = mul(inst.world, viewProj);
    result.position = mul(float4(position.xyz, 1.0), worldViewProj);
    result.worldPosition = mul(float4(position.xyz, 1.0), inst.world).xyz;
    result.uv = uv;
    result.normal = normalize(mul(normal, (float3x3)inst.world));
    result.instanceId = instanceId;
    result.materialId = vertexMaterialId == 0xffffffff ? inst.materialId : vertexMaterialId;
    
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    Material mat = g_materialData[input.materialId];
    float2 materialUv = input.uv * mat.uvScale + mat.uvOffset;
    float4 albedo = g_texture[mat.albedoTexIndex].Sample(g_sampler, materialUv);
    float3 normal = normalize(input.normal);
    float3 ambient = albedo.rgb * iblIntensity * diffuseIblEnabled;
    float3 diffuse = 0.0;
    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        DirectLightSample light = EvaluateDirectLight(lightIndex, input.worldPosition);
        diffuse += albedo.rgb * light.radiance * saturate(dot(normal, light.surfaceToLight));
    }
    diffuse *= directLightEnabled;
    return float4(ambient + diffuse, albedo.a);
}
