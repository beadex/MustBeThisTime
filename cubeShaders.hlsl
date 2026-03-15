cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 mvp;
};

// New: light data
cbuffer LightConstantBuffer : register(b1)
{
    float3 lightPosition;
    float padding0; // keep 16-byte alignment
}

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float3 worldPos : POSITION; // new
    float2 uv : TEXCOORD;
};

Texture2D g_diffuseMap : register(t0);
Texture2D g_specularMap : register(t1);
SamplerState g_sampler : register(s0);

PSInput VSMain(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD)
{
    PSInput result;

    // still using mvp for clip-space
    result.position = mul(float4(position, 1.0f), mvp);

    // for now treat object space as world space (your cubes share same unit mesh)
    result.worldPos = position;

    result.normal = normalize(normal);
    result.uv = uv;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // Real light direction from fragment to light (approx: object space == world space)
    float3 fragPos = input.worldPos;
    float3 L = normalize(lightPosition - fragPos);

    float3 n = normalize(input.normal);
    float diff = max(dot(n, L), 0.0f);

    float3 baseDiffuse = g_diffuseMap.Sample(g_sampler, input.uv).rgb;
    float3 baseSpecular = g_specularMap.Sample(g_sampler, input.uv).rgb;

    float ambientStrength = 0.2f;
    float specularStrength = 0.3f;

    float3 ambient = ambientStrength * baseDiffuse;
    float3 diffuse = diff * baseDiffuse;
    float3 specular = specularStrength * diff * baseSpecular;

    float3 color = ambient + diffuse + specular;
    return float4(color, 1.0f);
}