/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

struct Direct2DHwndContext::HwndPimpl : public Direct2DGraphicsContext::Pimpl
{
private:
    struct SwapChainThread
    {
        SwapChainThread(Direct2DHwndContext::HwndPimpl& ownerIn, HWND hwnd, std::function<void()> callbackIn)
            : owner(ownerIn),
            callback(callbackIn)
        {
            SetWindowSubclass(hwnd, subclassWindowProc, (UINT_PTR)this, (DWORD_PTR)this);
        }

        ~SwapChainThread()
        {
            RemoveWindowSubclass(owner.hwnd, subclassWindowProc, (UINT_PTR)this);

            stopThread();
        }

        void startThread()
        {
            if (thread.has_value())
                return;

            DxgiAdapter::Ptr adapter{};
            if (owner.hwnd)
                adapter = owner.directX->adapters.getAdapterForHwnd(owner.hwnd);

            if (!adapter)
                return;

            commandQueueReadIndex = 0;
            commandQueueWriteIndex = 0;
            for (auto& command : commandQueue)
                command = {};

            //
            // Create a swap chain
            //
            if (auto hr = swap.create(owner.hwnd, owner.getClientRect(), adapter); FAILED(hr))
            {
                return;
            }

            swapEventReceived = true;
            dirtyRectangleCountMask = 0;

            PostMessage(owner.hwnd, swapChainReadyMessageID, 0, 0);

            //
            // Create the thread
            //
            thread = std::thread{ [&] { threadLoop(); } };
        }

        void stopThread()
        {
            if (thread.has_value())
            {
                SetEvent(quitEvent.getHandle());
                thread->join();
                thread = std::nullopt;

                swap = {};
            }

            for (auto& command : commandQueue)
                command = {};
        }

        static LRESULT subclassWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR referenceData)
        {
            auto* that = reinterpret_cast<SwapChainThread*> (referenceData);

            switch (message)
            {
            case swapChainReadyMessageID:
            {
                if (that->callback)
                    that->callback();

                return 0;
            }

            case presentErrorMessageID:
            {
                that->owner.teardown();

                return 0;
            }
            }

            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        void notifyFramePainted()
        {
            jassert(swapEventReceived);

            addCommand([this]() { present(); });
        }

        void setupDirtyRectangles()
        {
            juce::Rectangle<int> swapChainSize;

            swapChainSize = swap.getSize();

            presentParameters.DirtyRectsCount = 0;

            if (!owner.deferredRepaints.containsRectangle(swapChainSize))
            {
                // Allocate enough memory for the array of dirty rectangles
                dirtyRectangles.resize((size_t)owner.deferredRepaints.getNumRectangles());

                // Fill the array of dirty rectangles, intersecting each paint area with the swap chain buffer
                presentParameters.pDirtyRects = dirtyRectangles.data();

                for (const auto& area : owner.deferredRepaints)
                {
                    if (const auto intersection = area.getIntersection(swapChainSize); !intersection.isEmpty())
                        presentParameters.pDirtyRects[presentParameters.DirtyRectsCount++] = D2DUtilities::toRECT(intersection);
                }
            }

            presentParameters.DirtyRectsCount &= dirtyRectangleCountMask;
        }

        bool isReady()
        {
            return swap.canPaint() && thread.has_value() && swapEventReceived;
        }

        bool prepare(Rectangle<int> size, bool sizing)
        {
            if (sizing)
                return false;

            if (size != getSize())
            {
                stopThread();
                createSwapChain(size);
                startThread();
            }

            if (!thread.has_value())
            {
                startThread();
            }

            return true;
        }

        void teardown()
        {
            stopThread();
        }

        void createSwapChain(Rectangle<int> requestedSize)
        {
            auto hr = swap.create(owner.hwnd, requestedSize, owner.directX->adapters.getAdapterForHwnd(owner.hwnd));
            jassert(SUCCEEDED(hr));
            swapChainSize64.store(((uint64_t)requestedSize.getWidth() << 32) | (uint64_t)requestedSize.getHeight());

            dirtyRectangleCountMask = 0;

            uint32_t messageID = SUCCEEDED(hr) ? swapChainReadyMessageID : presentErrorMessageID;
            PostMessage(owner.hwnd, messageID, 0, 0);

            return;

            addCommand([&]
                {
                    HRESULT hr = S_OK;

                    auto requestedSize = getSize();
                    if (requestedSize.isEmpty() || swap.getSize() == requestedSize)
                        return;

                    if (!swap.canPaint())
                    {
                        hr = swap.create(owner.hwnd, requestedSize, owner.directX->adapters.getAdapterForHwnd(owner.hwnd));
                    }

                    hr = swap.resize(requestedSize);
                    jassert(SUCCEEDED(hr));

                    swapChainSize64.store(((uint64_t)requestedSize.getWidth() << 32) | (uint64_t)requestedSize.getHeight());

                    dirtyRectangleCountMask = 0;

                    uint32_t messageID = SUCCEEDED(hr) ? swapChainReadyMessageID : presentErrorMessageID;
                    PostMessage(owner.hwnd, messageID, 0, 0);
                });
        }

        Rectangle<int> getSize() const noexcept
        {
            auto size64 = swapChainSize64.load();
            return { (int)(size64 >> 32), (int)size64 };
        }

        SwapChain swap;
        std::function<void()> swapChainCallback;

        static constexpr uint32_t swapChainReadyMessageID = WM_USER + 124;
        static constexpr uint32_t presentErrorMessageID = WM_USER + 125;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwapChainThread)

    private:
        Direct2DHwndContext::HwndPimpl& owner;
        std::function<void()> callback;
        std::vector<RECT> dirtyRectangles;
        DXGI_PRESENT_PARAMETERS presentParameters{};
        bool swapEventReceived = false;

        static constexpr auto commandQueueSize = 16;
        std::array<std::optional<std::function<void()>>, commandQueueSize> commandQueue;
        int commandQueueWriteIndex = 0;
        int commandQueueReadIndex = 0;
        uint32_t dirtyRectangleCountMask = 0;

        WindowsScopedEvent commandEvent;
        WindowsScopedEvent quitEvent;
        std::optional<std::thread> thread;
        std::atomic<uint64_t> swapChainSize64{};

        void addCommand(std::function<void()> handler)
        {
            commandQueue[commandQueueWriteIndex] = std::move(handler);
            commandQueueWriteIndex = (commandQueueWriteIndex + 1) & (commandQueue.size() - 1);
            SetEvent(commandEvent.getHandle());
        }

        void present()
        {
            JUCE_D2DMETRICS_SCOPED_ELAPSED_TIME(owner.owner.metrics, present1Duration);

            // Present the freshly painted buffer
            swapEventReceived = false;

            auto const hr = swap.getChain()->Present1(swap.presentSyncInterval, swap.presentFlags, &presentParameters);
            jassertquiet(SUCCEEDED(hr));

            if (SUCCEEDED(hr))
            {
                dirtyRectangleCountMask = 0xffffffff;
            }
            else
            {
                PostMessage(owner.hwnd, presentErrorMessageID, 0, 0);

                swap = {};
            }

            JUCE_TRACE_LOG_D2D_PAINT_CALL(etw::direct2dHwndPaintEnd, owner.owner.getFrameId());
        }

        void threadLoop()
        {
            bool running = true;

            Thread::setCurrentThreadName ("JUCE D2D swap chain thread");

            while (running)
            {
                const HANDLE handles[] { swap.getEvent()->getHandle(), commandEvent.getHandle(), quitEvent.getHandle()};

                const auto waitResult = WaitForMultipleObjects ((DWORD) std::size (handles), handles, FALSE, INFINITE);

                switch (waitResult)
                {
                    case WAIT_OBJECT_0:
                    {
                        // Swap chain event fired
                        JUCE_TRACE_LOG_D2D_PAINT_CALL(etw::swapChainThreadEvent, owner.owner.getFrameId());

                        swapEventReceived = true;

                        PostMessage(owner.hwnd, swapChainReadyMessageID, 0, 0);
                        break;
                    }

                    case WAIT_OBJECT_0 + 1:
                    {
                        // Command event fired

                        while (commandQueueReadIndex != commandQueueWriteIndex)
                        {
                            auto& command = commandQueue[commandQueueReadIndex];
                            commandQueueReadIndex = (commandQueueReadIndex + 1) & (commandQueue.size() - 1);

                            if (command.has_value())
                            {
                                (*command)();
                                command = {};
                            }
                        }

                        break;
                    }

                    case WAIT_OBJECT_0 + 2:
                    {
                        // Quit event fired
                        running = false;
                        break;
                    }

                    case WAIT_FAILED:
                    default:
                        running = false;
                        jassertfalse;
                        break;
                }
            }

            swap = {};
        }
    };

    ComSmartPtr<ID2D1DeviceContext1> deviceContext;
    SwapChainThread swapChainThread;

    // Areas that must be repainted during the next paint call, between startFrame/endFrame
    RectangleList<int> deferredRepaints;

    int64 lastFinishFrameTicks = 0;
    HWND hwnd = nullptr;

    bool prepare(bool sizing) override
    {
        const auto adapter = directX->adapters.getAdapterForHwnd (hwnd);

        if (adapter == nullptr)
            return false;

        if (deviceContext == nullptr)
            deviceContext = Direct2DDeviceContext::create (adapter);

        if (deviceContext == nullptr)
            return false;

        if (! deviceResources.has_value())
            deviceResources = Direct2DDeviceResources::create (deviceContext);

        if (! deviceResources.has_value())
            return false;

        auto clientRect = getClientRect();
        if (! hwnd || clientRect.isEmpty())
            return false;

        if (!swapChainThread.isReady())
        {
            if (!swapChainThread.prepare(clientRect, sizing))
                return false;
        }

        return true;
    }

    void teardown() override
    {
        swapChainThread.teardown();
        deviceContext = nullptr;

        Pimpl::teardown();
    }

    RectangleList<int> getPaintAreas() const override
    {
        return deferredRepaints;
    }

    bool checkPaintReady() override
    {
        // Try not to saturate the message thread; this is a little crude. Perhaps some kind of credit system...
        if (auto now = Time::getHighResolutionTicks(); Time::highResolutionTicksToSeconds (now - lastFinishFrameTicks) < 0.001)
            return false;

        bool ready = Pimpl::checkPaintReady();
        ready &= swapChainThread.swap.canPaint();
        ready &= swapChainThread.isReady();

        return ready;
    }

    JUCE_DECLARE_WEAK_REFERENCEABLE (HwndPimpl)

public:
    HwndPimpl (Direct2DHwndContext& ownerIn, HWND hwndIn, std::function<void()> swapChainCallbackIn)
        : Pimpl (ownerIn),
          hwnd (hwndIn),
          swapChainThread(*this, hwndIn, swapChainCallbackIn)
    {
    }

    ~HwndPimpl() override = default;

    void handleShowWindow()
    {
        // One of the trickier problems was determining when Direct2D & DXGI resources can be safely created;
        // that's not really spelled out in the documentation.
        // This method is called when the component peer receives WM_SHOWWINDOW
        prepare(false /* sizing */);
        deferredRepaints = getClientRect();
    }

    Rectangle<int> getClientRect() const
    {
        RECT clientRect;
        GetClientRect (hwnd, &clientRect);

        return Rectangle<int>::leftTopRightBottom (clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
    }

    Rectangle<int> getFrameSize() const override
    {
        return getClientRect();
    }

    ComSmartPtr<ID2D1DeviceContext1> getDeviceContext() const override
    {
        return deviceContext;
    }

    ComSmartPtr<ID2D1Image> getDeviceContextTarget() const override
    {
        return swapChainThread.swap.getBuffer();
    }

    bool setSwapChainBufferSize (Rectangle<int> size)
    {
        if (size == swapChainThread.getSize() || size.isEmpty())
            return true;

        // Require the entire window to be repainted
        deferredRepaints = size;

        InvalidateRect (hwnd, nullptr, TRUE);

        // Resize/scale the swap chain
        prepare(true);

        return true;
    }

    void addDeferredRepaint (Rectangle<int> deferredRepaint)
    {
        deferredRepaints.add (deferredRepaint);

        JUCE_TRACE_EVENT_INT_RECT (etw::repaint, etw::paintKeyword, deferredRepaint);
    }

    SavedState* startFrame (float dpiScale, bool sizing) override
    {
        if (! setSwapChainBufferSize(getClientRect()))
            return nullptr;

        auto* savedState = Pimpl::startFrame (dpiScale, sizing);

        if (savedState == nullptr)
            return nullptr;

        // If a new frame is starting, clear deferredAreas in case repaint is called
        // while the frame is being painted to ensure the new areas are painted on the
        // next frame
        swapChainThread.setupDirtyRectangles();
        deferredRepaints.clear();

        JUCE_TRACE_LOG_D2D_PAINT_CALL (etw::direct2dHwndPaintStart, owner.getFrameId());

        return savedState;
    }

    HRESULT finishFrame() override
    {
        const auto result = Pimpl::finishFrame();

        swapChainThread.notifyFramePainted();

        lastFinishFrameTicks = Time::getHighResolutionTicks();
        return result;
    }

    Image createSnapshot() const
    {
#if 0
        ScopedLock locker{ swapChainLock };

        JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED

        // This won't capture child windows. Perhaps a better approach would be to use
        // IGraphicsCaptureItemInterop, although this is only supported on Windows 10 v1903+

        if (deviceContext == nullptr)
            return {};

        const auto buffer = swap.getBuffer();

        if (buffer == nullptr)
            return {};

        // Create the bitmap to receive the snapshot
        D2D1_BITMAP_PROPERTIES1 bitmapProperties{};
        bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        bitmapProperties.pixelFormat = buffer->GetPixelFormat();

        const auto swapRect = swap.getSize();
        const auto size = D2D1::SizeU ((UINT32) swapRect.getWidth(), (UINT32) swapRect.getHeight());

        ComSmartPtr<ID2D1Bitmap1> snapshot;

        if (const auto hr = deviceContext->CreateBitmap (size, nullptr, 0, bitmapProperties, snapshot.resetAndGetPointerAddress()); FAILED (hr))
            return {};

        swap.getChain()->Present (0, DXGI_PRESENT_DO_NOT_WAIT);

        // Copy the swap chain buffer to the bitmap snapshot
        D2D_POINT_2U p { 0, 0 };
        const auto sourceRect = D2DUtilities::toRECT_U (swapRect);

        if (const auto hr = snapshot->CopyFromBitmap (&p, buffer, &sourceRect); FAILED (hr))
            return {};

        const Image result { new Direct2DPixelData { D2DUtilities::getDeviceForContext (deviceContext), snapshot } };

        swap.getChain()->Present (0, DXGI_PRESENT_DO_NOT_WAIT);

        return result;
#endif

        return {};
    }
};

//==============================================================================
Direct2DHwndContext::Direct2DHwndContext (HWND windowHandle, std::function<void()> swapChainCallbackIn)
{
   #if JUCE_DIRECT2D_METRICS
    metrics = new Direct2DMetrics { Direct2DMetricsHub::getInstance()->lock,
                                    "HWND " + String::toHexString ((pointer_sized_int) windowHandle),
                                    windowHandle };
    Direct2DMetricsHub::getInstance()->add (metrics);
   #endif

    pimpl = std::make_unique<HwndPimpl> (*this, windowHandle, swapChainCallbackIn);
}

Direct2DHwndContext::~Direct2DHwndContext()
{
   #if JUCE_DIRECT2D_METRICS
    Direct2DMetricsHub::getInstance()->remove (metrics);
   #endif
}

Direct2DGraphicsContext::Pimpl* Direct2DHwndContext::getPimpl() const noexcept
{
    return pimpl.get();
}

void Direct2DHwndContext::handleShowWindow()
{
    pimpl->handleShowWindow();
}

void Direct2DHwndContext::addDeferredRepaint (Rectangle<int> deferredRepaint)
{
    pimpl->addDeferredRepaint (deferredRepaint);
}

Image Direct2DHwndContext::createSnapshot() const
{
    return pimpl->createSnapshot();
}

void Direct2DHwndContext::clearTargetBuffer()
{
    applyPendingClipList();
    pimpl->getDeviceContext()->Clear();
}

} // namespace juce
