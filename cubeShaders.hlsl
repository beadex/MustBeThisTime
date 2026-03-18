cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 normalMatrix;
};

// New: light data
cbuffer LightConstantBuffer : register(b1)
{
    float3 lightPosition;
    float padding0;
    float4 lightColor;
    float3 cameraPos;
    float padding1;
}

struct VS_Input
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

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

PSInput VSMain(VS_Input input)
{
    PSInput result;
    float4 worldPos = mul(float4(input.position, 1.0), model);
    result.worldPos = worldPos.xyz;

    // Use the 3x3 part of the matrix for the normal
    result.normal = normalize(mul(input.normal, (float3x3) normalMatrix));
    
    result.position = mul(mul(worldPos, view), projection);
    result.uv = input.uv;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float ambientStrength = 0.2f;
    float4 ambient = ambientStrength * lightColor;

    // Diffuse
    float3 norm = normalize(input.normal);
    float3 lightDir = normalize(lightPosition - input.worldPos);
    float diff = max(dot(norm, lightDir), 0.0f);
    float4 diffuse = diff * lightColor;

    // Specular
    float specularStrength = 1.0f;
    float3 viewDir = normalize(cameraPos - input.worldPos); // ← fix: use cameraPos
    float3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32);
    float4 specular = specularStrength * spec * lightColor; // ← fix: float3
    
    float4 diffuseMapColor = g_diffuseMap.Sample(g_sampler, input.uv).rgba;
    float4 specularMapColor = g_specularMap.Sample(g_sampler, input.uv).rgba;

    float4 result = ambient * diffuseMapColor + diffuse * diffuseMapColor + specular * specularMapColor;

    return result;
}