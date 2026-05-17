#include "pch.h"
#include "GCrossAdapterResource.h"

#include "d3dUtil.h"
#include "GDevice.h"
#include "GResource.h"

bool GCrossAdapterResource::IsInit() const
{
    return isInit;
}

GCrossAdapterResource::GCrossAdapterResource(D3D12_RESOURCE_DESC& desc, const std::shared_ptr<GDevice>& primeDevice,
                                             const std::shared_ptr<GDevice>& sharedDevice, const std::wstring& name, D3D12_RESOURCE_FLAGS primeTextureExtraFlags)
{
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT64 sizeInBytes;
    UINT64 totalBytes;
    primeDevice->GetDXDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, &sizeInBytes, &totalBytes);
    UINT64 heapSize = Align(layout.Footprint.RowPitch * layout.Footprint.Height);
    heapSize = std::max(heapSize, totalBytes);

    // Create a heap that will be shared by both adapters.
    CD3DX12_HEAP_DESC heapDesc(
        heapSize,
        D3D12_HEAP_TYPE_DEFAULT,
        0,
        D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER);

    ThrowIfFailed(primeDevice->GetDXDevice()->CreateHeap(&heapDesc, IID_PPV_ARGS(&crossAdapterResourceHeap[0])));

    HANDLE heapHandle = nullptr;
    ThrowIfFailed(primeDevice->GetDXDevice()->CreateSharedHandle(
        crossAdapterResourceHeap[0].Get(),
        nullptr,
        GENERIC_ALL,
        nullptr,
        &heapHandle));

    HRESULT openSharedHandleResult = sharedDevice->GetDXDevice()->OpenSharedHandle(
        heapHandle, IID_PPV_ARGS(&crossAdapterResourceHeap[1]));

    // We can close the handle after opening the cross-adapter shared resource.
    CloseHandle(heapHandle);

    ThrowIfFailed(openSharedHandleResult);   

    sharedResource = std::make_shared<GResource>(sharedDevice, desc, crossAdapterResourceHeap[1], name + L" Shared");

    desc.Flags |= primeTextureExtraFlags;
    primeResource = std::make_shared<GResource>(primeDevice, desc, crossAdapterResourceHeap[0], name);

    isInit = true;
}

const GResource& GCrossAdapterResource::GetPrimeResource() const
{
    return *primeResource;
}

const GResource& GCrossAdapterResource::GetSharedResource() const
{
    return *sharedResource;
}

void GCrossAdapterResource::Reset()
{
    if (!isInit) return;

    if (primeResource)
    {
        primeResource->Reset();
        primeResource.reset();
    }

    if (sharedResource)
    {
        sharedResource->Reset();
        sharedResource.reset();
    }

    isInit = false;
}

void GCrossAdapterResource::Resize(const UINT newWidth, const UINT newHeight)
{
    if (!isInit) return;

    auto desc = primeResource->GetD3D12ResourceDesc();
    desc.Width = newWidth;
    desc.Height = newHeight;

    // Сохраняем устройства и имя перед удалением старых ресурсов
    auto pDevice = primeResource->GetDevice();
    auto sDevice = sharedResource->GetDevice();
    auto resName = primeResource->GetName();

    // Освобождаем старые ресурсы и кучи!
    Reset();

    // Вычисляем новый размер кучи
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT64 sizeInBytes;
    UINT64 totalBytes;
    pDevice->GetDXDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, &sizeInBytes, &totalBytes);
    UINT64 heapSize = Align(layout.Footprint.RowPitch * layout.Footprint.Height);
    heapSize = std::max(heapSize, totalBytes);

    // Создаем НОВУЮ кучу на Prime-устройстве
    CD3DX12_HEAP_DESC heapDesc(heapSize, D3D12_HEAP_TYPE_DEFAULT, 0, D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER);
    ThrowIfFailed(pDevice->GetDXDevice()->CreateHeap(&heapDesc, IID_PPV_ARGS(&crossAdapterResourceHeap[0])));

    // Обмениваемся новым Handle
    HANDLE heapHandle = nullptr;
    ThrowIfFailed(pDevice->GetDXDevice()->CreateSharedHandle(crossAdapterResourceHeap[0].Get(), nullptr, GENERIC_ALL, nullptr, &heapHandle));
    ThrowIfFailed(sDevice->GetDXDevice()->OpenSharedHandle(heapHandle, IID_PPV_ARGS(&crossAdapterResourceHeap[1])));
    CloseHandle(heapHandle);

    // Создаем новые ресурсы в новых кучах
    sharedResource = std::make_shared<GResource>(sDevice, desc, crossAdapterResourceHeap[1], resName + L" Shared");
    primeResource = std::make_shared<GResource>(pDevice, desc, crossAdapterResourceHeap[0], resName);

    isInit = true;
}
