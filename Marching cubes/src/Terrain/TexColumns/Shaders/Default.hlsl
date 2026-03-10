//***************************************************************************************
// Default.hlsl - ИСПРАВЛЕННАЯ ВЕРСИЯ
//***************************************************************************************


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
Texture2D gDispMap : register(t2);
//Texture2D gTerrDiffMap : register(t3);
//Texture2D gTerrNormMap : register(t4);
//Texture2D gTerrDispMap : register(t5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gPrevWorld;
    float4x4 gTexTransform;
};

// Constant data that varies per pass.
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
    
    //int isBrushMode;
    float gScale;
    float gTessellationFactor;
    //float BrushRadius;

    //float BrushWPos;
    //float BrushFalofRadius;

    //float4 BrushColors;

};

// Constant data that varies per material.
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
    //float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    float3 Tan : TANGENT;
};

struct PixelOut
{
    float4 Color : SV_Target0; // Основной цвет (в color buffer)
    float4 Velocity : SV_Target1; // Velocity (в velocity buffer)
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    // Трансформируем позицию, нормаль, касательную в мировые координаты
    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld).xyz;
    // Для нормали/касательной используем gWorld (предполагая uniform scale).
    // Если есть non-uniform scale, нужна инверсно-транспонированная матрица мира (часто (float3x3)gInvWorld).
    // Но для простоты пока используем gWorld.
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorld));
    vout.Tan = normalize(mul(vin.Tan, (float3x3) gWorld));

    // Трансформируем текстурные координаты (с учетом трансформаций объекта и материала)
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;

    return vout;
}


struct HS_CONSTANT_DATA_OUTPUT
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};
//Called once per patch. The patch and an index to the patch (patch ID) are passed in
HS_CONSTANT_DATA_OUTPUT ConstantsHS(InputPatch<VertexOut, 3> p, uint PatchID : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT Out;
    
    // Вычисляем центр патча в мировых координатах
    float3 patchCenter = (p[0].PosW + p[1].PosW + p[2].PosW) / 3.0f;
    
    // Вычисляем расстояние от камеры до центра патча
    float distanceToCamera = distance(patchCenter, gEyePosW);
    
    // Базовый фактор тесселяции из константного буфера
    float baseTessFactor = gTessellationFactor;
    
    // Динамический множитель на основе расстояния
    // Чем дальше от камеры, тем меньше тесселяция
    float distanceMultiplier = 1.0f;
    
    /*if (distanceToCamera < 10.0f)
        distanceMultiplier = 4.0f;
    else if (distanceToCamera < 20.0f)
        distanceMultiplier = 2.f;
    else if (distanceToCamera < 30.0f)
        distanceMultiplier = 1.f;
    else
        distanceMultiplier = 1.f;*/
    
    // Итоговый фактор тесселяции
    float finalTessFactor = baseTessFactor * distanceMultiplier;
    
    // Ограничиваем значения (максимум 64, минимум 1)
    finalTessFactor = clamp(finalTessFactor, 1.0f, 64.0f);
    
    // Назначаем факторы тесселяции
    Out.Edges[0] = finalTessFactor;
    Out.Edges[1] = finalTessFactor;
    Out.Edges[2] = finalTessFactor;
    Out.Inside = finalTessFactor;
    
    return Out;
}

struct HS_CONTROL_POINT_OUTPUT
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD0;
    float3 TanW : TANGENT;
};

[domain("tri")] // indicates a triangle patch (3 verts)
[partitioning("fractional_odd")] // available options: fractional_even, fractional_odd, integer, pow2
[outputtopology("triangle_cw")] // vertex ordering for the output triangles
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantsHS")] // name of the patch constant hull shader
[maxtessfactor(64.0)] //hint to the driver – the lower the better
// Pass in the input patch and an index for the control point
HS_CONTROL_POINT_OUTPUT HS(InputPatch<VertexOut, 3> patch, uint i :
SV_OutputControlPointID)
{
    HS_CONTROL_POINT_OUTPUT hout;

    // Просто передаем данные контрольной точки
    hout.PosW = patch[i].PosW;
    hout.NormalW = patch[i].NormalW;
    hout.TexC = patch[i].TexC;
    hout.TanW = patch[i].Tan;

    return hout;
}



struct DS_OUTPUT
{
    float4 PosH : SV_POSITION; // Позиция в Clip Space (!!!)
    float3 PosW : POSITION; // Позиция в мире (для освещения)
    float3 NormalW : NORMAL; // Нормаль в мире (после смещения!)
    float2 TexC : TEXCOORD0; // Текстурные координаты
    float2 decalUV : TEXCOORD1; // Текстурные координаты
    float3 TanW : TANGENT; // Касательная в мире (для normal mapping)
    float4 CurPosH : POSITION2; // Текущая позиция БЕЗ джиттера (для velocity)
    float4 PrevPosH : POSITION3; // Предыдущая позиция (для velocity)
};

[domain("tri")]
DS_OUTPUT DS(HS_CONSTANT_DATA_OUTPUT input,
             float3 domainLoc : SV_DomainLocation,
             const OutputPatch<HS_CONTROL_POINT_OUTPUT, 3> patch)
{
    DS_OUTPUT dout;

    // 1. Интерполяция атрибутов контрольных точек
    dout.PosW = domainLoc.x * patch[0].PosW + domainLoc.y * patch[1].PosW + domainLoc.z * patch[2].PosW;
    dout.NormalW = domainLoc.x * patch[0].NormalW + domainLoc.y * patch[1].NormalW + domainLoc.z * patch[2].NormalW;
    dout.TexC = domainLoc.x * patch[0].TexC + domainLoc.y * patch[1].TexC + domainLoc.z * patch[2].TexC;
    dout.TanW = domainLoc.x * patch[0].TanW + domainLoc.y * patch[1].TanW + domainLoc.z * patch[2].TanW;
    
    float3 unitNormalW = normalize(dout.NormalW);
    dout.TanW = normalize(dout.TanW);
    
    float displacement = gDispMap.SampleLevel(gsamAnisotropicWrap, dout.TexC, 0).r;
    displacement *= gScale;
    dout.PosW += unitNormalW * displacement;
    
    // 4. Пересчет нормали/касательной (НЕ ТРЕБУЕТСЯ, так как смещения нет)
    // Нормализуем интерполированные векторы (важно для нормалей и касательных)
    dout.NormalW = normalize(dout.NormalW);
    dout.TanW = normalize(dout.TanW);
  
    // 5. Трансформация ИНТЕРПОЛИРОВАННОЙ мировой позиции в Clip Space
    
     // Текущая позиция с джиттером (для рендера)
    dout.PosH = mul(float4(dout.PosW, 1.0f), gJitteredViewProj);
    
    // Текущая позиция без джиттера (для velocity)
    dout.CurPosH = mul(float4(dout.PosW, 1.0f), gViewProj);
    //dout.CurPosH /= dout.CurPosH.w;
    // Предыдущая позиция (для velocity)
    // Для предыдущей позиции нужна предыдущая мировая позиция
    // Это сложно с тесселяцией, используем приближение
    float4 prevPosW = float4(dout.PosW - (gEyePosW - gEyePosW), 1.0f); // Упрощенно
    dout.PrevPosH = mul(prevPosW, prevViewProj);
    //dout.PrevPosH /= dout.PrevPosH.w;
    // Возвращаем структуру для Пиксельного Шейдера
    return dout;
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
float2 CalcVelocity(float4 newPos, float4 oldPos)
{
    
    return (newPos - oldPos).xy * float2(0.5f, -0.5f);
}
float3 ApplyExposure(float3 color, float exposure)
{
    return color * exposure;
}

float3 ToneMapReinhard(float3 color)
{
    return color / (1.0f + color);
}

PixelOut PS(DS_OUTPUT pin) : SV_Target
{
    PixelOut pout;

    float exposure = 1.2f;

    float4 diffuseAlbedo =
        gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) * gDiffuseAlbedo;

    float3 normalSample =
        gNormalMap.Sample(gsamAnisotropicWrap, pin.TexC).rgb;

    float3 bumpedNormalW =
        NormalSampleToWorldSpace(normalSample, pin.NormalW, pin.TanW);

    bumpedNormalW = normalize(bumpedNormalW);

    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };

    float3 directLight =
        ComputeLighting(gLights, mat, pin.PosW, bumpedNormalW, toEyeW, 1.0f);

    // === HDR color ===
    float3 hdrColor = ambient.rgb + directLight;

    // === Tone mapping pipeline ===
    hdrColor = ApplyExposure(hdrColor, exposure);
    hdrColor = ToneMapReinhard(hdrColor);

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
