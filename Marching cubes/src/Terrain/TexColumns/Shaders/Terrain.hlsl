#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

Texture2D gTerrDiffMap : register(t0);
Texture2D gTerrNormMap : register(t1);
Texture2D gTerrDispMap : register(t2);
Texture2D gBrushTexture : register(t3); // t3 - текстура кисти (SRV)

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

cbuffer cbTerrainTile : register(b3) 
{
    // 16 bytes
    float3 gTilePosition;
    float gTileSize;
    
    // 16 bytes  
    float3 gTerrainOffset;
    float gMapSize;
    
    // 16 bytes
    float gHeightScale;
    int showBoundingBox;
    float Padding1;
    float Padding2;
};

cbuffer cbBrush : register(b4)
{
    
    float4 BrushColors;

    float3 BrushWPos;
    int isBrushMode;
    int isPainting;
    float BrushRadius;
    float BrushFalofRadius;

}

// UAV текстура для рисования
//RWTexture2D<float4> gBrushTexture : register(u0);

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
    float3 TangentW : TANGENT;
    float2 TexC : TEXCOORD;
    float2 TexCl : TEXCOORD2;
    float height : HEIGHT;
    float4 PrevPosH : POSITION2; // Для velocity (предыдущий кадр)
    float4 CurPosH : POSITION3;
};

struct PixelOut
{
    float4 Color : SV_Target0; // Основной цвет (в color buffer)
    float4 Velocity : SV_Target1; // Velocity (в velocity buffer)
};


VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
  
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;
    float coeff = gTileSize / gMapSize;
    vout.TexC *= coeff;
    vout.TexC += gTilePosition.xz / gMapSize;
    vout.TexCl = vin.TexC;

    float height = gTerrDispMap.SampleLevel(gsamLinearClamp, vout.TexC, 0).r;
    vout.height = height;
    
    float3 posL = vin.PosL+gTerrainOffset;
    posL.y = posL.y + height * gHeightScale;

    float4 posW = mul(float4(posL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    
    // ИСПРАВЛЕННОЕ вычисление нормали
    float2 texelSize = float2(1.0f / gMapSize, 1.0f / gMapSize);
    
    // Семплируем высоты соседних точек
    float hL = gTerrDispMap.SampleLevel(gsamLinearClamp, vout.TexC + float2(-texelSize.x, 0.0f), 0).r;
    float hR = gTerrDispMap.SampleLevel(gsamLinearClamp, vout.TexC + float2(texelSize.x, 0.0f), 0).r;
    float hD = gTerrDispMap.SampleLevel(gsamLinearClamp, vout.TexC + float2(0.0f, -texelSize.y), 0).r;
    float hU = gTerrDispMap.SampleLevel(gsamLinearClamp, vout.TexC + float2(0.0f, texelSize.y), 0).r;
    
    // Вычисляем градиенты
    float dX = (hR - hL) * gHeightScale; // градиент по X
    float dZ = (hU - hD) * gHeightScale; // градиент по Z
    
    // Создаем нормаль напрямую из градиентов
    // Формула: normal = normalize((-dX, 1, -dZ))
    float3 normal = normalize(float3(-dX, 1.0f, -dZ));
    
    // Создаем тангент
    float3 tangent = normalize(float3(1.0f, dX, 0.0f));
    
    // Трансформируем в мировое пространство
    vout.NormalW = normalize(mul(normal, (float3x3) gWorld));
    vout.TangentW = normalize(mul(tangent, (float3x3) gWorld));
    
    // Трансформируем в clip space
    //vout.PosH = mul(posW, gViewProj);
    vout.PosH = mul(posW, gJitteredViewProj);
    
    // Текущая позиция без джиттера (для velocity)
    vout.CurPosH = mul(posW, gViewProj);
    //vout.CurPosH /= vout.CurPosH.w;
    // Предыдущая позиция (для velocity)
    float4 prevPosW = mul(float4(posL, 1.0f), gPrevWorld);
    vout.PrevPosH = mul(prevPosW, prevViewProj);
    //vout.PrevPosH /= vout.PrevPosH.w;
    return vout;
}
float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
    // Распаковываем нормаль из [0,1] в [-1,1]
    float3 normalT = 2.0f * normalMapSample - 1.0f;
    
    // Строим TBN матрицу
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);
    
    float3x3 TBN = float3x3(T, B, N);
    
    // Трансформируем нормаль в мировое пространство
    float3 bumpedNormalW = mul(normalT, TBN);
    
    return bumpedNormalW;
}

float4 GetBrushColor(VertexOut pin)
{
    // Инициализируем ПРОЗРАЧНЫМ цветом!
    float4 Color = float4(0., 0., 0., 0.); // <-- ИСПРАВЛЕНО!
    float distanceToBrush = distance(pin.PosW.xz, BrushWPos.xz);
    float radius = BrushRadius + BrushFalofRadius;

    if (distanceToBrush <= radius)
    {
        // Вычисляем интенсивность кисти с плавным затуханием
        float brushIntensity = 0.0f;
            
        if (distanceToBrush <= BrushRadius)
        {
            // Внутренний радиус - полная интенсивность
            brushIntensity = 1.0f; // <-- УБРАЛ умножение на BrushColors!
        }
        else
        {
            // Область затухания (falloff)
            float falloffDistance = distanceToBrush - BrushRadius;
            brushIntensity = 1.0f - smoothstep(0.0f, BrushFalofRadius, falloffDistance);
        }
            
        // Создаем цвет кисти
        float4 brushColor = float4(BrushColors.rgb, brushIntensity * BrushColors.a);
            
        // Поскольку Color = 0, это просто присваивание brushColor
        Color = brushColor; // Или Color = lerp(float4(0,0,0,0), brushColor, brushColor.a);
    }
    
    return Color;
}

float4 ShowBoundainBoxes(VertexOut pin) : SV_Target
{
    if (showBoundingBox == 1)
    {
        // Вычисляем границы тайла как в C++ коде
        float3 bboxMin = float3(gTilePosition.x, 0.0f, gTilePosition.z);
        float3 bboxMax = float3(gTilePosition.x + gTileSize, gHeightScale, gTilePosition.z + gTileSize);
    
        // Толщина границы (можно настроить)
        float borderThickness = 0.02f * gTileSize;
    
        // Проверяем, находится ли пиксель близко к любой границе
        bool nearLeft = (pin.PosW.x - bboxMin.x) < borderThickness;
        bool nearRight = (bboxMax.x - pin.PosW.x) < borderThickness;
        bool nearBottom = (pin.PosW.z - bboxMin.z) < borderThickness;
        bool nearTop = (bboxMax.z - pin.PosW.z) < borderThickness;
    
        // Если пиксель близко к любой из четырех границ - рисуем границу
        if (nearLeft || nearRight || nearBottom || nearTop)
        {
            return float4(0.0f, 1.0f, 0.0f, 1.0f); // Зеленая граница
        }
    }
    
    // Если не рисуем границу или showBoundingBox == 0, возвращаем прозрачный
    // Это позволит основной функции WirePS продолжить выполнение
    return float4(0, 0, 0, 0);
}
float3 ApplyExposure(float3 color, float exposure)
{
    return color * exposure;
}

float3 ToneMapReinhard(float3 color)
{
    return color / (1.0f + color);
}

PixelOut PS(VertexOut pin) : SV_Target
{
    PixelOut pout;

    float exposure = 1.2f;

    // === Bounding boxes ===
    float4 borderColor = ShowBoundainBoxes(pin);
    if (borderColor.a > 0.5)
    {
        pout.Color = borderColor;
        pout.Velocity = float4(0, 0, 0, 1);
        return pout;
    }

    // === Base color ===
    float4 diffuseAlbedo =
        gTerrDiffMap.Sample(gsamAnisotropicWrap, pin.TexC);

    float4 drawAlbedo =
        gBrushTexture.Sample(gsamAnisotropicWrap, pin.TexC);

    diffuseAlbedo *= gDiffuseAlbedo;

    if (drawAlbedo.a > 0.001f)
        diffuseAlbedo.rgb =
            lerp(diffuseAlbedo.rgb, drawAlbedo.rgb, drawAlbedo.a);

    float3 normalMapSample =
        gTerrNormMap.Sample(gsamAnisotropicWrap, pin.TexC).rgb;

    pin.NormalW = normalize(pin.NormalW);
    pin.TangentW = normalize(pin.TangentW);

    float3 bumpedNormalW =
        NormalSampleToWorldSpace(normalMapSample, pin.NormalW, pin.TangentW);

    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };

    float3 directLight =
        ComputeLighting(gLights, mat, pin.PosW, bumpedNormalW, toEyeW, 1.0f);

    // === HDR color ===
    float3 hdrColor = ambient.rgb + directLight;

    // === Brush overlay (в HDR) ===
    if (isBrushMode == 1)
    {
        float4 brushColor = GetBrushColor(pin);
        hdrColor = lerp(hdrColor, brushColor.rgb, brushColor.a);

        float outlineWidth = 2.0f;
        float distanceToBrush = distance(pin.PosW.xz, BrushWPos.xz);
        float radius = BrushRadius + BrushFalofRadius;

        if (abs(distanceToBrush - radius) < outlineWidth)
        {
            float outlineIntensity =
                1.0f - abs(distanceToBrush - radius) / outlineWidth;

            hdrColor = lerp(hdrColor,
                            float3(.4f, .7f, 1.f),
                            outlineIntensity * 0.8f);
        }
    }

    // === Tone mapping ===
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

PixelOut WirePS(VertexOut pin) : SV_Target
{
    PixelOut pout;
    pout.Velocity = float4(0.0f, 0.0f, 0.0f, 0.0f);
    // Сначала проверяем границы
    float4 borderColor = ShowBoundainBoxes(pin);
    if (borderColor.a > 0.5)
    {
        pout.Color = borderColor;
        return pout;
    }
    
    // Основной цвет тайла
        float minSize = 4;
        float normalizedSize = saturate((gTileSize - minSize) / (gMapSize - minSize));

    // Нелинейное преобразование для большей контрастности
        normalizedSize = smoothstep(0.0, 1.0, normalizedSize);

    // Очень насыщенные цвета
        float4 smallColor = float4(0.1f, 0.1f, 0.8f, 1.0f); // Темно-синий
        float4 largeColor = float4(0.9f, 0.1f, 0.1f, 1.0f); // Темно-красный
        pout.Color = lerp(smallColor, largeColor, normalizedSize);
    
    return pout;

}