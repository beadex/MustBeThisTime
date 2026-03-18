cbuffer LightSourceConstantBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(float3 position : POSITION)
{
    PSInput result;

    float4 worldPos = mul(float4(position, 1.0), model);
    float4 viewPos = mul(worldPos, view);
    result.position = mul(viewPos, projection);

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}