
#ifdef __INTELLISENSE__

    #define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
    #define JUCE_WINDOWS                    1

    #include <windows.h>
    #include <juce_core/juce_core.h>
    #include <juce_graphics/juce_graphics.h>
    #include <d2d1_2.h>
    #include <d3d11_1.h>
    #include <dwrite.h>
    #include <dcomp.h>
    #include "juce_win32_Direct2DHelpers.cpp"
    #include "juce_win32_Direct2DCommandQueue.cpp"

#endif

#define JUCE_DIRECT2D_DIRECT_COMPOSITION 1

namespace juce
{
namespace direct2d
{

//==============================================================================
//
// Device context and transform
//

struct DeviceContext
{
    HRESULT createHwndRenderTarget(HWND hwnd)
    {
        HRESULT hr = S_OK;

        if (hwndRenderTarget == nullptr)
        {
            SharedResourcePointer<DirectXFactories> factories;

            D2D1_SIZE_U size{ 1, 1 };

            D2D1_RENDER_TARGET_PROPERTIES renderTargetProps{};
            renderTargetProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            renderTargetProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;

            D2D1_HWND_RENDER_TARGET_PROPERTIES hwndRenderTargetProps{};
            hwndRenderTargetProps.hwnd = hwnd;
            hwndRenderTargetProps.pixelSize = size;
            hwndRenderTargetProps.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY | D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS;
            hr = factories->getDirect2DFactory()->CreateHwndRenderTarget(&renderTargetProps,
                &hwndRenderTargetProps,
                hwndRenderTarget.resetAndGetPointerAddress());
        }

        return hr;
    }

    void resetTransform()
    {
        context->SetTransform (D2D1::IdentityMatrix());
        transform = {};
    }

    //
    // The profiler shows that calling deviceContext->SetTransform is
    // surprisingly expensive. This class only calls SetTransform
    // if the transform is changing
    //
    void setTransform (AffineTransform newTransform)
    {
        if (approximatelyEqual (transform.mat00, newTransform.mat00) && approximatelyEqual (transform.mat01, newTransform.mat01) &&
            approximatelyEqual (transform.mat02, newTransform.mat02) && approximatelyEqual (transform.mat10, newTransform.mat10) &&
            approximatelyEqual (transform.mat11, newTransform.mat11) && approximatelyEqual (transform.mat12, newTransform.mat12))
        {
            return;
        }

        context->SetTransform (transformToMatrix (newTransform));
        transform = newTransform;
    }

    void release()
    {
        hwndRenderTarget = nullptr;
        context = nullptr;
    }

    ComSmartPtr<ID2D1DeviceContext1> context;
    ComSmartPtr<ID2D1HwndRenderTarget> hwndRenderTarget;
    AffineTransform                 transform;
};

class GeometryCache
{
public:
    void release()
    {
        hashMap.clear();
        cache.clear();
    }

    ID2D1GeometryRealization* getFilledGeometryRealisation(const Path& path, ID2D1Factory2* factory, ID2D1DeviceContext1* deviceContext, float dpiScaleFactor)
    {
        Hasher hasher{ path, dpiScaleFactor };
        auto hash = hasher.calculateHash();

        if (auto cachedGeometry = getCachedGeometry(hash))
        {
            if (! cachedGeometry->filledGeometryRealisation)
            {
                if (auto geometry = direct2d::pathToPathGeometry(factory, path))
                {
                    auto hr = deviceContext->CreateFilledGeometryRealization(geometry, hasher.flatteningTolerance, cachedGeometry->filledGeometryRealisation.resetAndGetPointerAddress());

                    if (hr == E_OUTOFMEMORY)
                    {
                        releaseOldestEntry();
                    }
                }
            }

            return cachedGeometry->filledGeometryRealisation;
        }

        return nullptr;
    }

    ID2D1GeometryRealization* getStrokedGeometryRealisation(const Path& path, const PathStrokeType& strokeType, ID2D1Factory2* factory, ID2D1DeviceContext1* deviceContext, float dpiScaleFactor)
    {
        Hasher hasher{ path, strokeType, dpiScaleFactor };
        auto hash = hasher.calculateHash();

        if (auto cachedGeometry = getCachedGeometry(hash))
        {
            if (!cachedGeometry->strokedGeometryRealisation)
            {
                if (auto geometry = direct2d::pathToPathGeometry(factory, path))
                {
                    if (auto strokeStyle = direct2d::pathStrokeTypeToStrokeStyle(factory, strokeType))
                    {
                        auto hr = deviceContext->CreateStrokedGeometryRealization(geometry, hasher.flatteningTolerance, 
                            strokeType.getStrokeThickness(), strokeStyle,
                            cachedGeometry->strokedGeometryRealisation.resetAndGetPointerAddress());

                        if (hr == E_OUTOFMEMORY)
                        {
                            releaseOldestEntry();
                        }
                    }
                }
            }

            return cachedGeometry->strokedGeometryRealisation;
        }

        return nullptr;
    }

private:

    static float findGeometryFlatteningTolerance(float dpiScaleFactor, /*const AffineTransform& transform,*/ float maxZoomFactor = 1.0f)
    {
        jassert(maxZoomFactor > 0.0f);

        //
        // Could use D2D1::ComputeFlatteningTolerance here, but that requires defining NTDDI_VERSION and it doesn't do anything special.
        // 
        // Direct2D default flattening tolerance is 0.25
        //
        //auto transformScaleFactor = std::sqrt(std::abs(transform.getDeterminant()));
        return 0.25f / (/*transformScaleFactor **/ dpiScaleFactor * maxZoomFactor);
    }

    //--------------------------------------------------------------------------
    //
    // Hashing
    //
    struct Hasher
    {
        Hasher(Path const& path, float dpiScaleFactor) :
            startingHash(path.calculateHash()),
            flatteningTolerance(findGeometryFlatteningTolerance(dpiScaleFactor)),
            nonZeroWinding(path.isUsingNonZeroWinding())
        {
        }

        Hasher(Path const& path, PathStrokeType const& strokeType, float dpiScaleFactor) :
            Hasher(path, dpiScaleFactor)
        {
            strokeThickness = strokeType.getStrokeThickness();
            jointStyle = static_cast<int16>(strokeType.getJointStyle());
            endCapStyle = static_cast<int16>(strokeType.getEndStyle());
        }

        size_t calculateHash() const
        {
            std::string_view tempStringView{ reinterpret_cast<char const*>(this), sizeof(Hasher) };
            return std::hash<std::string_view>{}(tempStringView);
        }

        size_t startingHash = 0;
        float strokeThickness = 0.0f;
        float flatteningTolerance = 0.0f;
        int16 jointStyle = 0;
        int16 endCapStyle = 0;
        int16 nonZeroWinding = 0;
        int16 reserved = 0;
    };


    //--------------------------------------------------------------------------
    //
    // Caching
    //
    struct CachedGeometry
    {
        CachedGeometry(size_t hash_) :
            hash(hash_)
        {
        }

        CachedGeometry(CachedGeometry&& other)  noexcept :
            timestamp(other.timestamp),
            hash(other.hash),
            filledGeometryRealisation(other.filledGeometryRealisation),
            strokedGeometryRealisation(other.strokedGeometryRealisation)
        {
        }

        ~CachedGeometry() = default;

        int64 timestamp = Time::getHighResolutionTicks();
        size_t hash = 0;
        ComSmartPtr<ID2D1GeometryRealization> filledGeometryRealisation;
        ComSmartPtr<ID2D1GeometryRealization> strokedGeometryRealisation;

        JUCE_DECLARE_WEAK_REFERENCEABLE(CachedGeometry)
    };

    std::deque<CachedGeometry> cache;
    std::unordered_map<size_t, WeakReference<CachedGeometry>> hashMap;

    CachedGeometry* getCachedGeometry(size_t hash)
    {
        auto cachedGeometry = hashMap[hash];

        releaseExpiredEntries();

        if (cachedGeometry.get())
        {
            cachedGeometry->timestamp = Time::getHighResolutionTicks();
            return cachedGeometry.get();
        }

        cache.emplace_back(CachedGeometry{ hash });
        cachedGeometry = &cache.back();
        hashMap[hash] = cachedGeometry;

        auto check = hashMap[hash];
        jassert(check);
        jassert(check->hash == hash);
        jassert(cachedGeometry->hash == hash);

        return cachedGeometry;
    }

    void releaseExpiredEntries()
    {
        auto cutoff = Time::getHighResolutionTicks() - Time::secondsToHighResolutionTicks(5.0);

        while (cache.size() > 0)
        {
            auto& front = cache.front();
            if (front.timestamp > cutoff)
            {
                break;
            }

            front.filledGeometryRealisation = nullptr;
            front.strokedGeometryRealisation = nullptr;

            hashMap.erase(front.hash);
            cache.pop_front();
        }
    }

    void releaseOldestEntry()
    {
        if (cache.size() > 0)
        {
            auto& front = cache.front();
            auto hash = front.hash;

            front.filledGeometryRealisation = nullptr;
            front.strokedGeometryRealisation = nullptr;

            cache.pop_front();

            hashMap.erase(hash);
        }
    }
};


//==============================================================================
//
// Device resources
//

class DeviceResources
{
public:
    DeviceResources() {}

    ~DeviceResources()
    {
        release();
    }

    //
    // Create a Direct2D device context for a DXGI adapter
    //
    HRESULT create(DXGIAdapter::Ptr adapter, double dpiScalingFactor)
    {
        HRESULT hr = S_OK;

        jassert(adapter);

        if (deviceContext.context == nullptr)
        {
            SharedResourcePointer<DirectXFactories> factories;

            hr = adapter->createDirect2DResources(factories->getDirect2DFactory());
            if (SUCCEEDED(hr))
            {
                hr = adapter->direct2DDevice->CreateDeviceContext (D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                    deviceContext.context.resetAndGetPointerAddress());
            }

            if (deviceContext.context)
            {
                deviceContext.context->SetTextAntialiasMode (D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

                float dpi = (float) (USER_DEFAULT_SCREEN_DPI * dpiScalingFactor);
                deviceContext.context->SetDpi (dpi, dpi);

                if (colourBrush == nullptr)
                {
                    hr = deviceContext.context->CreateSolidColorBrush (D2D1::ColorF (0.0f, 0.0f, 0.0f, 1.0f),
                                                                       colourBrush.resetAndGetPointerAddress());
                    jassertquiet (SUCCEEDED (hr));
                }
            }
        }

        return hr;
    }

    void release()
    {
        geometryCache.release();
        colourBrush = nullptr;
        deviceContext.release();
    }

    bool canPaint()
    {
        return deviceContext.context != nullptr && colourBrush != nullptr;
    }

    DeviceContext                     deviceContext;
    ComSmartPtr<ID2D1SolidColorBrush> colourBrush;
    GeometryCache                     geometryCache;
};

//==============================================================================
//
// Swap chain
//

class SwapChain
{
public:
    SwapChain() = default;

    ~SwapChain()
    {
        release();
    }

    HRESULT create(HWND hwnd, Rectangle<int> size, DXGIAdapter::Ptr adapter)
    {
        if (!chain && hwnd)
        {
            SharedResourcePointer<DirectXFactories> factories;
            auto dxgiFactory = factories->getDXGIFactory();

            if (dxgiFactory == nullptr || adapter->direct3DDevice == nullptr)
            {
                return E_FAIL;
            }

            HRESULT hr = S_OK;

            buffer = nullptr;
            chain  = nullptr;

            //
            // Make the waitable swap chain
            //
            // Create the swap chain with premultiplied alpha support for transparent windows
            //
            DXGI_SWAP_CHAIN_DESC1 swapChainDescription = {};
            swapChainDescription.Format                = DXGI_FORMAT_B8G8R8A8_UNORM;
            swapChainDescription.Width                 = size.getWidth();
            swapChainDescription.Height                = size.getHeight();
            swapChainDescription.SampleDesc.Count      = 1;
            swapChainDescription.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDescription.BufferCount           = bufferCount;
            swapChainDescription.SwapEffect            = swapEffect;
            swapChainDescription.Flags                 = swapChainFlags;

            swapChainDescription.Scaling   = DXGI_SCALING_STRETCH;
            swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
            hr                             = dxgiFactory->CreateSwapChainForComposition (adapter->direct3DDevice,
                                                             &swapChainDescription,
                                                             nullptr,
                                                             chain.resetAndGetPointerAddress());
            jassert (SUCCEEDED (hr));

            std::optional<ScopedEvent> swapChainEvent;

            if (SUCCEEDED (hr))
            {
                TRACE_LOG_D2D_CREATE_RESOURCE ("swapchain");

                //
                // Get the waitable swap chain presentation event and set the maximum frame latency
                //
                ComSmartPtr<IDXGISwapChain2> chain2;
                chain.QueryInterface<IDXGISwapChain2> (chain2);
                if (chain2)
                {
                    swapChainEvent.emplace (chain2->GetFrameLatencyWaitableObject());
                    if (swapChainEvent->getHandle() == INVALID_HANDLE_VALUE)
                        return E_NOINTERFACE;

                    hr = chain2->SetMaximumFrameLatency (2);
                    if (SUCCEEDED (hr))
                    {
                        state = State::chainAllocated;

                        TRACE_LOG_D2D_RESOURCE (etw::createSwapChain);
                    }
                }
            }
            else
            {
                return E_NOINTERFACE;
            }

            if (swapChainEvent.has_value() && swapChainEvent->getHandle() != nullptr)
                swapChainDispatcher.emplace (std::move (*swapChainEvent));

            return hr;
        }

        return S_OK;
    }

    HRESULT createBuffer (ID2D1DeviceContext* const deviceContext)
    {
        if (deviceContext && chain && ! buffer)
        {
            ComSmartPtr<IDXGISurface> surface;
            auto hr = chain->GetBuffer (0, __uuidof (surface), reinterpret_cast<void**> (surface.resetAndGetPointerAddress()));
            if (SUCCEEDED (hr))
            {
                D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
                bitmapProperties.bitmapOptions           = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
                bitmapProperties.pixelFormat.format      = DXGI_FORMAT_B8G8R8A8_UNORM;
                bitmapProperties.pixelFormat.alphaMode   = D2D1_ALPHA_MODE_PREMULTIPLIED;

                hr = deviceContext->CreateBitmapFromDxgiSurface (surface, bitmapProperties, buffer.resetAndGetPointerAddress());
                jassert (SUCCEEDED (hr));

                if (SUCCEEDED (hr))
                {
                    TRACE_LOG_D2D_RESOURCE (etw::createSwapChainBuffer);

                    state = State::bufferAllocated;
                }
            }

            return hr;
        }

        return S_OK;
    }

    void release()
    {
        swapChainDispatcher.reset();
        buffer         = nullptr;
        chain          = nullptr;
        state          = State::idle;
    }

    bool canPaint()
    {
        return chain != nullptr && buffer != nullptr && state >= State::bufferAllocated;
    }

    HRESULT resize (Rectangle<int> newSize, float dpiScalingFactor, ID2D1DeviceContext* const deviceContext)
    {
        if (chain)
        {
            auto scaledSize = newSize * dpiScalingFactor;
            scaledSize =
                scaledSize.getUnion ({ Direct2DGraphicsContext::minFrameSize, Direct2DGraphicsContext::minFrameSize })
                    .getIntersection ({ Direct2DGraphicsContext::maxFrameSize, Direct2DGraphicsContext::maxFrameSize });

            buffer = nullptr;
            state  = State::chainAllocated;

            auto dpi = USER_DEFAULT_SCREEN_DPI * dpiScalingFactor;
            deviceContext->SetDpi (dpi, dpi);

            auto hr = chain->ResizeBuffers (0, scaledSize.getWidth(), scaledSize.getHeight(), DXGI_FORMAT_B8G8R8A8_UNORM, swapChainFlags);
            if (SUCCEEDED (hr))
            {
                hr = createBuffer (deviceContext);
            }

            if (FAILED (hr))
            {
                release();
            }

            return hr;
        }

        return E_FAIL;
    }

    Rectangle<int> getSize() const
    {
        if (buffer)
        {
            auto size = buffer->GetPixelSize();
            return { (int) size.width, (int) size.height };
        }

        return {};
    }

    DXGI_SWAP_EFFECT const                     swapEffect          = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    UINT const                                 bufferCount         = 2;
    uint32 const                               swapChainFlags      = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    uint32 const                               presentSyncInterval = 1;
    uint32 const                               presentFlags        = 0;
    ComSmartPtr<IDXGISwapChain1>               chain;
    ComSmartPtr<ID2D1Bitmap1>                  buffer;

    std::optional<SwapChainDispatcher> swapChainDispatcher;

    enum class State
    {
        idle,
        chainAllocated,
        bufferAllocated,
        bufferFilled
    };
    State state = State::idle;
};

//==============================================================================
//
// DirectComposition
//
// Using DirectComposition enables transparent windows and smoother window
// resizing
//
// This class builds a simple DirectComposition tree that ultimately contains
// the swap chain
//

#define JUCE_EARLY_EXIT(expr) \
    JUCE_BLOCK_WITH_FORCED_SEMICOLON ( if (const auto hr = (expr); ! SUCCEEDED (hr)) return hr; )

class CompositionTree
{
public:
    HRESULT create (IDXGIDevice* const dxgiDevice, HWND hwnd, IDXGISwapChain1* const swapChain)
    {
        if (compositionDevice != nullptr)
            return S_OK;

        if (dxgiDevice == nullptr)
            return S_FALSE;

        JUCE_EARLY_EXIT (DCompositionCreateDevice (dxgiDevice,
                                                   __uuidof (IDCompositionDevice),
                                                   reinterpret_cast<void**> (compositionDevice.resetAndGetPointerAddress())));
        JUCE_EARLY_EXIT (compositionDevice->CreateTargetForHwnd (hwnd, FALSE, compositionTarget.resetAndGetPointerAddress()));
        JUCE_EARLY_EXIT (compositionDevice->CreateVisual (compositionVisual.resetAndGetPointerAddress()));
        JUCE_EARLY_EXIT (compositionTarget->SetRoot (compositionVisual));
        JUCE_EARLY_EXIT (compositionVisual->SetContent (swapChain));
        JUCE_EARLY_EXIT (compositionDevice->Commit());
        return S_OK;
    }

    void release()
    {
        compositionVisual = nullptr;
        compositionTarget = nullptr;
        compositionDevice = nullptr;
    }

    bool canPaint() const
    {
        return compositionVisual != nullptr;
    }

private:
    ComSmartPtr<IDCompositionDevice> compositionDevice;
    ComSmartPtr<IDCompositionTarget> compositionTarget;
    ComSmartPtr<IDCompositionVisual> compositionVisual;
};

#undef JUCE_EARLY_EXIT

} // namespace direct2d

} // namespace juce
