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

#ifdef __INTELLISENSE__

    #define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
    #define JUCE_WINDOWS                    1

    #include <d2d1_2.h>
    #include <d3d11_1.h>
    #include <dcomp.h>
    #include <dwrite.h>
    #include <juce_core/juce_core.h>
    #include <juce_graphics/juce_graphics.h>
    #include <windows.h>

#endif

namespace juce
{

#if JUCE_USE_DIRECTWRITE
namespace
{
    static String getLocalisedName (IDWriteLocalizedStrings* names)
    {
        jassert (names != nullptr);

        uint32 index = 0;
        BOOL exists = false;
        [[maybe_unused]] auto hr = names->FindLocaleName (L"en-us", &index, &exists);

        if (! exists)
            index = 0;

        uint32 length = 0;
        hr = names->GetStringLength (index, &length);

        HeapBlock<wchar_t> name (length + 1);
        hr = names->GetString (index, name, length + 1);

        return static_cast<const wchar_t*> (name);
    }

    static String getFontFamilyName (IDWriteFontFamily* family)
    {
        jassert (family != nullptr);
        ComSmartPtr<IDWriteLocalizedStrings> familyNames;
        [[maybe_unused]] auto hr = family->GetFamilyNames (familyNames.resetAndGetPointerAddress());
        jassert (SUCCEEDED (hr));
        return getLocalisedName (familyNames);
    }

    static String getFontFaceName (IDWriteFont* font)
    {
        jassert (font != nullptr);
        ComSmartPtr<IDWriteLocalizedStrings> faceNames;
        [[maybe_unused]] auto hr = font->GetFaceNames (faceNames.resetAndGetPointerAddress());
        jassert (SUCCEEDED (hr));
        return getLocalisedName (faceNames);
    }

    inline Point<float> convertPoint (D2D1_POINT_2F p) noexcept   { return Point<float> ((float) p.x, (float) p.y); }
}

class DirectXFactories
{
public:
    struct GraphicsOutput
    {
        ComSmartPtr<IDXGIOutput> dxgiOutput;
    };

    struct GraphicsAdapter : public ReferenceCountedObject
    {
        GraphicsAdapter (DirectXFactories& directXFactories_, const ComSmartPtr<IDXGIAdapter>& dxgiAdapter_)
            : directXFactories (directXFactories_), dxgiAdapter (dxgiAdapter_)
        {
            uint32                    i = 0;
            ComSmartPtr<IDXGIOutput> dxgiOutput;

            while (dxgiAdapter_->EnumOutputs (i, dxgiOutput.resetAndGetPointerAddress()) != DXGI_ERROR_NOT_FOUND)
            {
                outputs.push_back ({ dxgiOutput });
                ++i;
            }
        }

        HRESULT createDirect2DResources()
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

                hr = D3D11CreateDevice (dxgiAdapter,
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
                hr = direct3DDevice->QueryInterface (dxgiDevice.resetAndGetPointerAddress());
            }

            if (SUCCEEDED(hr) && direct2DDevice == nullptr)
            {
                hr = directXFactories.getDirect2DFactory()->CreateDevice (dxgiDevice, direct2DDevice.resetAndGetPointerAddress());
            }

            return hr;
        }

        DirectXFactories& directXFactories;
        ComSmartPtr<IDXGIAdapter> dxgiAdapter;
        ComSmartPtr<ID3D11Device> direct3DDevice;
        ComSmartPtr<IDXGIDevice>  dxgiDevice;
        ComSmartPtr<ID2D1Device1> direct2DDevice;
        std::vector<GraphicsOutput> outputs;

        using Ptr = ReferenceCountedObjectPtr<GraphicsAdapter>;
    };

    DirectXFactories()
    {
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wlanguage-extension-token")

        if (direct2dDll.open ("d2d1.dll"))
        {
            JUCE_LOAD_WINAPI_FUNCTION (direct2dDll, D2D1CreateFactory, d2d1CreateFactory,
                                       HRESULT, (D2D1_FACTORY_TYPE, REFIID, D2D1_FACTORY_OPTIONS*, void**))

            if (d2d1CreateFactory != nullptr)
            {
                D2D1_FACTORY_OPTIONS options;
#if JUCE_DEBUG
                options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#else
                options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
                d2d1CreateFactory (D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof (ID2D1Factory1), &options,
                                   (void**) d2dSharedFactory.resetAndGetPointerAddress());
            }
        }

        if (directWriteDll.open ("DWrite.dll"))
        {
            JUCE_LOAD_WINAPI_FUNCTION (directWriteDll, DWriteCreateFactory, dWriteCreateFactory,
                                       HRESULT, (DWRITE_FACTORY_TYPE, REFIID, IUnknown**))

            if (dWriteCreateFactory != nullptr)
            {
                dWriteCreateFactory (DWRITE_FACTORY_TYPE_SHARED, __uuidof (IDWriteFactory),
                                     (IUnknown**) directWriteFactory.resetAndGetPointerAddress());

                if (directWriteFactory != nullptr)
                {
                    directWriteFactory->GetSystemFontCollection (systemFonts.resetAndGetPointerAddress());
            }
            }

            if (d2dSharedFactory != nullptr)
            {
                auto d2dRTProp = D2D1::RenderTargetProperties (D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                               D2D1::PixelFormat (DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                                  D2D1_ALPHA_MODE_IGNORE),
                                                               0, 0,
                                                               D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE,
                                                               D2D1_FEATURE_LEVEL_DEFAULT);

                d2dSharedFactory->CreateDCRenderTarget (&d2dRTProp, directWriteRenderTarget.resetAndGetPointerAddress());
            }
        }

#if JUCE_DIRECT2D
        DXGIAdapters::getInstance().updateAdapters();
        adapters.clear();

        for (const auto& adapterPtr : DXGIAdapters::getInstance().getAdapters())
        {
            const GraphicsAdapter::Ptr adapter { new GraphicsAdapter { *this, adapterPtr } };

            if (! adapter->outputs.empty())
                adapters.add (adapter);
        }
#endif

        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    }

    ~DirectXFactories()
    {
#if JUCE_DIRECT2D
        adapters.clear();

        if (directWriteFactory != nullptr)
        {
            //
            // Unregister all the custom font stuff and then clear the array before releasing the factories
            //
            for (auto customFontCollectionLoader : customFontCollectionLoaders)
            {
                directWriteFactory->UnregisterFontCollectionLoader(customFontCollectionLoader);
                directWriteFactory->UnregisterFontFileLoader(customFontCollectionLoader->getFontFileLoader());
            }

            customFontCollectionLoaders.clear();
        }

#endif
        d2dSharedFactory = nullptr;  // (need to make sure these are released before deleting the DynamicLibrary objects)
        directWriteFactory = nullptr;
        systemFonts = nullptr;
        directWriteRenderTarget = nullptr;
    }

#if JUCE_DIRECT2D
    IDWriteFontFamily* getFontFamilyForRawData(const void* data, size_t dataSize)
    {
        //
        // Hopefully the raw data here is pointing to a TrueType font file in memory. 
        // This creates a custom font collection loader (one custom font per font collection)
        //
        if (directWriteFactory != nullptr)
        {
            DirectWriteCustomFontCollectionLoader* customFontCollectionLoader = nullptr;
            for (auto loader : customFontCollectionLoaders)
            {
                if (loader->hasRawData(data, dataSize))
                {
                    customFontCollectionLoader = loader;
                    break;
                }
            }

            if (customFontCollectionLoader == nullptr)
            {
                customFontCollectionLoader = customFontCollectionLoaders.add(new DirectWriteCustomFontCollectionLoader{ data, dataSize });

                directWriteFactory->RegisterFontFileLoader(customFontCollectionLoader->getFontFileLoader());
                directWriteFactory->RegisterFontCollectionLoader(customFontCollectionLoader);

                directWriteFactory->CreateCustomFontCollection(customFontCollectionLoader,
                    &customFontCollectionLoader->key,
                    sizeof(customFontCollectionLoader->key),
                    customFontCollectionLoader->customFontCollection.resetAndGetPointerAddress());
            }

            if (customFontCollectionLoader != nullptr && customFontCollectionLoader->customFontCollection != nullptr)
            {
                IDWriteFontFamily* directWriteFontFamily = nullptr;
                auto hr = customFontCollectionLoader->customFontCollection->GetFontFamily(0, &directWriteFontFamily);
                if (SUCCEEDED(hr))
                {
                    return directWriteFontFamily;
                }
            }
        }

        return nullptr;
    }

    IDXGIFactory2* getDXGIFactory() const
    {
        jassert (MessageManager::getInstance()->existsAndIsLockedByCurrentThread());
        return DXGIAdapters::getInstance().getFactory();
    }
#endif

    IDWriteFactory* getDirectWriteFactory() const
    {
        jassert(MessageManager::getInstance()->existsAndIsLockedByCurrentThread());
        return directWriteFactory;
    }

    IDWriteFontCollection* getSystemFonts() const
    {
        jassert (MessageManager::getInstance()->existsAndIsLockedByCurrentThread());
        return systemFonts;
    }

#if JUCE_DIRECT2D
    OwnedArray<DirectWriteCustomFontCollectionLoader>& getCustomFontCollectionLoaders()
    {
        jassert(MessageManager::getInstance()->isThisTheMessageThread());
        return customFontCollectionLoaders;
    }

    GraphicsAdapter::Ptr const getAdapterForHwnd (HWND hwnd) const
    {
        if (auto monitor = MonitorFromWindow (hwnd, MONITOR_DEFAULTTONULL))
        {
            for (auto& adapter : adapters)
            {
                for (auto output : adapter->outputs)
                {
                    DXGI_OUTPUT_DESC desc;
                    if (auto hr = output.dxgiOutput->GetDesc (&desc); SUCCEEDED (hr))
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

    GraphicsAdapter::Ptr getDefaultAdapter() const
    {
        return adapters.getFirst();
    }
#endif

    ID2D1DCRenderTarget* getDirectWriteRenderTarget() const
    {
        return directWriteRenderTarget;
    }

    ID2D1Factory2* getDirect2DFactory() const
    {
        return d2dSharedFactory;
    }

private:
    ComSmartPtr<ID2D1Factory2> d2dSharedFactory;
    ComSmartPtr<IDWriteFactory> directWriteFactory;
    ComSmartPtr<IDWriteFontCollection> systemFonts;
    ComSmartPtr<ID2D1DCRenderTarget> directWriteRenderTarget;

    DynamicLibrary direct2dDll, directWriteDll;

#if JUCE_DIRECT2D
    OwnedArray<DirectWriteCustomFontCollectionLoader> customFontCollectionLoaders;

    ReferenceCountedArray<GraphicsAdapter> adapters;
#endif
};

//==============================================================================
class WindowsDirectWriteTypeface  : public Typeface
{
public:
    WindowsDirectWriteTypeface (const Font& font, IDWriteFontCollection* fontCollection)
        : Typeface (font.getTypefaceName(), font.getTypefaceStyle())
    {
        jassert (fontCollection != nullptr);

        uint32 fontIndex = 0;
        [[maybe_unused]] auto hr = fontCollection->FindFamilyName (font.getTypefaceName().toWideCharPointer(), &fontIndex, &fontFound);

        if (! fontFound)
            fontIndex = 0;

        // Get the font family using the search results
        // Fonts like: Times New Roman, Times New Roman Bold, Times New Roman Italic are all in the same font family
        ComSmartPtr<IDWriteFontFamily> dwFontFamily;
        hr = fontCollection->GetFontFamily (fontIndex, dwFontFamily.resetAndGetPointerAddress());

        // Get a specific font in the font family using typeface style
        {
            ComSmartPtr<IDWriteFont> dwFont;

            for (int i = (int) dwFontFamily->GetFontCount(); --i >= 0;)
            {
                hr = dwFontFamily->GetFont ((UINT32) i, dwFont.resetAndGetPointerAddress());

                if (i == 0)
                    break;

                ComSmartPtr<IDWriteLocalizedStrings> faceNames;
                hr = dwFont->GetFaceNames (faceNames.resetAndGetPointerAddress());

                if (font.getTypefaceStyle() == getLocalisedName (faceNames))
                    break;
            }

            jassert (dwFont != nullptr);
            hr = dwFont->CreateFontFace (dwFontFace.resetAndGetPointerAddress());
        }

        initializeFromFontFace();
    }
   
#if JUCE_DIRECT2D
    //
    // Alternate constructor for WindowsDirectWriteTypeface to create the typeface from TTF data in memory
    //
    WindowsDirectWriteTypeface (const void* data, size_t dataSize) :
        Typeface({}, {}) // set the typeface name & style as empty initially
    {
        //
        // Get the DirectWrite font family for the raw data
        //
        auto fontFamily = factories->getFontFamilyForRawData(data, dataSize);
        if (fontFamily == nullptr)
        {
            return;
        }

        //
        // Get the JUCE typeface name from the DirectWrite font family
        //
        {
            ComSmartPtr<IDWriteLocalizedStrings> familyNames;
            auto hr = fontFamily->GetFamilyNames(familyNames.resetAndGetPointerAddress());
            if (FAILED(hr))
            {
                return;
            }

            name = getLocalisedName(familyNames);
        }

        //
        // Get the JUCE typeface style from the DirectWrite font and get the font face
        // 
        // Only supports one font per family
        // 
        {
            ComSmartPtr<IDWriteFont> directWriteFont;
            auto hr = fontFamily->GetFont(0, directWriteFont.resetAndGetPointerAddress());
            if (FAILED(hr))
        {
                return;
            }

            ComSmartPtr<IDWriteLocalizedStrings> faceNames;
            hr = directWriteFont->GetFaceNames(faceNames.resetAndGetPointerAddress());
            if (FAILED(hr))
            {
                return;
            }

            fontFound = true;

            style = getLocalisedName(faceNames);

            hr = directWriteFont->CreateFontFace(dwFontFace.resetAndGetPointerAddress());
            if (FAILED(hr))
            {
                return;
            }
        }

        initializeFromFontFace();
    }
#endif

    bool loadedOk() const noexcept          { return dwFontFace != nullptr; }
    BOOL isFontFound() const noexcept       { return fontFound; }

    float getAscent() const                 { return ascent; }
    float getDescent() const                { return 1.0f - ascent; }
    float getHeightToPointsFactor() const   { return heightToPointsFactor; }

    float getStringWidth (const String& text)
    {
        auto textUTF32 = text.toUTF32();
        auto len = textUTF32.length();

        HeapBlock<UINT16> glyphIndices (len);
        dwFontFace->GetGlyphIndices (textUTF32, (UINT32) len, glyphIndices);

        HeapBlock<DWRITE_GLYPH_METRICS> dwGlyphMetrics (len);
        dwFontFace->GetDesignGlyphMetrics (glyphIndices, (UINT32) len, dwGlyphMetrics, false);

        float x = 0;

        for (size_t i = 0; i < len; ++i)
            x += (float) dwGlyphMetrics[i].advanceWidth / (float) designUnitsPerEm;

        return x * unitsToHeightScaleFactor;
    }

    void getGlyphPositions (const String& text, Array<int>& resultGlyphs, Array<float>& xOffsets)
    {
        xOffsets.add (0);

        auto textUTF32 = text.toUTF32();
        auto len = textUTF32.length();

        HeapBlock<UINT16> glyphIndices (len);
        dwFontFace->GetGlyphIndices (textUTF32, (UINT32) len, glyphIndices);
        HeapBlock<DWRITE_GLYPH_METRICS> dwGlyphMetrics (len);
        dwFontFace->GetDesignGlyphMetrics (glyphIndices, (UINT32) len, dwGlyphMetrics, false);

        float x = 0;

        for (size_t i = 0; i < len; ++i)
        {
            x += (float) dwGlyphMetrics[i].advanceWidth / (float) designUnitsPerEm;
            xOffsets.add (x * unitsToHeightScaleFactor);
            resultGlyphs.add (glyphIndices[i]);
        }
    }

    bool getOutlineForGlyph (int glyphNumber, Path& path)
    {
        jassert (path.isEmpty());  // we might need to apply a transform to the path, so this must be empty
        auto glyphIndex = (UINT16) glyphNumber;
        ComSmartPtr<PathGeometrySink> pathGeometrySink (new PathGeometrySink());

        dwFontFace->GetGlyphRunOutline (1024.0f, &glyphIndex, nullptr, nullptr,
                                        1, false, false, pathGeometrySink);
        path = pathGeometrySink->path;

        if (! pathTransform.isIdentity())
            path.applyTransform (pathTransform);

        return true;
    }

    IDWriteFontFace* getIDWriteFontFace() const noexcept    { return dwFontFace; }

    float getUnitsToHeightScaleFactor() const noexcept      { return unitsToHeightScaleFactor; }

private:
    SharedResourcePointer<DirectXFactories> factories;
    ComSmartPtr<IDWriteFontFace> dwFontFace;
    float unitsToHeightScaleFactor = 1.0f, heightToPointsFactor = 1.0f, ascent = 0;
    int designUnitsPerEm = 0;
    AffineTransform pathTransform;
    BOOL fontFound = false;

    //
    // D.R.Y. since this code is common to both constructors
    //
    void initializeFromFontFace()
    {
        if (dwFontFace != nullptr)
        {
            DWRITE_FONT_METRICS dwFontMetrics;
            dwFontFace->GetMetrics(&dwFontMetrics);

            // All Font Metrics are in design units so we need to get designUnitsPerEm value
            // to get the metrics into Em/Design Independent Pixels
            designUnitsPerEm = dwFontMetrics.designUnitsPerEm;

            ascent = std::abs((float)dwFontMetrics.ascent);
            auto totalSize = ascent + std::abs((float)dwFontMetrics.descent);
            ascent /= totalSize;
            unitsToHeightScaleFactor = (float)designUnitsPerEm / totalSize;

            auto tempDC = GetDC(nullptr);
            auto dpi = (float)(GetDeviceCaps(tempDC, LOGPIXELSX) + GetDeviceCaps(tempDC, LOGPIXELSY)) / 2.0f;
            heightToPointsFactor = (dpi / (float)GetDeviceCaps(tempDC, LOGPIXELSY)) * unitsToHeightScaleFactor;
            ReleaseDC(nullptr, tempDC);

            auto pathAscent = (1024.0f * dwFontMetrics.ascent) / (float)designUnitsPerEm;
            auto pathDescent = (1024.0f * dwFontMetrics.descent) / (float)designUnitsPerEm;
            auto pathScale = 1.0f / (std::abs(pathAscent) + std::abs(pathDescent));
            pathTransform = AffineTransform::scale(pathScale);
        }
    }

    struct PathGeometrySink  : public ComBaseClassHelper<IDWriteGeometrySink>
    {
        PathGeometrySink() : ComBaseClassHelper (0) {}

        void STDMETHODCALLTYPE AddBeziers (const D2D1_BEZIER_SEGMENT* beziers, UINT beziersCount) noexcept override
        {
            for (UINT i = 0; i < beziersCount; ++i)
                path.cubicTo (convertPoint (beziers[i].point1),
                              convertPoint (beziers[i].point2),
                              convertPoint (beziers[i].point3));
        }

        void STDMETHODCALLTYPE AddLines (const D2D1_POINT_2F* points, UINT pointsCount) noexcept override
        {
            for (UINT i = 0; i < pointsCount; ++i)
                path.lineTo (convertPoint (points[i]));
        }

        void STDMETHODCALLTYPE BeginFigure (D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN) noexcept override
        {
            path.startNewSubPath (convertPoint (startPoint));
        }

        void STDMETHODCALLTYPE EndFigure (D2D1_FIGURE_END figureEnd) noexcept override
        {
            if (figureEnd == D2D1_FIGURE_END_CLOSED)
                path.closeSubPath();
        }

        void STDMETHODCALLTYPE SetFillMode (D2D1_FILL_MODE fillMode) noexcept override
        {
            path.setUsingNonZeroWinding (fillMode == D2D1_FILL_MODE_WINDING);
        }

        void STDMETHODCALLTYPE SetSegmentFlags (D2D1_PATH_SEGMENT) noexcept override {}
        JUCE_COMRESULT Close() noexcept override  { return S_OK; }

        Path path;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PathGeometrySink)
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowsDirectWriteTypeface)
};

#endif

} // namespace juce
