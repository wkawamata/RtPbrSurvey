#ifndef NRD_RELAX_INPUT_ADMISSION_HLSLI
#define NRD_RELAX_INPUT_ADMISSION_HLSLI

struct NrdAdmissionResult
{
    float4 diffuse;
    float4 specular;
    float4 viewZ;
    float4 admission;
};

uint InspectNrdAdmissionSignal(inout float4 signal, float4 state, out bool inactive)
{
    inactive = state.y == 4;
    if (any(!isfinite(signal)) || any(signal < 0) || any(signal.xyz > 65504) || signal.w > 65504.5 ||
        any(!isfinite(state)) || state.w != 0 || state.y < 1 || state.y > 4 || state.y != floor(state.y))
        return 3;
    if ((state.y == 1 && (state.x <= 0 || state.x > 65504 || state.z <= 0)) ||
        (state.y == 2 && (state.x != 0 || state.z <= 0)) ||
        (state.y >= 3 && (state.x != 0 || state.z != 0)))
        return 3;
    if (inactive)
    {
        if (any(signal.xyz != 0))
            return 3;
        signal = 0;
        return 0;
    }
    if (signal.w == 0)
        return 4;
    signal.w = min(signal.w, 65504);
    return 0;
}

NrdAdmissionResult AdmitNrdReconstruction(float4 diffuse, float4 specular, float4 diffuseState,
                                         float4 specularState, float4 validity, float4 viewZ)
{
    NrdAdmissionResult result = (NrdAdmissionResult)0;
    result.viewZ = float4(1e6, 0, 0, 0);
    result.admission.y = 1;
    if (any(!isfinite(validity)) || any(validity < 0) || any(validity > 1) ||
        any(validity != floor(validity)) || validity.w != 0 || validity.x != validity.y + validity.z ||
        any(!isfinite(viewZ)) || any(viewZ.yzw != 0))
    {
        result.admission.y = 2;
        return result;
    }
    if (validity.x != 1 || viewZ.x <= 0 || viewZ.x >= 1000)
        return result;
    bool diffuseInactive, specularInactive;
    uint diffuseCode = InspectNrdAdmissionSignal(diffuse, diffuseState, diffuseInactive);
    uint specularCode = InspectNrdAdmissionSignal(specular, specularState, specularInactive);
    uint code = diffuseCode == 3 || specularCode == 3 ? 3 : max(diffuseCode, specularCode);
    if (code != 0)
    {
        result.admission.y = code;
        return result;
    }
    result.diffuse = diffuse;
    result.specular = specular;
    result.viewZ = viewZ;
    result.admission = float4(1, 0, diffuseInactive, specularInactive);
    return result;
}

#endif
