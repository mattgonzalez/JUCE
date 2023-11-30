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

struct IDWriteFactory;
struct IDWriteFontCollection;
struct IDWriteFontFamily;

namespace juce
{

class DirectWriteCustomFontCollectionLoader;

struct DirectX : public DeletedAtShutdown
{
    DirectX() = default;
    ~DirectX();

    //---------------------------------------------------------------------------
    //
    // DirectWrite
    //

    class DirectWrite
    {
    public:
        DirectWrite();
        ~DirectWrite();

        IDWriteFactory* getFactory() const { return directWriteFactory; }
        IDWriteFontCollection* getSystemFonts() const { return systemFonts; }
        IDWriteFontFamily* getFontFamilyForRawData(const void* data, size_t dataSize);
        OwnedArray<DirectWriteCustomFontCollectionLoader>& getCustomFontCollectionLoaders() { return customFontCollectionLoaders; }

    private:
        ComSmartPtr<IDWriteFactory> directWriteFactory;
        ComSmartPtr<IDWriteFontCollection> systemFonts;
        OwnedArray<DirectWriteCustomFontCollectionLoader> customFontCollectionLoaders;
    } directWrite;

    //---------------------------------------------------------------------------
    //
    // Direct2D
    //

    class Direct2D
    {
    public:
        Direct2D();
        ~Direct2D();

        ID2D1Factory2* getFactory() const { return d2dSharedFactory; }
        ID2D1DCRenderTarget* getDirectWriteRenderTarget() const { return directWriteRenderTarget; }

    private:
        ComSmartPtr<ID2D1Factory2> d2dSharedFactory;
        ComSmartPtr<ID2D1DCRenderTarget> directWriteRenderTarget;
    } direct2D;

    //---------------------------------------------------------------------------
    //
    // DXGI
    //
    struct DXGI
    {
    private:
        ComSmartPtr<IDXGIFactory2> factory = [&]() -> ComSmartPtr<IDXGIFactory2>
            {
                ComSmartPtr<IDXGIFactory2> result;
                CreateDXGIFactory(__uuidof (IDXGIFactory2), (void**)result.resetAndGetPointerAddress());
                return result;
            }();

    public:
        DXGI() :
            adapters(factory)
        {
        }
        ~DXGI()
        {
            adapters.clearAdapterArray();
        }

        struct Adapter : public ReferenceCountedObject
        {
            Adapter(IDXGIAdapter* dxgiAdapter_)
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

            ~Adapter() = default;

            ComSmartPtr<IDXGIAdapter> dxgiAdapter;
            std::vector<ComSmartPtr<IDXGIOutput>> dxgiOutputs;

            using Ptr = ReferenceCountedObjectPtr<Adapter>;

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
                    direct2DDeviceUniqueID = Uuid::null();

                    hr = direct2DFactory->CreateDevice(dxgiDevice, direct2DDevice.resetAndGetPointerAddress());
                    if (SUCCEEDED(hr))
                    {
                        direct2DDeviceUniqueID = Uuid{};
                    }
                }

                return hr;
            }

            ComSmartPtr<ID3D11Device> direct3DDevice;
            ComSmartPtr<IDXGIDevice>  dxgiDevice;
            ComSmartPtr<ID2D1Device1> direct2DDevice;
            Uuid                      direct2DDeviceUniqueID;
#endif
        };

        class Adapters
        {
        public:
            Adapters(IDXGIFactory2* factory_) :
                factory(factory_)
            {
                updateAdapters();
            }

            ~Adapters() = default;

            void updateAdapters()
            {
                if (factory == nullptr)
                    return;

                //adapterArray.clear();

                UINT i = 0;
                ComSmartPtr<IDXGIAdapter> adapter;

                while (factory->EnumAdapters(i++, adapter.resetAndGetPointerAddress()) != DXGI_ERROR_NOT_FOUND)
                {
                    adapterArray.add(new Adapter{ adapter });
                }
            }

            void clearAdapterArray()
            {
                adapterArray.clear();
            }

            auto const& getAdapterArray() const
            {
                return adapterArray;
            }

            IDXGIFactory2* getFactory() const { return factory; }

            Adapter::Ptr const getAdapterForHwnd(HWND hwnd) const
            {
                if (auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL))
                {
                    for (auto& adapter : adapterArray)
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

            Adapter::Ptr getDefaultAdapter() const
            {
                return adapterArray.getFirst();
            }

        private:
            ComSmartPtr<IDXGIFactory2> factory;
            ReferenceCountedArray<Adapter> adapterArray;
        } adapters;

        IDXGIFactory2* getFactory() const { return factory; }

    } dxgi;

    JUCE_DECLARE_SINGLETON(DirectX, false)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DirectX)
};

} // namespace juce
