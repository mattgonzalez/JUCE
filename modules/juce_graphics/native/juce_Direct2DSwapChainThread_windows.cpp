
#ifdef __INTELLISENSE__

#define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
#define JUCE_WINDOWS 1

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
    // Device context
    //

    class DeviceResources
    {
    public:
        DeviceResources()
        {
        }

        ~DeviceResources()
        {
            release();
        }

        HRESULT create (ID2D1Factory1* const direct2dFactory)
        {
            HRESULT hr = S_OK;

            if (direct2dFactory != nullptr)
            {
                if (deviceContext == nullptr)
                {
                    // This flag adds support for surfaces with a different color channel ordering
                    // than the API default. It is required for compatibility with Direct2D.
                    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if JUCE_DEBUG
                    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

                    hr = D3D11CreateDevice (nullptr,
                                            D3D_DRIVER_TYPE_HARDWARE,
                                            nullptr,
                                            creationFlags,
                                            nullptr,
                                            0,
                                            D3D11_SDK_VERSION,
                                            direct3DDevice.resetAndGetPointerAddress(),
                                            nullptr,
                                            nullptr);
                    if (SUCCEEDED (hr))
                    {
                        hr = direct3DDevice->QueryInterface (dxgiDevice.resetAndGetPointerAddress());
                        if (SUCCEEDED (hr))
                        {
                            ComSmartPtr<IDXGIAdapter> dxgiAdapter;
                            hr = dxgiDevice->GetAdapter (dxgiAdapter.resetAndGetPointerAddress());
                            if (SUCCEEDED (hr))
                            {
                                hr = dxgiAdapter->GetParent (__uuidof (dxgiFactory), reinterpret_cast<void**> (dxgiFactory.resetAndGetPointerAddress()));
                                if (SUCCEEDED (hr))
                                {
                                    ComSmartPtr<ID2D1Device> direct2DDevice;
                                    hr = direct2dFactory->CreateDevice (dxgiDevice, direct2DDevice.resetAndGetPointerAddress());
                                    if (SUCCEEDED (hr))
                                    {
                                        hr = direct2DDevice->CreateDeviceContext (D2D1_DEVICE_CONTEXT_OPTIONS_NONE, deviceContext.resetAndGetPointerAddress());
                                        if (SUCCEEDED (hr))
                                        {
                                            deviceContext->SetTextAntialiasMode (D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    jassert (SUCCEEDED (hr));
                }

                if (colourBrush == nullptr && deviceContext != nullptr)
                {
                    hr = deviceContext->CreateSolidColorBrush (D2D1::ColorF::ColorF (0.0f, 0.0f, 0.0f, 1.0f), colourBrush.resetAndGetPointerAddress());
                    jassertquiet (SUCCEEDED (hr));
                }
            }

            return hr;
        }

        void release()
        {
            colourBrush = nullptr;
            deviceContext = nullptr;
            dxgiFactory = nullptr;
            dxgiDevice = nullptr;
            direct3DDevice = nullptr;
        }

        bool canPaint()
        {
            return deviceContext != nullptr && colourBrush != nullptr;
        }

        ComSmartPtr<ID3D11Device> direct3DDevice;
        ComSmartPtr<IDXGIFactory2> dxgiFactory;
        ComSmartPtr<IDXGIDevice> dxgiDevice;
        ComSmartPtr<ID2D1DeviceContext> deviceContext;
        ComSmartPtr<ID2D1SolidColorBrush> colourBrush;
    };

    //==============================================================================
    //
    // Swap chain
    //

    class SwapChain
    {
    public:
        SwapChain()
        {
        }

        ~SwapChain()
        {
            release();
        }

        HRESULT create (HWND hwnd, Rectangle<int> size, ID3D11Device* const direct3DDevice, IDXGIFactory2* const dxgiFactory, bool opaque)
        {
            if (dxgiFactory && direct3DDevice && (! chain || ! chain2) && hwnd)
            {
                HRESULT hr = S_OK;

                buffer = nullptr;
                chain = nullptr;
                chain2 = nullptr;

                //
                // Make the waitable swap chain
                //
                DXGI_SWAP_CHAIN_DESC1 swapChainDescription = {};
                swapChainDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                swapChainDescription.Width = size.getWidth();
                swapChainDescription.Height = size.getHeight();
                swapChainDescription.SampleDesc.Count = 1;
                swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                swapChainDescription.BufferCount = bufferCount;
                swapChainDescription.SwapEffect = swapEffect;
                swapChainDescription.Flags = swapChainFlags;

                if (opaque)
                {
                    swapChainDescription.Scaling = DXGI_SCALING_NONE;
                    swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
                    hr = dxgiFactory->CreateSwapChainForHwnd (direct3DDevice,
                                                              hwnd,
                                                              &swapChainDescription,
                                                              nullptr,
                                                              nullptr,
                                                              chain.resetAndGetPointerAddress());
                }
                else
                {
                    swapChainDescription.Scaling = DXGI_SCALING_STRETCH;
                    swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
                    hr = dxgiFactory->CreateSwapChainForComposition (direct3DDevice,
                                                                     &swapChainDescription,
                                                                     nullptr,
                                                                     chain.resetAndGetPointerAddress());
                }
                jassert (SUCCEEDED (hr));

                if (SUCCEEDED (hr))
                {
                    //
                    // Get the waitable swap chain presentation event and set the maximum frame latency
                    //
                    chain.QueryInterface<IDXGISwapChain2> (chain2);
                    if (chain2)
                    {
                        swapChainEvent = chain2->GetFrameLatencyWaitableObject();
                        if (swapChainEvent == INVALID_HANDLE_VALUE)
                        {
                            return E_NOINTERFACE;
                        }

                        hr = chain2->SetMaximumFrameLatency (2);
                        if (SUCCEEDED (hr))
                        {
                            state = chainAllocated;
                        }
                    }
                }
                else
                {
                    return E_NOINTERFACE;
                }

                return hr;
            }

            return S_OK;
        }

        HRESULT createBuffer (ID2D1DeviceContext* const deviceContext, bool opaque)
        {
            if (deviceContext && chain && ! buffer)
            {
                ComSmartPtr<IDXGISurface> surface;
                auto hr = chain->GetBuffer (0, __uuidof (surface), reinterpret_cast<void**> (surface.resetAndGetPointerAddress()));
                if (SUCCEEDED (hr))
                {
                    D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
                    bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
                    bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    bitmapProperties.pixelFormat.alphaMode = opaque ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED;

                    hr = deviceContext->CreateBitmapFromDxgiSurface (surface, bitmapProperties, buffer.resetAndGetPointerAddress());
                    jassert (SUCCEEDED (hr));

                    if (SUCCEEDED (hr))
                    {
                        state = bufferAllocated;
                    }
                }

                return hr;
            }

            return S_OK;
        }

        void release()
        {
            buffer = nullptr;
            chain = nullptr;
            CloseHandle (swapChainEvent);
            swapChainEvent = nullptr;
            state = idle;
        }

        bool canPaint()
        {
            return chain != nullptr &&
                buffer != nullptr &&
                state >= bufferAllocated;
        }

        HRESULT resize (Rectangle<int> newSize, float dpiScalingFactor, ID2D1DeviceContext* const deviceContext, bool opaque)
        {
            if (chain)
            {
                auto scaledSize = newSize * dpiScalingFactor;
                scaledSize = scaledSize.getUnion ({ Direct2DLowLevelGraphicsContext::minWindowSize, Direct2DLowLevelGraphicsContext::minWindowSize }).getIntersection ({ Direct2DLowLevelGraphicsContext::maxWindowSize, Direct2DLowLevelGraphicsContext::maxWindowSize });

                buffer = nullptr;
                state = chainAllocated;

                auto dpi = 96.0f * dpiScalingFactor;
                deviceContext->SetDpi (dpi, dpi);

                auto hr = chain->ResizeBuffers (0, scaledSize.getWidth(), scaledSize.getHeight(), DXGI_FORMAT_B8G8R8A8_UNORM, swapChainFlags);
                if (SUCCEEDED (hr))
                {
                    hr = createBuffer (deviceContext, opaque);
                }

                return hr;
            }

            return E_FAIL;
        }

        juce::Rectangle<int> getSize() const
        {
            if (buffer)
            {
                auto size = buffer->GetPixelSize();
                return { (int)size.width, (int)size.height };
            }

            return {};
        }

        DXGI_SWAP_EFFECT const swapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        UINT const bufferCount = 2;
        uint32 const swapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        uint32 const presentSyncInterval = 1;
        uint32 const presentFlags = 0;
        ComSmartPtr<IDXGISwapChain1> chain;
        ComSmartPtr<IDXGISwapChain2> chain2;
        ComSmartPtr<ID2D1Bitmap1> buffer;
        HANDLE swapChainEvent = nullptr;
        enum State
        {
            idle,
            chainAllocated,
            bufferAllocated,
            bufferFilled
        } state = idle;
    };

    //==============================================================================
    //
    // DirectComposition
    //

    class CompositionTree
    {
    public:
        HRESULT create (IDXGIDevice* const dxgiDevice, HWND hwnd, IDXGISwapChain1* const swapChain, bool opaqueFlag)
        {
            HRESULT hr = S_OK;

            if (opaqueFlag)
            {
                return S_OK;
            }

            if (dxgiDevice && ! compositionDevice)
            {
                hr = DCompositionCreateDevice (dxgiDevice, __uuidof (IDCompositionDevice), reinterpret_cast<void**> (compositionDevice.resetAndGetPointerAddress()));
                if (SUCCEEDED (hr))
                {
                    hr = compositionDevice->CreateTargetForHwnd (hwnd, FALSE, compositionTarget.resetAndGetPointerAddress());
                    if (SUCCEEDED (hr))
                    {
                        hr = compositionDevice->CreateVisual (compositionVisual.resetAndGetPointerAddress());
                        if (SUCCEEDED (hr))
                        {
                            hr = compositionTarget->SetRoot (compositionVisual);
                            if (SUCCEEDED (hr))
                            {
                                hr = compositionVisual->SetContent (swapChain);
                                if (SUCCEEDED (hr))
                                {
                                    hr = compositionDevice->Commit();
                                }
                            }
                        }
                    }
                }
            }

            return hr;
        }

        void release()
        {
            compositionVisual = nullptr;
            compositionTarget = nullptr;
            compositionDevice = nullptr;
        }

        bool canPaint()
        {
            return compositionVisual != nullptr;
        }

    private:
        ComSmartPtr<IDCompositionDevice> compositionDevice;
        ComSmartPtr<IDCompositionTarget> compositionTarget;
        ComSmartPtr<IDCompositionVisual> compositionVisual;
    };

    //==============================================================================
    //
    // SwapChainReadyMessage
    //

    struct SwapChainListener
    {
        virtual ~SwapChainListener() = default;

        virtual void swapChainSignaledReady() = 0;

        JUCE_DECLARE_WEAK_REFERENCEABLE (SwapChainListener)
    };

    struct SwapChainReadyMessage : public CallbackMessage
    {
        SwapChainReadyMessage (SwapChainListener* swapChainListener_, int64 swapChainReadyTicks_)
            : swapChainListener (swapChainListener_),
            swapChainReadyTicks (swapChainReadyTicks_)
        {
        }
        ~SwapChainReadyMessage() override = default;

        void messageCallback() override
        {
            TRACE_LOG_SWAP_CHAIN_MESSAGE;

            if (swapChainListener)
            {
                swapChainListener->swapChainSignaledReady ();
            }
        }

        static void createAndPost (SwapChainListener* swapChainListener_, int64 swapChainReadyTicks_)
        {
            (new SwapChainReadyMessage { swapChainListener_, swapChainReadyTicks_ })->post();
        }

        WeakReference<SwapChainListener> swapChainListener;
        int64 messageCreationTicks = Time::getHighResolutionTicks();
        int64 swapChainReadyTicks;
    };

    //==============================================================================
    //
    // SwapChainReadyThread
    //

    class SwapChainReadyThread : protected Thread
    {
    private:
        SwapChainListener* const swapChainListener;
        HANDLE events[2] = {};

        enum
        {
            shutdownEvent,
            swapChainEvent
        };

    public:
        SwapChainReadyThread (direct2d::SwapChainListener* swapChainListener_) 
            : Thread ("Direct2DPresentationThread"),
            swapChainListener (swapChainListener_)
        {
            events[shutdownEvent] = CreateEvent (nullptr, FALSE, FALSE, nullptr);
        }

        ~SwapChainReadyThread() override
        {
            stop();
            CloseHandle(events[shutdownEvent]);
        }

        void start (HANDLE swapChainEventHandle_)
        {
            events[swapChainEvent] = swapChainEventHandle_;
            startThread (Thread::Priority::highest);
        }

        void stop()
        {
            jassert (MessageManager::getInstance()->isThisTheMessageThread());

            SetEvent(events[shutdownEvent]);
            stopThread (100000);
        }

        void run() override
        {
            SetThreadDescription (GetCurrentThread(), L"SwapChainReadyThread");

            while (!threadShouldExit())
            {
                auto waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                switch (waitResult)
                {
                case WAIT_OBJECT_0 + shutdownEvent:
                {
                    return;
                }

                case WAIT_OBJECT_0 + swapChainEvent:
                {
                    TRACE_LOG_SWAP_CHAIN_EVENT;

                    eventSignaled.store(true);

                    SwapChainReadyMessage::createAndPost(swapChainListener, Time::getHighResolutionTicks());
                    break;
                }

                default:
                {
                    jassertfalse;
                    break;
                }
                }
            }
        }

        std::atomic<bool> eventSignaled;
    };

} // namespace direct2d

} // namespace juce
