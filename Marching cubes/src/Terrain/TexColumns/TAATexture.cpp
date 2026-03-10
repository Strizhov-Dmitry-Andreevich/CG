#include "TAATexture.h"
using Microsoft::WRL::ComPtr;

TAATexture::TAATexture(ID3D12Device* device,
    DXGI_FORMAT format,
    UINT width,
    UINT height,
    D3D12_RESOURCE_FLAGS flags)
    : mDevice(device)
    , mFormat(format)
    , mWidth(width)
    , mHeight(height)
    , mFlags(flags)
{
    CreateResource();
}

void TAATexture::CreateResource()
{

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
        mFormat,
        mWidth, mHeight,
        1, 1,                       // arraySize, mipLevels
        1, 0,                        // sample count, quality
        mFlags
    );

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = mFormat;

    // Нужно ли использовать clear value?
    bool useClearValue = (mFlags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
        (mFlags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    if (useClearValue)
    {
        // Для цветовых текстур
        if (mFormat == DXGI_FORMAT_R8G8B8A8_UNORM ||
            mFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            mFormat == DXGI_FORMAT_B8G8R8A8_UNORM ||
            mFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            mFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 0.0f;
        }
        // Для velocity (R16G16_FLOAT)
        else if (mFormat == DXGI_FORMAT_R16G16_FLOAT)
        {
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
        }
        // Для глубины
        else if (mFormat == DXGI_FORMAT_D24_UNORM_S8_UINT ||
            mFormat == DXGI_FORMAT_D32_FLOAT ||
            mFormat == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
        {
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
        }
    }

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        useClearValue ? &clearValue : nullptr,
        IID_PPV_ARGS(mResource.GetAddressOf())
    ));
}

void TAATexture::Resize(UINT newWidth, UINT newHeight)
{
    if (newWidth != mWidth || newHeight != mHeight)
    {
        mWidth = newWidth;
        mHeight = newHeight;
        mResource.Reset();
        CreateResource();
    }
}