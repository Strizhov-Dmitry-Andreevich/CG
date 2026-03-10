// MCStandard.hlsl
// Vertex + Pixel shader for the Marching Cubes plateau mesh.
// Identical pipeline interface to MeshStandard.hlsl (same root signature,
// same input layout, same render targets) — only the texture sampling
// is different: uses Triplanar Mapping instead of UV coordinates.


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

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap  : register(t1);

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
    float3   gEyePosW;
    float    cbPerObjectPad1;
    float2   gRenderTargetSize;
    float2   gInvRenderTargetSize;
    float    gNearZ;
    float    gFarZ;
    float    gTotalTime;
    float    gDeltaTime;
    float4   gAmbientLight;
    float4x4 gJitteredViewProj;
    float4x4 prevViewProj;
    Light    gLights[MaxLights];
    float    gScale;
    float    gTessellationFactor;
};

cbuffer cbMaterial : register(b2)
{
    float4   gDiffuseAlbedo;
    float3   gFresnelR0;
    float    gRoughness;
    float4x4 gMatTransform;
};

SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// UV scale: how many times the texture tiles per world unit.
// 1/64 means one full tile every 64 world units.
// Increase for a smaller/more repeated pattern, decrease for larger tiles.
static const float kTriplanarUVScale = 1.0f / 256.0f;

// Blend sharpness: power applied to the normal weights.
// 1.0  = very soft, gradual transition between projections.
// 4.0  = moderate blend (recommended for rocky surfaces).
// 16.0 = very sharp — nearly a single projection per face.
static const float kTriplanarSharpness = 16.0f;

struct VertexIn
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC    : TEXCOORD;
    float3 Tan     : TANGENT;
};

struct VertexOut
{
    float4 PosH     : SV_POSITION;
    float3 PosW     : POSITION;   // world position — used for triplanar UVs
    float3 NormalW  : NORMAL;
    float3 TanW     : TANGENT;
    float2 TexC     : TEXCOORD;
    float4 PrevPosH : POSITION2;
    float4 CurPosH  : POSITION3;
};

struct PixelOut
{
    float4 Color    : SV_Target0;
    float4 Velocity : SV_Target1;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float4 posW     = mul(float4(vin.PosL, 1.0f), gWorld);
    float4 prevPosW = mul(float4(vin.PosL, 1.0f), gPrevWorld);

    vout.PosW    = posW.xyz;
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    vout.TanW    = mul(vin.Tan,     (float3x3)gWorld);
    vout.TexC    = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform).xy;

    vout.PosH    = mul(posW, gJitteredViewProj);
    vout.CurPosH = mul(posW, gViewProj);
    vout.PrevPosH = mul(prevPosW, prevViewProj);

    return vout;
}


// Compute blend weights from the world-space normal.
// abs() makes them symmetric (so +Y and -Y faces look the same).
// Raising to a power sharpens the boundary between projections.
float3 TriplanarWeights(float3 normalW)
{
    float3 w = pow(abs(normalW), kTriplanarSharpness);
    return w / (w.x + w.y + w.z + 1e-6f); // normalize to sum = 1
}

// Triplanar diffuse sample.
float4 TriplanarDiffuse(float3 posW, float3 weights)
{
    float s = kTriplanarUVScale;

    float4 cx = gDiffuseMap.Sample(gsamAnisotropicWrap, posW.yz * s); 
    float4 cy = gDiffuseMap.Sample(gsamAnisotropicWrap, posW.xz * s);
    float4 cz = gDiffuseMap.Sample(gsamAnisotropicWrap, posW.xy * s); 

    return cx * weights.x + cy * weights.y + cz * weights.z;
}

float3 TriplanarNormal(float3 posW, float3 normalW, float3 weights)
{
    float s = kTriplanarUVScale;

    // Sample three tangent-space normals, unpack from [0,1] to [-1,1]
    float3 tnX = gNormalMap.Sample(gsamAnisotropicWrap, posW.yz * s).rgb * 2.0f - 1.0f;
    float3 tnY = gNormalMap.Sample(gsamAnisotropicWrap, posW.xz * s).rgb * 2.0f - 1.0f;
    float3 tnZ = gNormalMap.Sample(gsamAnisotropicWrap, posW.xy * s).rgb * 2.0f - 1.0f;

    // Whiteout blend: reconstruct world-space normals per projection.
    // Each tangent-space normal is rotated to align with the face normal:
    //   X projection: tangent=(0,0,1), bitangent=(0,1,0), normal=(1,0,0)
    //   Y projection: tangent=(1,0,0), bitangent=(0,0,1), normal=(0,1,0)
    //   Z projection: tangent=(1,0,0), bitangent=(0,1,0), normal=(0,0,1)
    float3 nX = float3(tnX.z,  tnX.y, -tnX.x); // X-facing face
    float3 nY = float3(tnY.x,  tnY.z, -tnY.y); // Y-facing face (top/bottom)
    float3 nZ = float3(tnZ.x, -tnZ.z,  tnZ.y); // Z-facing face

    // Blend and add the surface macro-normal (so detail stays relative to face)
    float3 blended = nX * weights.x + nY * weights.y + nZ * weights.z + normalW;

    return normalize(blended);
}


PixelOut PS(VertexOut pin) : SV_Target
{
    PixelOut pout;

    float3 normalW = normalize(pin.NormalW);


    float3 weights = TriplanarWeights(normalW);

    float4 diffuseAlbedo = TriplanarDiffuse(pin.PosW, weights) * gDiffuseAlbedo;

    float3 bumpedNormalW = TriplanarNormal(pin.PosW, normalW, weights);

    // ── Lighting ───────────────────────────────────────────────────────────────
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    float4 ambient     = gAmbientLight * diffuseAlbedo;
    float  shininess   = 1.0f - gRoughness;
    Material mat       = { diffuseAlbedo, gFresnelR0, shininess };

    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
                                         bumpedNormalW, toEyeW, 1.0f);

    // ── Tone mapping 
    float3 hdrColor = (ambient + directLight).rgb;
    float  exposure = 1.2f;
    hdrColor        = hdrColor * exposure;
    hdrColor        = hdrColor / (1.0f + hdrColor);
    pout.Color      = float4(hdrColor, diffuseAlbedo.a);

    // ── Velocity (for TAA) ─────────────────────────────────────────────────────
    float2 posNDC  = pin.CurPosH.xy  / pin.CurPosH.w;
    float2 prevNDC = pin.PrevPosH.xy / pin.PrevPosH.w;

    float2 uv     = posNDC  * 0.5f + 0.5f; uv.y     = 1.0f - uv.y;
    float2 prevUV = prevNDC * 0.5f + 0.5f; prevUV.y = 1.0f - prevUV.y;

    pout.Velocity = float4(uv - prevUV, 0.0f, 1.0f);

    return pout;
}

// Wireframe variant — one-liner, same as MeshStandard
PixelOut WirePS(VertexOut pin) : SV_Target
{
    PixelOut pout;
    pout.Color    = float4(0.9f, 0.6f, 0.1f, 1.0f); // orange wireframe
    pout.Velocity = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return pout;
}
