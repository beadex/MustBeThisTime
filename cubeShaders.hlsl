cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 normalMatrix;
};

struct DirectionalLight
{
    float4 direction;
    float4 ambient;
    float4 diffuse;
    float4 specular;
};

struct PointLight
{
    float4 position; // xyz = position
    float4 attenuation; // x = constant, y = linear, z = quadratic
    float4 ambient;
    float4 diffuse;
    float4 specular;
};

struct SpotLight
{
    float4 position;
    float4 direction;
    float4 cutoffs; // x = inner cutoff cosine, y = outer cutoff cosine
    float4 attenuation; // x = constant, y = linear, z = quadratic
    float4 ambient;
    float4 diffuse;
    float4 specular;
};

cbuffer LightingConstants : register(b1)
{
    DirectionalLight directionalLight;
    SpotLight spotLight;
    float4 viewPos;
    uint pointLightCount;
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
    float3 worldPos : POSITION;
    float2 uv : TEXCOORD;
};

Texture2D g_diffuseMap : register(t0);
Texture2D g_specularMap : register(t1);
StructuredBuffer<PointLight> g_pointLights : register(t2);
SamplerState g_sampler : register(s0);

float3 CalcDirLight(DirectionalLight light, float3 normal, float3 viewDir, float2 uv);
float3 CalcPointLight(PointLight light, float3 normal, float3 fragPos, float3 viewDir, float2 uv);
float3 CalcSpotLight(SpotLight light, float3 normal, float3 fragPos, float3 viewDir, float2 uv);

PSInput VSMain(VS_Input input)
{
    PSInput result;

    float4 worldPos = mul(float4(input.position, 1.0f), model);
    result.worldPos = worldPos.xyz;

    result.normal = normalize(mul(input.normal, (float3x3) normalMatrix));
    result.position = mul(mul(worldPos, view), projection);
    result.uv = input.uv;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 norm = normalize(input.normal);
    float3 viewDir = normalize(viewPos.xyz - input.worldPos);

    float3 result = CalcDirLight(directionalLight, norm, viewDir, input.uv);

    [loop]
    for (uint i = 0; i < pointLightCount; ++i)
    {
        result += CalcPointLight(g_pointLights[i], norm, input.worldPos, viewDir, input.uv);
    }

    result += CalcSpotLight(spotLight, norm, input.worldPos, viewDir, input.uv);

    return float4(result, 1.0f);
}

float3 CalcDirLight(DirectionalLight light, float3 normal, float3 viewDir, float2 uv)
{
    float3 albedo = g_diffuseMap.Sample(g_sampler, uv).rgb;
    float3 specMap = g_specularMap.Sample(g_sampler, uv).rgb;

    float3 lightDir = normalize(-light.direction.xyz);
    float diff = max(dot(normal, lightDir), 0.0f);

    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 64.0f);

    float3 ambient = light.ambient.rgb * albedo;
    float3 diffuse = light.diffuse.rgb * diff * albedo;
    float3 specular = light.specular.rgb * spec * specMap;

    return ambient + diffuse + specular;
}

float3 CalcPointLight(PointLight light, float3 normal, float3 fragPos, float3 viewDir, float2 uv)
{
    float3 albedo = g_diffuseMap.Sample(g_sampler, uv).rgb;
    float3 specMap = g_specularMap.Sample(g_sampler, uv).rgb;

    float3 lightDir = normalize(light.position.xyz - fragPos);
    float diff = max(dot(normal, lightDir), 0.0f);

    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 64.0f);

    float distanceToLight = length(light.position.xyz - fragPos);
    float attenuation =
        1.0f / (light.attenuation.x +
                light.attenuation.y * distanceToLight +
                light.attenuation.z * distanceToLight * distanceToLight);

    float3 ambient = light.ambient.rgb * albedo;
    float3 diffuse = light.diffuse.rgb * diff * albedo;
    float3 specular = light.specular.rgb * spec * specMap;

    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;

    return ambient + diffuse + specular;
}

float3 CalcSpotLight(SpotLight light, float3 normal, float3 fragPos, float3 viewDir, float2 uv)
{
    float3 albedo = g_diffuseMap.Sample(g_sampler, uv).rgb;
    float3 specMap = g_specularMap.Sample(g_sampler, uv).rgb;

    float3 lightDir = normalize(light.position.xyz - fragPos);
    float diff = max(dot(normal, lightDir), 0.0f);

    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 64.0f);

    float distanceToLight = length(light.position.xyz - fragPos);
    float attenuation =
        1.0f / (light.attenuation.x +
                light.attenuation.y * distanceToLight +
                light.attenuation.z * distanceToLight * distanceToLight);

    float theta = dot(lightDir, normalize(-light.direction.xyz));
    float epsilon = light.cutoffs.x - light.cutoffs.y;
    float intensity = saturate((theta - light.cutoffs.y) / epsilon);

    float3 ambient = light.ambient.rgb * albedo;
    float3 diffuse = light.diffuse.rgb * diff * albedo;
    float3 specular = light.specular.rgb * spec * specMap;

    ambient *= attenuation * intensity;
    diffuse *= attenuation * intensity;
    specular *= attenuation * intensity;

    return ambient + diffuse + specular;
}