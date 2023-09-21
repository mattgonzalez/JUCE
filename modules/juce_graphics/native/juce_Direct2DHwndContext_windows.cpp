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
    #include "juce_ETW_windows.h"
    #include "jue_Direct2DGraphicsContext_windows.h"
    #include "jue_Direct2DGraphicsContext_windows.cpp"

#endif

namespace juce
{

//==============================================================================

struct Direct2DHwndContext::HwndPimpl : public Direct2DGraphicsContext::Pimpl
{
private:
    direct2d::SwapChain       swap;
    direct2d::CompositionTree compositionTree;
    direct2d::UpdateRegion    updateRegion;
    bool                      swapChainReady = false;
    RectangleList<int>        deferredRepaints;
    Rectangle<int>            frameSize;
    int                       dirtyRectanglesCapacity = 0;
    HeapBlock<RECT>           dirtyRectangles;

    HRESULT prepare() override
    {
        if (auto hr = Pimpl::prepare(); FAILED (hr))
        {
            return hr;
        }

        if (! hwnd || frameSize.isEmpty())
        {
            return E_FAIL;
        }

        if (! swap.canPaint())
        {
            if (auto hr = swap.create (hwnd, frameSize, adapter); FAILED (hr))
            {
                return hr;
            }

            if (auto hr = swap.createBuffer (deviceResources.deviceContext.context); FAILED (hr))
            {
                return hr;
            }
        }

        if (! compositionTree.canPaint())
        {
            if (auto hr = compositionTree.create (adapter->dxgiDevice, hwnd, swap.chain); FAILED (hr))
            {
                return hr;
            }
        }

        return S_OK;
    }

    void teardown() override
    {
        compositionTree.release();
        swap.release();

        Pimpl::teardown();
    }

    void adjustPaintAreas (RectangleList<int>& paintAreas) override
    {
        //
        // Does the entire buffer need to be filled?
        //
        if (swap.state == direct2d::SwapChain::bufferAllocated)
        {
            deferredRepaints = swap.getSize();
        }

        //
        // If the window alpha is less than 1.0, clip to the union of the
        // deferred repaints so the device context Clear() works correctly
        //
        if (targetAlpha < 1.0f || ! opaque)
        {
            paintAreas = deferredRepaints.getBounds();
        }
        else
        {
            paintAreas = deferredRepaints;
        }
    }

    bool checkPaintReady() override
    {
        swapChainReady |= swap.swapChainDispatcher->isSwapChainReady (swap.dispatcherBitNumber);

        //
        // Paint if:
        //      resources are allocated
        //      deferredRepaints has areas to be painted
        //      the swap chain is ready
        //
        bool ready = Pimpl::checkPaintReady();
        ready &= swap.canPaint();
        ready &= compositionTree.canPaint();
        ready &= deferredRepaints.getNumRectangles() > 0;
        ready &= swapChainReady;
        return ready;
    }

    JUCE_DECLARE_WEAK_REFERENCEABLE (HwndPimpl)

public:
    HwndPimpl (Direct2DHwndContext& owner_, HWND hwnd_, double dpiScalingFactor_, bool opaque_)
        : Pimpl (owner_, dpiScalingFactor_, opaque_),
          hwnd (hwnd_)
    {
        adapter = factories->getAdapterForHwnd(hwnd_);
    }

    ~HwndPimpl() override {}

    void handleTargetVisible()
    {
        //
        // One of the trickier problems was determining when Direct2D & DXGI resources can be safely created;
        // that's not really spelled out in the documentation.
        //
        // This method is called when the component peer receives WM_SHOWWINDOW
        //
        prepare();

        frameSize        = getClientRect();
        deferredRepaints = frameSize;
    }

    Rectangle<int> getClientRect() const
    {
        RECT clientRect;
        GetClientRect (hwnd, &clientRect);

        return Rectangle<int>::leftTopRightBottom (clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
    }

    Rectangle<int> getFrameSize() override
    {
        return swap.getSize();
    }

    ID2D1Image* const getDeviceContextTarget()
    {
        return swap.buffer;
    }

    void resize (Rectangle<int> size)
    {
        if ((frameSize.isEmpty() && size.getWidth() <= 1 && size.getHeight() <= 1) || (size == frameSize))
        {
            return;
        }

        //
        // Require the entire window to be repainted
        //
        frameSize        = size;
        deferredRepaints = size;

        prepare();

        if (auto deviceContext = deviceResources.deviceContext.context)
        {
            auto hr = swap.resize (size, (float) snappedDpiScalingFactor, deviceContext);
            jassert (SUCCEEDED (hr));
            if (FAILED (hr))
            {
                teardown();
            }
        }
    }

    void resize()
    {
        resize (getClientRect());
    }

    void restoreWindow()
    {
        resize (frameSize);
    }

    void addDeferredRepaint (Rectangle<int> deferredRepaint)
    {
        //
        // Clipping regions are specified with floating-point values and can be anti-aliased with
        // sub-pixel boundaries, especially with high DPI.
        //
        // The swap chain dirty rectangles are specified with integer values.
        //
        // To keep the clipping regions and the dirty rectangles lined up, find the lowest
        // common denominator and expand the clipping region slightly so that both the
        // clipping region and the dirty rectangle will sit on pixel boundaries.
        //
        auto snapMask = ~(repaintAreaPixelSnap - 1);
        deferredRepaints.add (Rectangle<int>::leftTopRightBottom (deferredRepaint.getX() & snapMask,
                                                                  deferredRepaint.getY() & snapMask,
                                                                  (deferredRepaint.getRight() + repaintAreaPixelSnap - 1) & snapMask,
                                                                  (deferredRepaint.getBottom() + repaintAreaPixelSnap - 1) & snapMask));
    }

    void addInvalidWindowRegionToDeferredRepaints()
    {
        updateRegion.getRECTAndValidate (hwnd);
        updateRegion.addToRectangleList (deferredRepaints);
        updateRegion.clear();
    }

    HRESULT finishFrame() override
    {
        if (auto hr = Pimpl::finishFrame(); FAILED (hr))
        {
            return hr;
        }

        //
        // Fill out the array of dirty rectangles
        //
        // Compare deferredRepaints to the swap chain buffer area. If the rectangles in deferredRepaints are contained
        // by the swap chain buffer area, then mark those rectangles as dirty. DXGI will only keep the dirty rectangles from the
        // current buffer and copy the clean area from the previous buffer.
        //
        // The buffer needs to be completely filled before using dirty rectangles. The dirty rectangles need to be contained
        // within the swap chain buffer.
        //
#if JUCE_DIRECT2D_METRICS
        direct2d::ScopedElapsedTime set { owner.stats, direct2d::PaintStats::present1Duration };
#endif

        //
        // Allocate enough memory for the array of dirty rectangles
        //
        if (dirtyRectanglesCapacity < deferredRepaints.getNumRectangles())
        {
            dirtyRectangles.realloc (deferredRepaints.getNumRectangles());
            dirtyRectanglesCapacity = deferredRepaints.getNumRectangles();
        }

        //
        // Fill the array of dirty rectangles, intersecting deferredRepaints with the swap chain buffer
        //
        DXGI_PRESENT_PARAMETERS presentParameters {};
        if (swap.state == direct2d::SwapChain::bufferFilled)
        {
            RECT* dirtyRectangle = dirtyRectangles.getData();
            auto const swapChainSize = swap.getSize();
            for (auto const& area : deferredRepaints)
            {
                //
                // If this deferred area contains the entire swap chain, then
                // no need for dirty rectangles
                //
                if (area.contains(swapChainSize))
                {
                    presentParameters.DirtyRectsCount = 0;
                    break;
                }

                //
                // Intersect this deferred repaint area with the swap chain buffer
                //
                auto intersection = (area * snappedDpiScalingFactor).getSmallestIntegerContainer().getIntersection(swapChainSize);
                if (intersection.isEmpty())
                {
                    //
                    // Can't clip to an empty rectangle
                    //
                    continue;
                }

                //
                // Add this deferred repaint area to the dirty rectangle array (scaled for DPI)
                //
                *dirtyRectangle = direct2d::rectangleToRECT(intersection);

                dirtyRectangle++;
                presentParameters.DirtyRectsCount++;
            }
            presentParameters.pDirtyRects = dirtyRectangles.getData();
        }

        //
        // Present the freshly painted buffer
        //
        auto hr = swap.chain->Present1 (swap.presentSyncInterval, swap.presentFlags, &presentParameters);
        jassert (SUCCEEDED (hr));

        //
        // The buffer is now completely filled and ready for dirty rectangles
        //
        swap.state = direct2d::SwapChain::bufferFilled;

        deferredRepaints.clear();
        swapChainReady = false;

        if (FAILED (hr))
        {
            teardown();
        }

        return hr;
    }

    void setScaleFactor (double scale_) override
    {
        Pimpl::setScaleFactor (scale_);

        deferredRepaints = frameSize;
        resize();
    }

    double getScaleFactor() const
    {
        return dpiScalingFactor;
    }

    SavedState* getCurrentSavedState() const
    {
        return savedClientStates.size() > 0 ? savedClientStates.top().get() : nullptr;
    }

    SavedState* pushFirstSavedState (Rectangle<int> initialClipRegion)
    {
        jassert (savedClientStates.size() == 0);

        savedClientStates.push (
            std::make_unique<SavedState> (initialClipRegion, deviceResources.colourBrush, deviceResources.deviceContext));

        return getCurrentSavedState();
    }

    SavedState* pushSavedState()
    {
        jassert (savedClientStates.size() > 0);

        savedClientStates.push (std::make_unique<SavedState> (savedClientStates.top().get()));

        return getCurrentSavedState();
    }

    SavedState* popSavedState()
    {
        savedClientStates.top()->popLayers();
        savedClientStates.pop();

        return getCurrentSavedState();
    }

    void popAllSavedStates()
    {
        while (savedClientStates.size() > 0)
        {
            popSavedState();
        }
    }

    inline ID2D1DeviceContext1* const getDeviceContext() const noexcept
    {
        return deviceResources.deviceContext.context;
    }

    void setDeviceContextTransform (AffineTransform transform)
    {
        deviceResources.deviceContext.setTransform (transform);
    }

    auto getDirect2DFactory()
    {
        return factories->getDirect2DFactory();
    }

    auto getDirectWriteFactory()
    {
        return factories->getDirectWriteFactory();
    }

    auto getSystemFonts()
    {
        return factories->getSystemFonts();
    }

    HWND hwnd   = nullptr;

#if JUCE_DIRECT2D_METRICS
    int64 paintStartTicks = 0;
    int64 paintEndTicks   = 0;
#endif
};

//==============================================================================
Direct2DHwndContext::Direct2DHwndContext (HWND hwnd_, double dpiScalingFactor_, bool opaque)
{
    pimpl = std::make_unique<HwndPimpl> (*this, hwnd_, dpiScalingFactor_, opaque);
}

Direct2DHwndContext::~Direct2DHwndContext() {}

Direct2DGraphicsContext::Pimpl* const Direct2DHwndContext::getPimpl() const noexcept
{
    return pimpl.get();
}

void Direct2DHwndContext::handleShowWindow()
{
    pimpl->handleTargetVisible();
}

void Direct2DHwndContext::setWindowAlpha (float alpha)
{
    pimpl->setTargetAlpha (alpha);
}

void Direct2DHwndContext::resize()
{
    pimpl->resize();
}

void Direct2DHwndContext::resize (int width, int height)
{
    pimpl->resize ({ width, height });
}

void Direct2DHwndContext::restoreWindow()
{
    pimpl->restoreWindow();
}

void Direct2DHwndContext::addDeferredRepaint (Rectangle<int> deferredRepaint)
{
    pimpl->addDeferredRepaint (deferredRepaint);
}

void Direct2DHwndContext::addInvalidWindowRegionToDeferredRepaints()
{
    pimpl->addInvalidWindowRegionToDeferredRepaints();
}

void Direct2DHwndContext::clearTargetBuffer()
{
    //
    // For opaque windows, clear the background to black with the window alpha
    // For non-opaque windows, clear the background to transparent black
    //
    // In either case, add a transparency layer if the window alpha is less than 1.0
    //
    pimpl->getDeviceContext()->Clear (pimpl->backgroundColor);
    if (pimpl->targetAlpha < 1.0f)
    {
        beginTransparencyLayer (pimpl->targetAlpha);
    }
}

} // namespace juce
