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
        float                     snappedDpiScalingFactor = 1.0f;
        static constexpr int      dpiScalingIntConversionShift = 7;
        static constexpr int      dpiScalingIntConversionFactor = 1 << dpiScalingIntConversionShift;
        int                       repaintAreaPixelSnap = dpiScalingIntConversionFactor;
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
            if (!deviceResources.canPaint())
            {
                if (auto hr = deviceResources.create(adapter, snappedDpiScalingFactor); FAILED(hr))
                {
                    return hr;
                }
            }

            if (!hwnd || frameSize.isEmpty())
            {
                return E_FAIL;
            }

            if (!swap.canPaint())
            {
                if (auto hr = swap.create(hwnd, frameSize, adapter); FAILED(hr))
                {
                    return hr;
                }

                if (auto hr = swap.createBuffer(deviceResources.deviceContext.context); FAILED(hr))
                {
                    return hr;
                }
            }

            if (!compositionTree.canPaint())
            {
                if (auto hr = compositionTree.create(adapter->dxgiDevice, hwnd, swap.chain); FAILED(hr))
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

        void adjustPaintAreas(RectangleList<int>& paintAreas) override
        {
            //
            // Does the entire buffer need to be filled?
            //
            if (swap.state == direct2d::SwapChain::State::bufferAllocated)
            {
                deferredRepaints = swap.getSize();
            }

            //
            // If the window alpha is less than 1.0, clip to the union of the
            // deferred repaints so the device context Clear() works correctly
            //
            if (targetAlpha < 1.0f || !opaque)
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
            swapChainReady |= swap.swapChainDispatcher->isSwapChainReady();

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

        JUCE_DECLARE_WEAK_REFERENCEABLE(HwndPimpl)

    public:
        HwndPimpl(Direct2DHwndContext& owner_, HWND hwnd_, bool opaque_)
            : Pimpl(owner_, opaque_),
            hwnd(hwnd_)
        {
            adapter = factories->getAdapterForHwnd(hwnd_);
        }

        ~HwndPimpl() override = default;

        void handleTargetVisible()
        {
            //
            // One of the trickier problems was determining when Direct2D & DXGI resources can be safely created;
            // that's not really spelled out in the documentation.
            //
            // This method is called when the component peer receives WM_SHOWWINDOW
            //
            prepare();

            frameSize = getClientRect();
            deferredRepaints = frameSize;
        }

        Rectangle<int> getClientRect() const
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            return Rectangle<int>::leftTopRightBottom(clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
        }

        Rectangle<int> getFrameSize() override
        {
            return swap.getSize();
        }

        ID2D1Image* getDeviceContextTarget() override
        {
            return swap.buffer;
        }

        void setSize(Rectangle<int> size)
        {
            if (size == frameSize)
            {
                return;
            }

            resizeSwapChain(size);
        }

        void resizeSwapChain(Rectangle<int> size)
        {
            if (size.isEmpty())
            {
                return;
            }

            //
            // Require the entire window to be repainted
            //
            frameSize = size;
            deferredRepaints = size;
            InvalidateRect(hwnd, nullptr, TRUE);

            //
            // Resize/scale the swap chain
            //
            prepare();

            if (auto deviceContext = deviceResources.deviceContext.context)
            {
                auto hr = swap.resize(size, (float)snappedDpiScalingFactor, deviceContext);
                jassert(SUCCEEDED(hr));
                if (FAILED(hr))
                {
                    teardown();
                }
            }
        }

        void addDeferredRepaint(Rectangle<int> deferredRepaint)
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
            deferredRepaints.add(Rectangle<int>::leftTopRightBottom(deferredRepaint.getX() & snapMask,
                deferredRepaint.getY() & snapMask,
                (deferredRepaint.getRight() + repaintAreaPixelSnap - 1) & snapMask,
                (deferredRepaint.getBottom() + repaintAreaPixelSnap - 1) & snapMask));
        }

        void addInvalidWindowRegionToDeferredRepaints()
        {
            updateRegion.getRECTAndValidate(hwnd);
            updateRegion.addToRectangleList(deferredRepaints);
            updateRegion.clear();
        }

        void clearBackground() override
        {
            if (! opaque && swap.state == direct2d::SwapChain::bufferAllocated)
            {
                deviceResources.deviceContext.createHwndRenderTarget(hwnd);

                auto& hwndRenderTarget = deviceResources.deviceContext.hwndRenderTarget;
                if (hwndRenderTarget)
                {
                    D2D1_COLOR_F colorF = direct2d::colourToD2D(getBackgroundTransparencyKeyColour());

                    RECT clientRect;
                    GetClientRect(hwnd, &clientRect);

                    D2D1_SIZE_U size{ (uint32)(clientRect.right - clientRect.left), (uint32)(clientRect.bottom - clientRect.top) };
                    hwndRenderTarget->Resize(size);
                    hwndRenderTarget->BeginDraw();
                    hwndRenderTarget->Clear(colorF);
                    hwndRenderTarget->EndDraw();
                }
            }
        }

        HRESULT finishFrame() override
        {
            if (auto hr = Pimpl::finishFrame(); FAILED(hr))
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
            direct2d::ScopedElapsedTime set{ owner.stats, direct2d::PaintStats::present1Duration };
#endif

            //
            // Allocate enough memory for the array of dirty rectangles
            //
            if (dirtyRectanglesCapacity < deferredRepaints.getNumRectangles())
            {
                dirtyRectangles.realloc(deferredRepaints.getNumRectangles());
                dirtyRectanglesCapacity = deferredRepaints.getNumRectangles();
            }

            //
            // Fill the array of dirty rectangles, intersecting deferredRepaints with the swap chain buffer
            //
            DXGI_PRESENT_PARAMETERS presentParameters{};
            if (swap.state == direct2d::SwapChain::State::bufferFilled)
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
            auto hr = swap.chain->Present1(swap.presentSyncInterval, swap.presentFlags, &presentParameters);
            jassert(SUCCEEDED(hr));

            //
            // The buffer is now completely filled and ready for dirty rectangles
            //
            swap.state = direct2d::SwapChain::State::bufferFilled;

            deferredRepaints.clear();
            swapChainReady = false;

            if (FAILED(hr))
            {
                teardown();
            }

            return hr;
        }

        void setScaleFactor(float scale_) override
        {
            Pimpl::setScaleFactor(scale_);

            //
            // Round DPI scaling factor to nearest 1/128 so the repainted areas
            // and the dirty rectangles both snap to pixel boundaries
            //
            snappedDpiScalingFactor = roundToInt(scale_ * dpiScalingIntConversionFactor) / float{ dpiScalingIntConversionFactor };

            //
            // Typical Windows DPI scaling is in steps of 25%, so the repaint area needs to be expanded and snapped to the nearest multiple of 4.
            //
            // Windows lets the user enter scaling in steps of 1%, which would require expanding to the nearest multiple of 100.
            // This method finds the least common denominator as a power of 2 up to a snap of 128 pixels.
            //
            // For example: DPI scaling 150%
            //      greatestCommonDenominator = gdc( 1.5 * 128, 128) = 64
            //      repaintAreaPixel = 128 / 64 = 2
            //      deferredRepaints will be expanded to snap to the next multiple of 2
            //
            // DPI scaling of 225%
            //      greatestCommonDenominator = gdc( 2.25 * 128, 128) = 32
            //      repaintAreaPixel = 128 / 32 = 4
            //      deferredRepaints will be expanded to snap to the next multiple of 4
            //
            // DPI scaling of 301%
            //      greatestCommonDenominator = gdc( 3.01 * 128, 128) = 1
            //      repaintAreaPixel = 128 / 1 = 128
            //      deferredRepaints will be expanded to snap to the next multiple of 128
            //
            // For the typical scaling factors, the deferred repaint area will be only slightly expanded to the nearest multiple of 4. The more offbeat
            // scaling factors will be less efficient and require more painting.
            //
            auto greatestCommonDenominator =
                std::gcd(roundToInt(float{ dpiScalingIntConversionFactor } *snappedDpiScalingFactor), dpiScalingIntConversionFactor);
            repaintAreaPixelSnap = dpiScalingIntConversionFactor / greatestCommonDenominator;

            //
            // Resize the swap chain buffer
            //
            resizeSwapChain(getClientRect());

            //
            // Repaint the entire window
            //
            deferredRepaints = frameSize;
        }

        double getScaleFactor() const
        {
            return dpiScalingFactor;
        }

        SavedState* getCurrentSavedState() const
        {
            return savedClientStates.size() > 0 ? savedClientStates.top().get() : nullptr;
        }

        SavedState* pushFirstSavedState(Rectangle<int> initialClipRegion)
        {
            jassert(savedClientStates.size() == 0);

            savedClientStates.push(
                std::make_unique<SavedState>(initialClipRegion, deviceResources.colourBrush, deviceResources.deviceContext));

            return getCurrentSavedState();
        }

        SavedState* pushSavedState()
        {
            jassert(savedClientStates.size() > 0);

            savedClientStates.push(std::make_unique<SavedState>(savedClientStates.top().get()));

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

        inline ID2D1DeviceContext1* getDeviceContext() const noexcept
        {
            return deviceResources.deviceContext.context;
        }

        void setDeviceContextTransform(AffineTransform transform)
        {
            deviceResources.deviceContext.setTransform(transform);
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

        Image createSnapshot([[maybe_unused]] direct2d::DPIScalableArea<int> scalableArea)
        {
#if JUCE_DIRECT2D_SNAPSHOT
            scalableArea.clipToPhysicalArea(frameSize);

            if (scalableArea.isEmpty() ||
                deviceResources.deviceContext.context == nullptr ||
                swap.buffer == nullptr)
            {
                return {};
            }

            //
            // Create the bitmap to receive the snapshot
            //
            D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
            bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
            bitmapProperties.dpiX = USER_DEFAULT_SCREEN_DPI * snappedDpiScalingFactor;
            bitmapProperties.dpiY = bitmapProperties.dpiX;
            bitmapProperties.pixelFormat = swap.buffer->GetPixelFormat();
            
            auto size = direct2d::getPhysicalD2DSizeU(scalableArea);

            ComSmartPtr<ID2D1Bitmap1> snapshot;
            auto hr = deviceResources.deviceContext.context->CreateBitmap(size, nullptr, 0, bitmapProperties, snapshot.resetAndGetPointerAddress());
            if (SUCCEEDED(hr))
            {
                //
                // Copy the swap chain buffer to the bitmap snapshot
                //
                D2D_POINT_2U p{ 0, 0 };
                D2D_RECT_U  sourceRect
                {
                    (uint32)scalableArea.getPhysicalArea().getX(),
                    (uint32)scalableArea.getPhysicalArea().getY(),
                    (uint32)scalableArea.getPhysicalArea().getRight(),
                    (uint32)scalableArea.getPhysicalArea().getBottom()
                };
                if (hr = snapshot->CopyFromBitmap(&p, swap.buffer, &sourceRect); SUCCEEDED(hr))
                {
                    auto pixelData = Direct2DPixelData::fromDirect2DBitmap(snapshot, scalableArea.withZeroOrigin());
                    return Image{ pixelData };
                }
            }
#endif

            return Image{};
        }

        HWND hwnd = nullptr;

#if JUCE_DIRECT2D_METRICS
        int64 paintStartTicks = 0;
        int64 paintEndTicks = 0;
#endif
    };

    //==============================================================================
    Direct2DHwndContext::Direct2DHwndContext(HWND hwnd_, float dpiScalingFactor_, bool opaque)
    {
        pimpl = std::make_unique<HwndPimpl>(*this, hwnd_, opaque);

        getPimpl()->setScaleFactor(dpiScalingFactor_);
        updateSize();
    }

    Direct2DHwndContext::~Direct2DHwndContext() {}

    HWND Direct2DHwndContext::getHwnd() const noexcept
    {
        return pimpl->hwnd;
    }

    Direct2DGraphicsContext::Pimpl* Direct2DHwndContext::getPimpl() const noexcept
    {
        return pimpl.get();
    }

    void Direct2DHwndContext::handleShowWindow()
    {
        pimpl->handleTargetVisible();
    }

    void Direct2DHwndContext::setWindowAlpha(float alpha)
    {
        pimpl->setTargetAlpha(alpha);
    }

    void Direct2DHwndContext::setSize(int width, int height)
    {
        pimpl->setSize({ width, height });
    }

    void Direct2DHwndContext::updateSize()
    {
        pimpl->setSize(pimpl->getClientRect());
    }

    void Direct2DHwndContext::addDeferredRepaint(Rectangle<int> deferredRepaint)
    {
        pimpl->addDeferredRepaint(deferredRepaint);
    }

    void Direct2DHwndContext::addInvalidWindowRegionToDeferredRepaints()
    {
        pimpl->addInvalidWindowRegionToDeferredRepaints();
    }

    Image Direct2DHwndContext::createSnapshot(Rectangle<int> deviceIndependentArea)
    {
        return pimpl->createSnapshot(direct2d::DPIScalableArea<int>::fromDeviceIndependentArea(deviceIndependentArea, (float)pimpl->getScaleFactor()));
    }

    Image Direct2DHwndContext::createSnapshot()
    {
        return pimpl->createSnapshot(direct2d::DPIScalableArea<int>::fromPhysicalArea(pimpl->getClientRect(), (float)pimpl->getScaleFactor()));
    }

    void Direct2DHwndContext::clearTargetBuffer()
    {
        //
        // For opaque windows, clear the background to black with the window alpha
        // For non-opaque windows, clear the background to transparent black
        //
        // In either case, add a transparency layer if the window alpha is less than 1.0
        //
        pimpl->getDeviceContext()->Clear(pimpl->backgroundColor);
        if (pimpl->targetAlpha < 1.0f)
        {
            beginTransparencyLayer(pimpl->targetAlpha);
        }
    }

} // namespace juce
