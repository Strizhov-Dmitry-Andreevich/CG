#include "LightingUtil.hlsl"

cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    float4x4 gJitteredViewProj;
    float4x4 prevViewProj;
    Light gLights[MaxLights];
    float gScale;
    float gTessellationFactor;
};

cbuffer AtmosphereCB : register(b1)
{
    float3 RayleighScattering;
    float RayleighHeight;

    float3 MieScattering;
    float MieHeight;

    float3 SunDirection;
    float SunIntensity;

    float3 SunColor;
    float AtmosphereDensity;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float3 ViewDir : TEXCOORD0;
};

VSOut VS(uint id : SV_VertexID)
{
    VSOut o;
    float2 pos[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    float2 p = pos[id];
    o.PosH = float4(p, 0.0f, 1.0f);

    // Восстанавливаем направление взгляда
    float4 worldH = mul(float4(p, 1.0f, 1.0f), gInvViewProj);
    o.ViewDir = normalize(worldH.xyz / worldH.w - gEyePosW);

    return o;
}

static const float PI = 3.14159265359f;

float RayleighPhase(float mu)
{
    return 3.0f * (1.0f + mu * mu) / (16.0f * PI);
}


float MiePhase(float mu, float g)
{
    float g2 = g * g;
    float denom = max(1.0f + g2 - 2.0f * g * mu, 0.001f);
    return (1.0f - g2) / (4.0f * PI * pow(denom, 1.5f));
}

float OpticalDepth(float elevation, float heightScale)
{
    elevation = max(elevation, 0.01f);
    float hs = max(heightScale, 0.05f);
    return (exp(-elevation * hs * 4.0f) + 0.08f) * AtmosphereDensity;
}

float4 PS(VSOut input) : SV_Target
{
    float3 viewDir = normalize(input.ViewDir);
    float3 sunDir = normalize(SunDirection);

    float mu = clamp(dot(viewDir, sunDir), -1.0f, 1.0f);
    

    float elevation = viewDir.y;
    float sunElevation = sunDir.y;
    
    float optR = OpticalDepth(elevation, RayleighHeight);
    float optM = OpticalDepth(elevation, max(MieHeight * 2.0f, 0.1f));

    // Фазовая функция
    float phaseR = RayleighPhase(mu);
    float3 rayleigh = RayleighScattering * phaseR * optR;
    
    float g = saturate(MieHeight);
    float phaseM = MiePhase(mu, g);
    float3 mie = MieScattering * phaseM * optM;

    float horizonBias = 0.15f;
    float elevAdjusted = elevation + horizonBias;
    float elevT = saturate(elevAdjusted);
    
    float horizonBlend = 1.0f - pow(elevT, 0.35f); // wide, soft horizon band
    float3 zenithColor = float3(0.04f, 0.10f, 0.38f); // deep blue at top
    float3 horizonColor = float3(0.50f, 0.62f, 0.90f); // hazy pale blue at horizon
    float3 skyGradient = lerp(zenithColor, horizonColor, horizonBlend);
    
    float sunsetT = saturate(1.0f - abs(sunElevation) * 3.5f); // 1 at horizon
    float3 sunsetTint = float3(1.05f, 0.40f, 0.05f); // deep orange
    skyGradient = lerp(skyGradient, sunsetTint,
                       sunsetT * horizonBlend * 0.75f);
    
    // ---- Night darkening ----
    float nightT = saturate(-sunElevation * 3.0f);
    float3 nightSky = float3(0.005f, 0.008f, 0.020f);
    skyGradient = lerp(skyGradient, nightSky, nightT);

     // ---- Scatter contribution (sun-coloured, day-only) ----
    float3 scatter = (rayleigh + mie) * SunColor * SunIntensity;
    scatter *= saturate(sunElevation * 4.0f + 0.5f); // fade at sunset/night

    float discThreshold = 0.998f;
    float disc = smoothstep(discThreshold - 0.01f,
                        discThreshold + 0.001f,
                        mu);

    float limb = pow(saturate((mu - discThreshold) / max(1.0f - discThreshold, 1e-5f)), 0.7f);

    float3 solarDisc = SunColor * disc * limb * SunIntensity * 15.0f;

    solarDisc *= saturate(sunElevation * 10.0f + 0.5f);

    // ---- Ground fill (below horizon) ----
    float groundT = saturate(-elevAdjusted * 5.0f);
    float3 groundDay = float3(0.06f, 0.05f, 0.04f);
    float3 groundSet = float3(0.18f, 0.08f, 0.02f);
    float3 ground = lerp(groundDay, groundSet, sunsetT);

    // ---- Combine ----
    float3 final = skyGradient + scatter + solarDisc;
    final = lerp(final, ground, groundT);
    
    float exposure = 1.2f;
    final *= exposure;
    final = final / (1.0f + final);
    
    return float4(max(final, 0.0f), 1.0f);
}

// Тестовый шейдер для отладки
float4 fPS(VSOut input) : SV_Target
{
    float3 viewDir = normalize(input.ViewDir);
    
    // Визуализируем направление взгляда
    return float4(viewDir * 0.5f + 0.5f, 1.0f);
}