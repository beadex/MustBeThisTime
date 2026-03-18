cbuffer LightSourceConstantBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float padding;
};

cbuffer LightSourceDataConstantBuffer : register(b1)
{
    float4 lightColor;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(float3 position : POSITION)
{
    PSInput result;
    float4 worldPos = mul(float4(position, 1.0), model);
    
    result.position = mul(mul(worldPos, view), projection);

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return lightColor;
}