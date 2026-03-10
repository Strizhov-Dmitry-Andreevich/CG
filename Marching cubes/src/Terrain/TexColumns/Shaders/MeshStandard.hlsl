// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gPrevWorld;
    float4x4 gTexTransform;
};

cbuffer cbPass : register(b1)
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

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 Tan : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TanW : TANGENT;
    float2 TexC : TEXCOORD;
    float4 PrevPosH : POSITION2;
    float4 CurPosH : POSITION3;
};

struct PixelOut
{
    float4 Color : SV_Target0; // Основной цвет (в color buffer)
    float4 Velocity : SV_Target1; // Velocity (в velocity buffer)
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    // Преобразование в мировое пространство
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    float4 prevPosW = mul(float4(vin.PosL, 1.0f), gPrevWorld);
    
    // Преобразование нормали и тангента
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
    vout.TanW = mul(vin.Tan, (float3x3) gWorld);
    
    
    
    // Текстурные координаты
    vout.TexC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform).xy;
    
    // Преобразование в пространство клипа
    vout.PosH = mul(posW, gJitteredViewProj);
    vout.PrevPosH = mul(prevPosW, prevViewProj);
    //vout.PrevPosH /= vout.PrevPosH.w;
    vout.CurPosH = mul(posW, gViewProj);
    //vout.CurPosH /= vout.CurPosH.w;
    //vout.PosH = mul(posW, gViewProj);
    
    return vout;
}


float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
	// Uncompress each component from [0,1] to [-1,1].
    float3 normalT = 2.0f * normalMapSample - 1.0f;

	// Build orthonormal basis.
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

	// Transform from tangent space to world space.
    float3 bumpedNormalW = mul(normalT, TBN);

    return bumpedNormalW;
}
float3 ApplyToneMapping(float3 color)
{
    // Reinhard tone mapping
    return color / (1.0f + color);
}

float3 ApplyExposure(float3 color, float exposure)
{
    return color * exposure;
}

PixelOut PS(VertexOut pin) : SV_Target
{
    PixelOut pout;
    
    float4 diffuseAlbedo =
        gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) * gDiffuseAlbedo;

    float3 normalSample =
        gNormalMap.Sample(gsamAnisotropicWrap, pin.TexC).rgb;

    pin.NormalW = normalize(pin.NormalW);
    float3 bumpedNormalW =
        NormalSampleToWorldSpace(normalSample, pin.NormalW, pin.TanW);

    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };

    float3 shadowFactor = 1.0f;

    float4 directLight =
        ComputeLighting(gLights, mat, pin.PosW,
                        bumpedNormalW, toEyeW, shadowFactor);

    float3 hdrColor = (ambient + directLight).rgb;

    // === ВАЖНО: та же экспозиция что и у неба ===
    float exposure = 1.2f;

    hdrColor = ApplyExposure(hdrColor, exposure);
    hdrColor = ApplyToneMapping(hdrColor);

    pout.Color = float4(hdrColor, diffuseAlbedo.a);

    // === VELOCITY ===
    float2 posNDC = pin.CurPosH.xy / pin.CurPosH.w;
    float2 prevPosNDC = pin.PrevPosH.xy / pin.PrevPosH.w;

    float2 uv = posNDC * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;

    float2 prevUV = prevPosNDC * 0.5f + 0.5f;
    prevUV.y = 1.0f - prevUV.y;

    pout.Velocity = float4(uv - prevUV, 0.0f, 1.0f);

    return pout;
}


// Wireframe шейдер
PixelOut WirePS(VertexOut pin) : SV_Target
{
    PixelOut pout;
    pout.Color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    pout.Velocity = float4(0.0f, 0.0f, 0.0f, 0.0f); // Нулевой velocity для wireframe
    return pout;
}