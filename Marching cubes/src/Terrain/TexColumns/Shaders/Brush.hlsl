// Brush.hlsl - compute shader

// Константные буферы для compute shader
cbuffer cbTerrainTile : register(b1)
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

cbuffer cbBrush : register(b0)
{
    float4 BrushColors;
    float3 BrushWPos;
    int isBrushMode;
    int isPainting;
    float BrushRadius;
    float BrushFalofRadius;
}

// SRV для карты высот
Texture2D gTerrDispMap : register(t0);

// UAV для текстуры кисти
RWTexture2D<float4> gBrushTexture : register(u0);

[numthreads(16, 16, 1)]
void BrushCS(uint3 threadID : SV_DispatchThreadID)
{
    const uint textureWidth = 1024;
    const uint textureHeight = 1024;
    
    if (threadID.x >= textureWidth || threadID.y >= textureHeight)
        return;
    
    if (isPainting == 0)
        return;
    
    float2 pixelUV = float2(threadID.xy) / float2(textureWidth, textureHeight);
    float2 brushRelativePos = BrushWPos.xz - gTilePosition.xz;
    float2 brushTerrainUV = float2(
        brushRelativePos.x / gMapSize,
        brushRelativePos.y / gMapSize
    );
    
    float distanceToBrush = distance(pixelUV, brushTerrainUV);
    float brushRadiusUV = BrushRadius / gMapSize;
    float brushFalloffUV = BrushFalofRadius / gMapSize;
    
    float intensity = 0.0f;
    
    if (distanceToBrush <= brushRadiusUV)
    {
        intensity = 1.0f;
    }
    else if (distanceToBrush <= brushRadiusUV + brushFalloffUV)
    {
        float normalizedDist = (distanceToBrush - brushRadiusUV) / brushFalloffUV;
        intensity = 1.0f - smoothstep(0.0f, 1.0f, normalizedDist);
    }
    
    if (intensity > 0.0f)
    {
        float4 currentColor = gBrushTexture[threadID.xy];
        float4 brushColor = float4(BrushColors.rgb, intensity * BrushColors.a);
    
    // Новый цвет накладывается поверх старого
        float4 newColor;
    
    // Цвет: линейная интерполяция в зависимости от alpha новой кисти
        newColor.rgb = lerp(currentColor.rgb, brushColor.rgb, brushColor.a);
    
    // Alpha: увеличиваем, но не больше 1
        newColor.a = min(currentColor.a + brushColor.a * 0.5f, 1.0f);
    
        gBrushTexture[threadID.xy] = saturate(newColor);
    }
}