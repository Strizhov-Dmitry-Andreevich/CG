// Simple outline pixel shader.
// Must define a pixel entrypoint named `main` (used by the app) and
// output all G-Buffer render targets (SV_Target0..SV_Target4) because
// the PSO was created with multiple render targets.

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL;
    float2 TexC : TEXCOORD;
};

struct PSOutput
{
    float4 RTV0 : SV_Target;
    float4 RTV1 : SV_Target;
    float4 RTV2 : SV_Target;
    float4 RTV3 : SV_Target;
    float4 RTV4 : SV_Target;
};

cbuffer PassConstants : register(b0)
{
    // unused
};

PSOutput main(PSInput input)
{
    PSOutput o;
    // Red outline into Albedo (RTV0). Clear/zero other targets.
    o.RTV0 = float4(1.0f, 0.0f, 0.0f, 1.0f);
    o.RTV1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    o.RTV2 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    o.RTV3 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    o.RTV4 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return o;
}
