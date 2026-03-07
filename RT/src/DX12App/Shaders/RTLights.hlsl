#include "LightingUtil.hlsl"

Texture2D gZW : register(t0);
Texture2D gNormal : register(t1);
RaytracingAccelerationStructure gTLAS : register(t2);
// Root parameter 9 binds a descriptor table with these register spaces:
// - space1: Texture2D bindless (t0..)
// - space2: StructuredBuffer<VertexRT> bindless (t0..)
// - space3: ByteAddressBuffer bindless (t0..)
// - space4: StructuredBuffer<uint4> instance data (t0)
Texture2D gBindlessTex[512] : register(t0, space1);
// Must match MAX in root signature.
struct VertexRT
{
    float3 Tangent;
    float3 Pos;
    float3 Normal;
    float2 TexC;
};

StructuredBuffer<VertexRT> gBindlessVB[256] : register(t0, space2);
ByteAddressBuffer gBindlessIB[256] : register(t0, space3);
StructuredBuffer<uint4> gRTInstanceData : register(t0, space4);

SamplerState gsamAnisotropicWrap : register(s4);

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
};

cbuffer LightConstants : register(b1)
{
    Light light;
    float3 LColor;
    int LightType; //0 - directional; 1 - point; 2 - spot
    float4x4 LWorld;
    float4x4 LViewProj[6];
    float4x4 LShadowTransform[6];
};

// functions

float3 RestoreWorldPosition(float2 UV, float depth)
{
    //magic DirectX texcoord mutations
    float4 clipPos;
    clipPos.x = UV.x * 2.0f - 1.0f;
    clipPos.y = 1.0f - UV.y * 2.0f;
    clipPos.z = depth;
    clipPos.w = 1.0f;

    //transform into world space
    float4 viewPos = mul(clipPos, gInvViewProj);
    viewPos.xyz /= viewPos.w;

    return viewPos.xyz;
}

// Helpers for index fetch (HLSL does NOT allow nested function definitions).
uint LoadIndex16(ByteAddressBuffer ib, uint index)
{
    uint byteOffset = index * 2;
    uint aligned = byteOffset & ~3u;
    uint packed = ib.Load(aligned);
    return ((byteOffset & 2u) == 0u) ? (packed & 0xFFFFu) : (packed >> 16);
}

bool TraceRay(float3 origin, float3 direction, float maxDistance)
{
    // Alpha-test aware shadow ray:
    // - Force triangles to be treated as non-opaque candidates
    // - For each candidate, sample its diffuse alpha; if alpha >= cutoff -> commit, else ignore and continue.

    // Layout of instance data per instanceID (2x uint4):
    // 0: { GeoIndex, GeoIndex, DiffuseTexIndex, IndexFormat }
    // 1: { AlphaCutoff (asuint), TexScale (asuint), _pad0, _pad1 }

    // No back-face culling: alpha-tested cards (foliage/grass) must cast shadows from both sides.
    RayQuery < RAY_FLAG_FORCE_NON_OPAQUE > rayQuery;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = normalize(direction);
    ray.TMin = 0.001f;
    ray.TMax = maxDistance;

    rayQuery.TraceRayInline(
        gTLAS,
        RAY_FLAG_FORCE_NON_OPAQUE,
        0xff,
        ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            uint instId = rayQuery.CandidateInstanceID();
            uint4 d0 = gRTInstanceData[instId * 2 + 0];
            uint4 d1 = gRTInstanceData[instId * 2 + 1];

            uint vbIndex = d0.x;
            uint ibIndex = d0.y;
            uint texIndex = d0.z;
            uint indexFormat = d0.w;

            float alphaCutoff = asfloat(d1.x);
            float texScale = asfloat(d1.y);

            StructuredBuffer<VertexRT> vb = gBindlessVB[vbIndex];
            ByteAddressBuffer ib = gBindlessIB[ibIndex];
            Texture2D tex = gBindlessTex[texIndex];

            uint prim = rayQuery.CandidatePrimitiveIndex();
            uint i0, i1, i2;

            // Opaque shortcut (avoid alpha sampling for most geometry).
            if (alphaCutoff < 0.0f)
            {
                rayQuery.CommitNonOpaqueTriangleHit();
                continue;
            }

            // IndexFormat: 0 = R16_UINT, 1 = R32_UINT (raw buffer, 4-byte addressed).
            if (indexFormat == 0u)
            {
                i0 = LoadIndex16(ib, prim * 3 + 0);
                i1 = LoadIndex16(ib, prim * 3 + 1);
                i2 = LoadIndex16(ib, prim * 3 + 2);
            }
            else
            {
                i0 = ib.Load((prim * 3 + 0) * 4);
                i1 = ib.Load((prim * 3 + 1) * 4);
                i2 = ib.Load((prim * 3 + 2) * 4);
            }

            float2 uv0 = vb[i0].TexC;
            float2 uv1 = vb[i1].TexC;
            float2 uv2 = vb[i2].TexC;

            float2 bc = rayQuery.CandidateTriangleBarycentrics();
            float b0 = 1.0f - bc.x - bc.y;
            float b1 = bc.x;
            float b2 = bc.y;

            float2 uv = (uv0 * b0 + uv1 * b1 + uv2 * b2) * texScale;

            float alpha = tex.SampleLevel(gsamAnisotropicWrap, uv, 0).a;
            if (alpha >= alphaCutoff)
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    return rayQuery.CommittedStatus() != COMMITTED_NOTHING;
}

// structures

struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float4 PosH : SV_Position;
};

struct psout
{
    float depth : SV_Depth;
};

// shaders

VertexOut VS(uint vertexID : SV_VertexID)
{
    //full-screen quad
    float2 verts[3] =
    {
        float2(-1, -1),
        float2(-1, 3),
        float2(3, -1)
    };
    
    VertexOut vo;
    vo.PosH = float4(verts[vertexID], 0, 1);
    return vo;
}

VertexOut LightsGeometryVS(VertexIn vi)
{
    VertexOut vo;
    
    vo.PosH = mul(mul(float4(vi.PosL, 1.f), LWorld), gViewProj);
    
    return vo;
}

psout PS(VertexOut pin)
{
    psout res;
    
    float2 UV = pin.PosH.xy / gRenderTargetSize;
    float screenDepth = gZW.Load(int3(pin.PosH.xy, 0)).w;
    float3 WorldPos = RestoreWorldPosition(UV, screenDepth);
    float3 Normal = gNormal.Load(int3(pin.PosH.xy, 0)).rgb;
    
    if (screenDepth >= 1.0f)
    {
        res.depth = 1.0f;
        return res;
    }
    
    float shadowFactor = 1.0f;
    //Scaling jitter by distance from camera(cant use depth since its 0.9999 most of the time(DAMN YOU FARZ)(or depthbias idk))
    float DistanceToCamera = length(WorldPos - gEyePosW);
    float MaxJitterDistance = 50.0f;
    float MinJitterDistance = 10.0f;

    float distanceFactor = saturate((DistanceToCamera - MinJitterDistance) / (MaxJitterDistance - MinJitterDistance));
    float rayJitter = 0.005f * distanceFactor;

    
    // RayTrace based on light type
    if (LightType == 0) // Directional Light
    {
        float3 RayOrigin = WorldPos + Normal * 0.1f;
        float3 lightDir = normalize(-light.Direction + float3(
            (frac(sin(dot(UV, float2(12.9898, 78.233))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(39.346, 11.135))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(67.89, 45.321))) * 43758.5453) - 0.5f) * rayJitter
        ));
        
        bool hit = TraceRay(RayOrigin, lightDir, 1000.0f);
        shadowFactor = hit ? 0.0f : 1.0f;
    }
    else if (LightType == 1) // Point Light
    {
        float3 toLight = light.Position - WorldPos;
        float distanceToLight = length(toLight);
        float3 lightDir = normalize(toLight / distanceToLight + float3(
            (frac(sin(dot(UV, float2(23.456, 89.012))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(56.789, 34.567))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(90.123, 67.890))) * 43758.5453) - 0.5f) * rayJitter
        ));
        
        float3 rayOrigin = WorldPos + normalize(Normal) * 0.1f;
        
        bool hit = TraceRay(rayOrigin, lightDir, distanceToLight - 0.2f);
        shadowFactor = hit ? 0.0f : 1.0f;
    }
    else if (LightType == 2) // Spot Light
    {
        float3 toLight = light.Position - WorldPos;
        float distanceToLight = length(toLight);
        float3 lightDir = normalize(toLight / distanceToLight + float3(
            (frac(sin(dot(UV, float2(23.456, 89.012))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(56.789, 34.567))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(90.123, 67.890))) * 43758.5453) - 0.5f) * rayJitter
        ));
        
        float3 rayOrigin = WorldPos + Normal * 0.1f;
        
        bool hit = TraceRay(rayOrigin, lightDir, distanceToLight - 0.2f);
        shadowFactor = hit ? 0.0f : 1.0f;
    }
    
    //draw result into Depth stencil
    res.depth = shadowFactor;
    return res;
}
