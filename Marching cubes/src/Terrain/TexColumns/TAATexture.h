#pragma once
#include <wrl.h>
#include <d3d12.h>
#include "d3dUtil.h" 
using namespace Microsoft::WRL;

class TAATexture
{
public:
    TAATexture(ID3D12Device* device,
        DXGI_FORMAT format,
        UINT width,
        UINT height,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    ~TAATexture() = default;

    // Запрещаем копирование
    TAATexture(const TAATexture&) = delete;
    TAATexture& operator=(const TAATexture&) = delete;

    // Разрешаем перемещение
    TAATexture(TAATexture&&) = default;
    TAATexture& operator=(TAATexture&&) = default;

    void Resize(UINT newWidth, UINT newHeight);

    // Геттеры
    ID3D12Resource* GetResource() const { return mResource.Get(); }
    DXGI_FORMAT GetFormat() const { return mFormat; }
    UINT GetWidth() const { return mWidth; }
    UINT GetHeight() const { return mHeight; }

    // Дескрипторы
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const { return mRTVHandle; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return mSRVHandle; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return mUAVHandle; }

    void SetRTVHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) { mRTVHandle = handle; }
    void SetSRVHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) { mSRVHandle = handle; }
    void SetUAVHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) { mUAVHandle = handle; }

private:
    void CreateResource();

private:
    ComPtr<ID3D12Device> mDevice;
    ComPtr<ID3D12Resource> mResource;

    DXGI_FORMAT mFormat;
    UINT mWidth;
    UINT mHeight;
    D3D12_RESOURCE_FLAGS mFlags;

    D3D12_CPU_DESCRIPTOR_HANDLE mRTVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE mSRVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE mUAVHandle;
};