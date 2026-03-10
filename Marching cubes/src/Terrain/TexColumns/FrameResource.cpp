#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT tileCount, UINT brushCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

  //  FrameCB = std::make_unique<UploadBuffer<FrameConstants>>(device, 1, true);
    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    AtmosphereCB = std::make_unique<UploadBuffer<AtmosphereConstants>>(device, 1, true);
    MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    TerrainCB = std::make_unique<UploadBuffer<TileConstants>>(device, tileCount, true);
    BrushCB = std::make_unique<UploadBuffer<BrushConstants>>(device, brushCount, true);
    TAACB = std::make_unique<UploadBuffer<TAAConstants>>(device, passCount, true);

}

FrameResource::~FrameResource()
{

}
Vertex::Vertex(DirectX::XMFLOAT3 _pos, DirectX::XMFLOAT3 _nm, DirectX::XMFLOAT2 _uv, DirectX::XMFLOAT3 _tan)
{
    Pos = _pos;
    Normal = _nm;
    TexC = _uv;
    Tangent = _tan;
}