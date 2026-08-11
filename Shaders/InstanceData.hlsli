struct InstanceData
{
    float4x4 world;
    float4x4 prevWorld;
    uint materialId;
    uint meshId;
    float padding[2];
};
