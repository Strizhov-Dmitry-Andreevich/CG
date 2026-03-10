#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 PrevWorld = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    //float g_TessellationFactor;
    //float g_Scale;
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

    DirectX::XMFLOAT4X4 JitteredViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 PrevViewProj = MathHelper::Identity4x4();
    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light Lights[MaxLights];




    float gScale = 1.f;
    float gTessellationFactor = 1.f;

};

struct BrushConstants
{
    DirectX::XMFLOAT4 BrushColors = { 0.f, 0.f, 0.f, 1.f };

    DirectX::XMFLOAT3 BrushWPos = { 0.f, 0.f, 0.f };
    int isBrushMode = 0;
    int isPainting = 0;
    float BrushRadius = 30.f;
    float BrushFalofRadius = 40.f;


};


struct Vertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
    DirectX::XMFLOAT3 Tangent;
    Vertex(DirectX::XMFLOAT3 _pos, DirectX::XMFLOAT3 _nm, DirectX::XMFLOAT2 _uv, DirectX::XMFLOAT3 _tan);
    Vertex() {};
};

struct TileConstants
{
    // 16 bytes
    DirectX::XMFLOAT3 TilePosition;
    float TileSize;

    // 16 bytes  
    DirectX::XMFLOAT3 gTerrainOffset;
    float mapSize;

    // 16 bytes
    float hScale;
    int showBoundingBox;
    float Padding1;
    float Padding2;
};

struct TAAConstants
{
    float blendFactor = 0.01f;
    DirectX::XMFLOAT3 pad;

};

struct AtmosphereConstants
{             


    DirectX::XMFLOAT3 RayleighScattering = DirectX::XMFLOAT3(0.17f, 0.37f, 0.92f); 
    float RayleighHeight = 1.;

    DirectX::XMFLOAT3 MieScattering = DirectX::XMFLOAT3(0.50f, 0.50f, 0.50f);
    float MieHeight = 0.76f;

    DirectX::XMFLOAT3 SunDirection = DirectX::XMFLOAT3(0.0f, 0.643f, 0.766f);
    float SunIntensity = 8.0f;

    DirectX::XMFLOAT3 SunColor = DirectX::XMFLOAT3(1.0f, 0.9f, 0.7f);
    float AtmosphereDensity = 1.0f;
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource
{
public:
    
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT tileCount, UINT brushCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
   // std::unique_ptr<UploadBuffer<FrameConstants>> FrameCB = nullptr;
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<AtmosphereConstants>> AtmosphereCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<TileConstants>> TerrainCB = nullptr;
    std::unique_ptr<UploadBuffer<BrushConstants>> BrushCB = nullptr;
    std::unique_ptr<UploadBuffer<TAAConstants>> TAACB = nullptr;


    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 Fence = 0;
};