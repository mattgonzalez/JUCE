/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 7 End-User License
   Agreement and JUCE Privacy Policy.

   End User License Agreement: www.juce.com/juce-7-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

struct DXGIAdapter : public ReferenceCountedObject
{
    DXGIAdapter(IDXGIAdapter* dxgiAdapter_)
        : dxgiAdapter(dxgiAdapter_)
    {
        uint32                    i = 0;
        ComSmartPtr<IDXGIOutput> dxgiOutput;

        while (dxgiAdapter_->EnumOutputs(i, dxgiOutput.resetAndGetPointerAddress()) != DXGI_ERROR_NOT_FOUND)
        {
            dxgiOutputs.push_back({ dxgiOutput });
            ++i;
        }
    }

    ComSmartPtr<IDXGIAdapter> dxgiAdapter;
    std::vector<ComSmartPtr<IDXGIOutput>> dxgiOutputs;

    using Ptr = ReferenceCountedObjectPtr<DXGIAdapter>;

#if JUCE_DIRECT2D
    HRESULT createDirect2DResources(ID2D1Factory2* direct2DFactory)
    {
        HRESULT hr = S_OK;

        if (direct3DDevice == nullptr)
        {
            direct2DDevice = nullptr;
            dxgiDevice = nullptr;

            // This flag adds support for surfaces with a different color channel ordering
            // than the API default. It is required for compatibility with Direct2D.
            UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if JUCE_DEBUG
            creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

            hr = D3D11CreateDevice(dxgiAdapter,
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                creationFlags,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                direct3DDevice.resetAndGetPointerAddress(),
                nullptr,
                nullptr);
        }

        if (SUCCEEDED(hr))
        {
            hr = direct3DDevice->QueryInterface(dxgiDevice.resetAndGetPointerAddress());
        }

        if (SUCCEEDED(hr) && direct2DDevice == nullptr)
        {
            hr = direct2DFactory->CreateDevice(dxgiDevice, direct2DDevice.resetAndGetPointerAddress());
        }

        return hr;
    }

    ComSmartPtr<ID3D11Device> direct3DDevice;
    ComSmartPtr<IDXGIDevice>  dxgiDevice;
    ComSmartPtr<ID2D1Device1> direct2DDevice;
#endif
};


class DXGIAdapters
{
public:
    void updateAdapters()
    {
        if (factory == nullptr)
            return;

        adapters.clear();

        UINT i = 0;
        ComSmartPtr<IDXGIAdapter> adapter;

        while (factory->EnumAdapters(i++, adapter.resetAndGetPointerAddress()) != DXGI_ERROR_NOT_FOUND)
        {
            adapters.add(new DXGIAdapter{ adapter });
        }
    }

    void clearAdapters()
    {
        adapters.clear();
    }

    auto const& getAdapters() const
    {
        return adapters;
    }

    IDXGIFactory2* getFactory() const { return factory; }

    static DXGIAdapters& getInstance()
    {
        static DXGIAdapters adapters;
        return adapters;
    }

    DXGIAdapter::Ptr const getAdapterForHwnd(HWND hwnd) const
    {
        if (auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL))
        {
            for (auto& adapter : DXGIAdapters::getInstance().getAdapters())
            {
                for (auto dxgiOutput : adapter->dxgiOutputs)
                {
                    DXGI_OUTPUT_DESC desc;
                    if (auto hr = dxgiOutput->GetDesc(&desc); SUCCEEDED(hr))
                    {
                        if (desc.Monitor == monitor)
                        {
                            return adapter;
                        }
                    }
                }
            }
        }

        return getDefaultAdapter();
    }

    DXGIAdapter::Ptr getDefaultAdapter() const
    {
        return adapters.getFirst();
    }

private:
    DXGIAdapters() = default;

    DynamicLibrary dxgiDll;
    ComSmartPtr<IDXGIFactory2> factory = [&]() -> ComSmartPtr<IDXGIFactory2>
    {
        if (! dxgiDll.open ("DXGI.dll"))
            return nullptr;

        JUCE_LOAD_WINAPI_FUNCTION (dxgiDll,
                                   CreateDXGIFactory,
                                   createDXGIFactory,
                                   HRESULT,
                                   (REFIID, void**))

        ComSmartPtr<IDXGIFactory2> result;
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wlanguage-extension-token")
        createDXGIFactory (__uuidof (IDXGIFactory2), (void**) result.resetAndGetPointerAddress());
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE

        return result;
    }();

    ReferenceCountedArray<DXGIAdapter> adapters;
};

} // namespace juce
