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
    float4 clearColor;
    float3 cameraPos;
    float padding1;
};

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
    float3 norm = normalize(input.normal);
    float3 lightDir = normalize(lightPosition - input.worldPos);
    float3 viewDir = normalize(cameraPos - input.worldPos);
    float3 reflectDir = reflect(-lightDir, norm);

    float4 diffuseMapColor = g_diffuseMap.Sample(g_sampler, input.uv);
    float4 specularMapColor = g_specularMap.Sample(g_sampler, input.uv);

    float diff = max(dot(norm, lightDir), 0.0f);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f);

    float ambientStrength = 0.8f;
    float specularStrength = 1.6f;
    float clearSpecularInfluence = 2.0f;

    // Ambient = pure light + clear
    float3 ambientTint = saturate(lightColor.rgb + clearColor.rgb);
    float3 ambient = ambientStrength * ambientTint * diffuseMapColor.rgb;

    // Diffuse = only light color
    float3 diffuse = diff * lightColor.rgb * diffuseMapColor.rgb;

    // Specular = more influenced by clearColor than lightColor
    float fresnel = pow(1.0f - saturate(dot(viewDir, norm)), 5.0f);
    float3 specularTint = saturate(lightColor.rgb + (clearSpecularInfluence * clearColor.rgb));
    float3 specular = specularStrength * spec * (0.25f + 0.75f * fresnel) * specularTint * specularMapColor.rgb;

    float3 finalColor = ambient + diffuse + specular;
    return float4(saturate(finalColor), diffuseMapColor.a);
}