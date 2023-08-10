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

/*
    JUCE 7.0.6 merge
            WASAPI loopback


    get rid of CS_OWNDC?

    -child window clipping?

    -JUCE_DIRECT2D_METRICS fix build /     -conditional frame stats, frame history
    
    -optimize save/restore state?


    -minimize calls to SetTransform
    -text analyzer?
    -ID2D1DrawingStateBlock?
    -recycle state structs
    -use std::stack for layers
    -don't paint occluded windows
    -Multithreaded device context
    -reusable geometry for exclude clip rectangle

    SetThreadDescription(
                GetCurrentThread(),
                L"Direct2DPresentation"
            );

    when to start threads in general

    handle device context creation error / paint errors     -restart render thread on error?
        watchdog timer?

    OK-Check use of InvalidateRect & ValidateRect
    OK-drawGlyphUnderline
    OK-DPI scaling
    OK start/stop thread when window is visible
    OK -logo highlights in juce animation demo
    OK -check resize when auto-arranging windows
    OK -single-channel bitmap for clip to image alpha
    OK -transparency layer in software mode?
    OK -check for empty dirty rectangles
    OK vblank in software mode
    OK -fix ScopedBrushTransformInverter
    OK -vblank attachment
    OK -Always present

    */

#ifdef __INTELLISENSE__

#define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
#define JUCE_WINDOWS 1

#include <d2d1_2.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dwrite.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <windows.h>
#include "juce_ETW_windows.h"

#endif

#ifndef DIRECT2D_CHILD_WINDOW
#define DIRECT2D_CHILD_WINDOW 1
#endif

#include "juce_Direct2DHelpers_windows.cpp"
#include "juce_Direct2DSwapChainThread_windows.cpp"
#include "juce_Direct2DChildWindow_windows.cpp"

namespace juce
{
//==============================================================================

struct Direct2DLowLevelGraphicsContext::ClientSavedState
{
private:
    //
    // Layer struct to keep track of pushed Direct2D layers.
    //
    // Most layers need to be popped by calling PopLayer, unless it's an axis aligned clip layer
    //
    struct PushedLayer
    {
        PushedLayer() = default;
        PushedLayer (PushedLayer&&) = default;

        void pop (ID2D1DeviceContext* deviceContext)
        {
            deviceContext->PopLayer();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PushedLayer)
    };

    struct PushedAxisAlignedClipLayer
    {
        PushedAxisAlignedClipLayer() = default;
        PushedAxisAlignedClipLayer (PushedAxisAlignedClipLayer&&) = default;

        void pop (ID2D1DeviceContext* deviceContext)
        {
            deviceContext->PopAxisAlignedClip();
        }
    };

    struct LayerPopper
    {
        void operator() (PushedLayer& layer)
        {
            layer.pop (deviceContext);
        }

        void operator() (PushedAxisAlignedClipLayer& layer)
        {
            layer.pop (deviceContext);
        }

        ID2D1DeviceContext* deviceContext;
    };

    using PushedLayerVariant = std::variant<PushedLayer, PushedAxisAlignedClipLayer>;
    std::stack<PushedLayerVariant> pushedLayers;

public:
    //
    // Constructor for first stack entry
    //
    ClientSavedState (Rectangle<int> swapChainBufferBounds_, ComSmartPtr<ID2D1SolidColorBrush>& colourBrush_)
        :   clipRegion (swapChainBufferBounds_),
           colourBrush(colourBrush_)
    {
        currentBrush = colourBrush;
    }

    ClientSavedState (ClientSavedState const* const previousState_) :
        currentTransform(previousState_->currentTransform),
        clipRegion(previousState_->clipRegion),
        font(previousState_->font),
        currentBrush(previousState_->currentBrush),
        colourBrush(previousState_->colourBrush),
        bitmapBrush(previousState_->bitmapBrush),
        linearGradient(previousState_->linearGradient),
        radialGradient(previousState_->radialGradient),
        gradientStops(previousState_->gradientStops),
        fillType(previousState_->fillType),
        interpolationMode(previousState_->interpolationMode)
    {
    }

    ~ClientSavedState()
    {
        jassert(pushedLayers.size() == 0);
        clearFont();
        clearFill();
    }

    void pushLayer (const D2D1_LAYER_PARAMETERS& layerParameters, ID2D1DeviceContext* const deviceContext)
    {
        jassert (deviceContext);

        //
        // Clipping and transparency are all handled by pushing Direct2D layers. The SavedState creates an internal stack
        // of Layer objects to keep track of how many layers need to be popped.
        //
        // Pass nullptr for the PushLayer layer parameter to allow Direct2D to manage the layers (Windows 8 or later)
        deviceContext->SetTransform (D2D1::IdentityMatrix());
        deviceContext->PushLayer (layerParameters, nullptr);

        pushedLayers.push (PushedLayer {});
    }

    void pushGeometryClipLayer (ComSmartPtr<ID2D1Geometry> geometry, ID2D1DeviceContext* const deviceContext)
    {
        if (geometry != nullptr)
        {
            pushLayer (D2D1::LayerParameters (D2D1::InfiniteRect(), geometry), deviceContext);
        }
    }

    void pushAxisAlignedClipLayer (Rectangle<int> r, ID2D1DeviceContext* const deviceContext)
    {
        jassert (deviceContext);

        deviceContext->SetTransform (direct2d::transformToMatrix (currentTransform.getTransform()));
        deviceContext->PushAxisAlignedClip (direct2d::rectangleToRectF (r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        pushedLayers.push (PushedAxisAlignedClipLayer {});

        //DBG ("PushAxisAligned " << (int)pushedLayers.size());
    }

    void pushTransparencyLayer (float opacity, ID2D1DeviceContext* const deviceContext)
    {
        pushLayer (D2D1::LayerParameters (D2D1::InfiniteRect(),
            nullptr,
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(),
            opacity),
            deviceContext);
    }

    void popLayers (ID2D1DeviceContext* deviceContext)
    {
        LayerPopper layerPopper { deviceContext };

        while (pushedLayers.size() > 0)
        {
            auto& pushedLayer = pushedLayers.top();

            std::visit (layerPopper, pushedLayer);

            pushedLayers.pop();
        }
    }

    void popTopLayer (ID2D1DeviceContext* deviceContext)
    {
        LayerPopper layerPopper { deviceContext };

        if (pushedLayers.size() > 0)
        {
            auto& pushedLayer = pushedLayers.top();

            std::visit (layerPopper, pushedLayer);

            pushedLayers.pop();
        }
    }

    void setFont (const Font& newFont)
    {
        font = newFont;
    }

    void clearFont()
    {
    }

    direct2d::DirectWriteFontFace getFontFace()
    {
        ReferenceCountedObjectPtr<WindowsDirectWriteTypeface> typeface = dynamic_cast<WindowsDirectWriteTypeface*> (font.getTypefacePtr().get());
        if (typeface)
        {
            return
            {
                typeface->getIDWriteFontFace(),
                font.getHeight(),
                typeface->getUnitsToHeightScaleFactor(),
                font.getHorizontalScale()
            };
        }

        return {};
    }

    void setOpacity (float newOpacity)
    {
        fillType.setOpacity (newOpacity);
    }

    void clearFill()
    {
        gradientStops = nullptr;
        linearGradient = nullptr;
        radialGradient = nullptr;
        bitmapBrush = nullptr;
        currentBrush = nullptr;
    }

    void updateCurrentBrush (ID2D1DeviceContext* const deviceContext)
    {
        if (fillType.isColour())
        {
            currentBrush = (ID2D1Brush*) colourBrush;
        }
        else if (fillType.isTiledImage())
        {
            D2D1_BRUSH_PROPERTIES brushProps = { fillType.getOpacity(), direct2d::transformToMatrix (fillType.transform) };
            auto bmProps = D2D1::BitmapBrushProperties (D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP);

            auto image = fillType.image;

            D2D1_SIZE_U size = { (UINT32) image.getWidth(), (UINT32) image.getHeight() };
            auto bp = D2D1::BitmapProperties();

            image = image.convertedToFormat (Image::ARGB);
            Image::BitmapData bd (image, Image::BitmapData::readOnly);
            bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

            ComSmartPtr<ID2D1Bitmap> tiledImageBitmap;
            auto hr = deviceContext->CreateBitmap (size, bd.data, bd.lineStride, bp, tiledImageBitmap.resetAndGetPointerAddress());
            jassert (SUCCEEDED (hr));
            if (SUCCEEDED (hr))
            {
                hr = deviceContext->CreateBitmapBrush (tiledImageBitmap, bmProps, brushProps, bitmapBrush.resetAndGetPointerAddress());
                jassert (SUCCEEDED (hr));
                if (SUCCEEDED (hr))
                {
                    currentBrush = bitmapBrush;
                }
            }
        }
        else if (fillType.isGradient())
        {
            D2D1_BRUSH_PROPERTIES brushProps = { fillType.getOpacity(), direct2d::transformToMatrix (fillType.transform) };
            const int numColors = fillType.gradient->getNumColours();

            HeapBlock<D2D1_GRADIENT_STOP> stops (numColors);

            for (int i = fillType.gradient->getNumColours(); --i >= 0;)
            {
                stops[i].color = direct2d::colourToD2D (fillType.gradient->getColour (i));
                stops[i].position = (FLOAT) fillType.gradient->getColourPosition (i);
            }

            deviceContext->CreateGradientStopCollection (stops.getData(), numColors, gradientStops.resetAndGetPointerAddress());

            if (fillType.gradient->isRadial)
            {
                const auto p1 = fillType.gradient->point1;
                const auto p2 = fillType.gradient->point2;
                const auto r = p1.getDistanceFrom (p2);
                const auto props = D2D1::RadialGradientBrushProperties ({ p1.x, p1.y }, {}, r, r);

                deviceContext->CreateRadialGradientBrush (props, brushProps, gradientStops, radialGradient.resetAndGetPointerAddress());
                currentBrush = radialGradient;
            }
            else
            {
                const auto p1 = fillType.gradient->point1;
                const auto p2 = fillType.gradient->point2;
                const auto props = D2D1::LinearGradientBrushProperties ({ p1.x, p1.y }, { p2.x, p2.y });

                deviceContext->CreateLinearGradientBrush (props, brushProps, gradientStops, linearGradient.resetAndGetPointerAddress());

                currentBrush = linearGradient;
            }
        }

        updateColourBrush();
    }

    void updateColourBrush()
    {
        if (colourBrush && fillType.isColour())
        {
            auto colour = direct2d::colourToD2D (fillType.colour);
            colourBrush->SetColor (colour);
        }
    }

    RenderingHelpers::TranslationOrTransform currentTransform;
    Rectangle<int> clipRegion;

    Font font;

    ID2D1Brush* currentBrush = nullptr;
    ComSmartPtr<ID2D1SolidColorBrush>& colourBrush; // reference to shared colour brush
    ComSmartPtr<ID2D1BitmapBrush> bitmapBrush;
    ComSmartPtr<ID2D1LinearGradientBrush> linearGradient;
    ComSmartPtr<ID2D1RadialGradientBrush> radialGradient;
    ComSmartPtr<ID2D1GradientStopCollection> gradientStops;

    FillType fillType;

    D2D1_INTERPOLATION_MODE interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR;


    //
    // Bitmap & gradient brushes are position-dependent and are therefore affected by transforms
    //
    // Drawing text affects the world transform, so those brushes need an inverse transform to undo the world transform
    //
    struct ScopedBrushTransformInverter
    {
        ScopedBrushTransformInverter (ClientSavedState const* const state_, AffineTransform const& transformToInvert_) : state (state_)
        {
            //
            // Set the brush transform if the current brush is not the solid color brush
            //
            if (state_->currentBrush && state_->currentBrush != state_->colourBrush)
            {
                state_->currentBrush->SetTransform (direct2d::transformToMatrix (transformToInvert_.inverted()));
                resetTransform = true;
            }
        }

        ~ScopedBrushTransformInverter()
        {
            if (resetTransform)
            {
                state->currentBrush->SetTransform (D2D1::IdentityMatrix());
            }
        }

        ClientSavedState const* const state;
        bool resetTransform = false;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClientSavedState)
};


//==============================================================================

struct Direct2DLowLevelGraphicsContext::Pimpl
{
private:
    Direct2DLowLevelGraphicsContext& owner;
    double dpiScalingFactor = 1.0;

#if DIRECT2D_CHILD_WINDOW
    direct2d::ChildWindow::Class childWindowClass;
    direct2d::ChildWindow childWindow;
#endif
    direct2d::DeviceResources deviceResources;
    direct2d::SwapChain swap;
    direct2d::CompositionTree compositionTree;
    direct2d::UpdateRegion updateRegion;
    direct2d::SwapChainReadyThread swapChainReadyThread;

    std::stack<std::unique_ptr<Direct2DLowLevelGraphicsContext::ClientSavedState>> savedClientStates;

    int frameNumber = 0;
    RectangleList<int> deferredRepaints;
    Rectangle<int> windowSize;
    Rectangle<int> previousWindowSize;
    int dirtyRectanglesCapacity = 0;
    HeapBlock<RECT> dirtyRectangles;

    HRESULT prepare()
    {
        auto parentWindowSize = getParentClientRect();
#if DIRECT2D_CHILD_WINDOW
        auto swapChainHwnd = childWindow.childHwnd;
#else
        auto swapChainHwnd = parentHwnd;
#endif

        if (!swapChainHwnd || parentWindowSize.isEmpty())
        {
            return E_FAIL;
        }

        if (!deviceResources.canPaint())
        {
            if (auto hr = deviceResources.create(sharedFactories->getDirect2DFactory()); FAILED(hr))
            {
                return hr;
            }
        }

        if (!swap.canPaint())
        {
#if DIRECT2D_CHILD_WINDOW
            childWindow.setSize(parentWindowSize);
#endif

            if (auto hr = swap.create (swapChainHwnd, parentWindowSize, deviceResources.direct3DDevice, deviceResources.dxgiFactory); FAILED (hr))
            {
                return hr;
            }

            if (auto hr = swap.createBuffer(deviceResources.deviceContext); FAILED(hr))
            {
                return hr;
            }
        }

        if (!compositionTree.canPaint())
        {
            if (auto hr = compositionTree.create (deviceResources.dxgiDevice, swapChainHwnd, swap.chain); FAILED(hr))
            {
                return hr;
            }
        }

        swapChainReadyThread.start(swap.swapChainEvent);

        return S_OK;
    }

    void teardown()
    {
        swapChainReadyThread.stop();
        compositionTree.release();
        swap.release();
        deviceResources.release();
    }

    bool checkAndClearSwapChainReadyFlag()
    {
        bool expected = true;
        return swapChainReadyThread.eventSignaled.compare_exchange_weak(expected, false);
    }

#if 0
    void swapChainSignaledReady() override
    {
#if 0

        if (owner.swapChainReadyCallback)
        {
            owner.swapChainReadyCallback();
        }
#endif
    }

    void swapChainTimedOut() override
    {
        teardown();
    }
#endif

    JUCE_DECLARE_WEAK_REFERENCEABLE(Pimpl)

public:
    Pimpl(Direct2DLowLevelGraphicsContext& owner_, HWND hwnd_, direct2d::SwapChainListener* const listener_) : owner(owner_),
#if DIRECT2D_CHILD_WINDOW
        childWindow(childWindowClass.className, hwnd_),
#endif
        swapChainReadyThread(listener_),
        parentHwnd(hwnd_)
    {
    }

    ~Pimpl()
    {
        popAllSavedStates();

        teardown();
#if DIRECT2D_CHILD_WINDOW
        childWindow.close();
#endif
    }

#if DIRECT2D_CHILD_WINDOW
    void handleChildWindowChange(bool visible)
#else
    void handleChildWindowChange(bool)
#endif
    {
#if DIRECT2D_CHILD_WINDOW
        if (visible)
        {
            if (childWindow.childHwnd)
            {
                auto size = getParentClientRect();
                childWindow.setSize(size);
                prepare();
                deferredRepaints = size;
            }
        }
        else
        {
            teardown();
        }
#endif
    }

    Rectangle<int> getParentClientRect() const
    {
        RECT clientRect;
        GetClientRect(parentHwnd, &clientRect);

        return Rectangle<int>::leftTopRightBottom(clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
    }

    void startResizing()
    {
        previousWindowSize = windowSize;
    }

    void finishResizing()
    {
        if (previousWindowSize != windowSize)
        {
            resize(windowSize);
        }
    }

    void resize(Rectangle<int> size)
    {
        if (!size.isEmpty())
        {
            prepare();

            //
            // Require the entire window to be repainted
            //
            windowSize = size;
            deferredRepaints = size;

            if (auto deviceContext = deviceResources.deviceContext)
            {
#if DIRECT2D_CHILD_WINDOW
                childWindow.setSize(size);
#endif
                auto hr = swap.resize(size, (float) dpiScalingFactor, deviceContext);
                jassert(SUCCEEDED(hr));
            }
        }
    }

    void resize()
    {
        resize(getParentClientRect());
    }

    void restoreWindow()
    {
        //
        // Child window still has the original window size
        //
#if DIRECT2D_CHILD_WINDOW
        resize(windowSize);
#endif
    }

    void addDeferredRepaint(Rectangle<int> deferredRepaint)
    {
        deferredRepaints.add(deferredRepaint);
        prepare();
    }

    void addInvalidWindowRegionToDeferredRepaints()
    {
        updateRegion.getRECTAndValidate(parentHwnd);
        updateRegion.addToRectangleList(deferredRepaints);
        updateRegion.clear();
        prepare();
    }

    RectangleList<int> const& getDeferredRepaints() const noexcept
    {
        return deferredRepaints;
    }

    bool allocateResources()
    {
        //
        // Is everything set up?
        //
        if (!deviceResources.canPaint() || !swap.canPaint() || !compositionTree.canPaint())
        {
            //
            // Allocate resources
            //
            auto hr = prepare();
            if (FAILED(hr))
            {
                return false;
            }
        }

        return true;
    }

    ClientSavedState* startFrame()
    {
#if JUCE_DIRECT2D_METRICS
        paintStartTicks = Time::getHighResolutionTicks();
#endif

        //
        // Paint if:
        //      resources are allocated
        //      deferredRepaints has areas to be painted
        //      the swap chain is ready
        //
        bool ready = allocateResources();
        ready |= deferredRepaints.getNumRectangles() > 0;
        if (!ready)
        {
            return nullptr;
        }

        //
        // Paint the entire window or just part of it?
        //
        if (swap.state == direct2d::SwapChain::bufferAllocated)
        {
            deferredRepaints = swap.getSize();
        }

        auto paintBounds = deferredRepaints.getBounds();
        if (! windowSize.intersects (paintBounds) || paintBounds.isEmpty())
        {
            return nullptr;
        }

        //
        // Swap chain ready? Check this *last* so painting doesn't get stuck since this will clear the swap chain ready flag
        //
        if (!checkAndClearSwapChainReadyFlag())
        {
            return nullptr;
        }

        TRACE_LOG_D2D_PAINT_START(frameNumber);

        //
        // Init the saved state stack
        //
        auto firstSavedState = pushFirstSavedState(paintBounds);

        //
        // Start drawing
        //
        deviceResources.deviceContext->SetTarget(swap.buffer);
        deviceResources.deviceContext->BeginDraw();

        return firstSavedState;
    }

    void finishFrame()
    {
        //
        // Fully pop the state stack
        //
        popAllSavedStates();

        //
        // Finish drawing
        //
        auto hr = deviceResources.deviceContext->EndDraw();
        jassert(SUCCEEDED(hr));
        deviceResources.deviceContext->SetTarget(nullptr);

        TRACE_LOG_D2D_PAINT_END(frameNumber);

#if JUCE_DIRECT2D_METRICS
        owner.stats->addValueTicks(direct2d::PaintStats::messageThreadPaintDuration, Time::getHighResolutionTicks() - paintStartTicks);
#endif

        //
        // Present the frame
        //
        {
#if JUCE_DIRECT2D_METRICS
            direct2d::ScopedElapsedTime set{ owner.stats, direct2d::PaintStats::present1Duration };
#endif

            TRACE_LOG_D2D_PRESENT1_START(frameNumber);

	        if (dirtyRectanglesCapacity < deferredRepaints.getNumRectangles())
	        {
	            dirtyRectangles.realloc(deferredRepaints.getNumRectangles());
	            dirtyRectanglesCapacity = deferredRepaints.getNumRectangles();
	        }
	
	        DXGI_PRESENT_PARAMETERS presentParameters {};
            if (swap.state == direct2d::SwapChain::bufferFilled)
            {

                RECT* dirtyRectangle = dirtyRectangles.getData();
                auto const swapChainSize = swap.getSize();
                for (auto const& area : deferredRepaints)
                {
                    auto intersection = area.getIntersection(swapChainSize);
                    if (intersection.isEmpty() || area.contains(swapChainSize))
                    {
                        continue;
                    }

                    *dirtyRectangle = direct2d::rectangleToRECT(intersection * dpiScalingFactor);

                    dirtyRectangle++;
                    presentParameters.DirtyRectsCount++;
                }
                presentParameters.pDirtyRects = dirtyRectangles.getData();
            }

	        hr = swap.chain->Present1 (swap.presentSyncInterval, swap.presentFlags, &presentParameters);
	        jassert(SUCCEEDED(hr));

            if (presentParameters.DirtyRectsCount == 0)
            {
                swap.state = direct2d::SwapChain::bufferFilled;
            }

            if (FAILED(hr))
            {
                teardown();
            }

            TRACE_LOG_D2D_PRESENT1_END (frameNumber);
        }

        deferredRepaints.clear();

        frameNumber++;
    }

    void presentIdleFrame()
    {
        TRACE_LOG_D2D(etw::presentIdleFrame);

        if (allocateResources())
        {
            if (checkAndClearSwapChainReadyFlag())
            {
                switch (swap.state)
                {
                case direct2d::SwapChain::bufferAllocated:
                {
                    deviceResources.deviceContext->SetTarget(swap.buffer);
                    deviceResources.deviceContext->BeginDraw();
                    deviceResources.deviceContext->Clear();
                    auto hr = deviceResources.deviceContext->EndDraw();
                    deviceResources.deviceContext->SetTarget(nullptr);
                    if (SUCCEEDED(hr))
                    {
                        hr = swap.chain->Present(swap.presentSyncInterval, 0);
                        jassert(SUCCEEDED(hr));
                    }

                    swap.state = direct2d::SwapChain::bufferFilled;

                    break;
                }

                case direct2d::SwapChain::bufferFilled:
                {
                    //
                    // Present the same buffer again without flipping the swap chain
                    //
                    TRACE_LOG_PRESENT_DO_NOT_SEQUENCE_START(-frameNumber);

                    auto hr = swap.chain->Present(swap.presentSyncInterval, DXGI_PRESENT_DO_NOT_SEQUENCE);
                    jassert(SUCCEEDED(hr));

                    TRACE_LOG_PRESENT_DO_NOT_SEQUENCE_END(-frameNumber);
                    break;
                }

                default:
                {
                    break;
                }
                }
            }
        }
    }

    void setScaleFactor(double scale_)
    {
        dpiScalingFactor = scale_;
        deferredRepaints = windowSize;
        resize();
    }

    double getScaleFactor() const
    {
        return dpiScalingFactor;
    }

    ClientSavedState* getCurrentSavedState() const
    {
        return savedClientStates.size() > 0 ? savedClientStates.top().get() : nullptr;
    }

    ClientSavedState* pushFirstSavedState(Rectangle<int> initialClipRegion)
    {
        jassert(savedClientStates.size() == 0);

        savedClientStates.push(std::make_unique<ClientSavedState>(initialClipRegion, deviceResources.colourBrush));

        return getCurrentSavedState();
    }

    ClientSavedState* pushSavedState()
    {
        jassert(savedClientStates.size() > 0);

        savedClientStates.push(std::make_unique<ClientSavedState>(savedClientStates.top().get()));

        return getCurrentSavedState();
    }

    ClientSavedState* popSavedState()
    {
        savedClientStates.top()->popLayers (deviceResources.deviceContext);
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

    inline ID2D1DeviceContext* const getDeviceContext() const noexcept
    {
        return deviceResources.deviceContext;
    }
    
    SharedResourcePointer<Direct2DFactories> sharedFactories;
    HWND parentHwnd = nullptr;
    ComSmartPtr<ID2D1StrokeStyle> strokeStyle;
    direct2d::DirectWriteGlyphRun glyphRun;

#if JUCE_DIRECT2D_METRICS
    int64 paintStartTicks = 0;
    int64 paintEndTicks = 0;
#endif
};

//==============================================================================
Direct2DLowLevelGraphicsContext::Direct2DLowLevelGraphicsContext (HWND hwnd_, direct2d::SwapChainListener* const listener_)
    : currentState (nullptr),
      pimpl (new Pimpl { *this, hwnd_, listener_ })
{
    resize();
}

Direct2DLowLevelGraphicsContext::~Direct2DLowLevelGraphicsContext()
{
}

void Direct2DLowLevelGraphicsContext::handleChildWindowChange (bool visible)
{
    pimpl->handleChildWindowChange (visible);
}

void Direct2DLowLevelGraphicsContext::setWindowAlpha(float alpha)
{
    windowAlpha = alpha;
}

void Direct2DLowLevelGraphicsContext::startResizing()
{
    pimpl->startResizing();
}

void Direct2DLowLevelGraphicsContext::resize()
{
    pimpl->resize();
}

void Direct2DLowLevelGraphicsContext::resize (int width, int height)
{
    pimpl->resize ({ width, height });
}

void Direct2DLowLevelGraphicsContext::finishResizing()
{
    pimpl->finishResizing();
}

void Direct2DLowLevelGraphicsContext::restoreWindow()
{
    pimpl->restoreWindow();
}

void Direct2DLowLevelGraphicsContext::addDeferredRepaint (Rectangle<int> deferredRepaint)
{
    pimpl->addDeferredRepaint (deferredRepaint);
}

void Direct2DLowLevelGraphicsContext::addInvalidWindowRegionToDeferredRepaints()
{
    pimpl->addInvalidWindowRegionToDeferredRepaints();
}

bool Direct2DLowLevelGraphicsContext::startFrame()
{
    TRACE_LOG_D2D_START_FRAME;

    if (currentState = pimpl->startFrame (); currentState != nullptr)
    {
        auto const& paintAreas = pimpl->getDeferredRepaints();
        if (paintAreas.getBounds().isEmpty() == false)
        {
            clipToRectangleList (paintAreas);
        }

        pimpl->getDeviceContext()->Clear();

        setFont (currentState->font);

        currentState->updateCurrentBrush(pimpl->getDeviceContext());

        if (windowAlpha < 1.0f)
        {
            beginTransparencyLayer(windowAlpha);
        }

        return true;
    }

    //
    // Present the last frame again to keep the swap chain event firing
    //
    pimpl->presentIdleFrame();

    return false;
}

void Direct2DLowLevelGraphicsContext::endFrame()
{
    pimpl->popAllSavedStates();
    currentState = nullptr;

    pimpl->finishFrame();
}

void Direct2DLowLevelGraphicsContext::setOrigin (Point<int> o)
{
    currentState->currentTransform.setOrigin (o);
}

void Direct2DLowLevelGraphicsContext::addTransform (const AffineTransform& transform)
{
    currentState->currentTransform.addTransform (transform);
}

float Direct2DLowLevelGraphicsContext::getPhysicalPixelScaleFactor()
{
    return currentState->currentTransform.getPhysicalPixelScaleFactor();
}

bool Direct2DLowLevelGraphicsContext::clipToRectangle (const Rectangle<int>& r)
{
    //
    // Transform the rectangle and update the current clip region
    //
    auto currentTransform = currentState->currentTransform.getTransform();
    auto transformedR = r.transformedBy (currentTransform);
    transformedR.intersectRectangle (currentState->clipRegion);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushAxisAlignedClipLayer (r, deviceContext);
    }

    return ! isClipEmpty();
}

bool Direct2DLowLevelGraphicsContext::clipToRectangleList (const RectangleList<int>& clipRegion)
{
    //
    // Just one rectangle?
    //
    if (clipRegion.getNumRectangles() == 1)
    {
        return clipToRectangle(clipRegion.getRectangle(0));
    }

    //
    // Transform the rectangles and update the current clip region
    //
    auto const currentTransform = currentState->currentTransform.getTransform();
    auto transformedR = clipRegion.getBounds().transformedBy (currentTransform);
    transformedR.intersectRectangle (currentState->clipRegion);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushGeometryClipLayer (direct2d::rectListToPathGeometry (pimpl->sharedFactories->getDirect2DFactory(),
            clipRegion, 
            currentState->currentTransform.getTransform(), 
            D2D1_FILL_MODE_WINDING), 
            deviceContext);
    }

    return ! isClipEmpty();
}

void Direct2DLowLevelGraphicsContext::excludeClipRectangle (const Rectangle<int>& r)
{
    //
    // To exclude the rectangle r, build a rectangle list with r as the first rectangle and a very large rectangle as the second.
    //
    // Then, convert that rectangle list to a geometry, but specify D2D1_FILL_MODE_ALTERNATE so the inside of r is *outside*
    // the geometry and everything else on the screen is inside the geometry.
    //
    // Have to use addWithoutMerging to build the rectangle list to keep the rectangles separate.
    //
    RectangleList<int> rectangles { r };
    rectangles.addWithoutMerging({ -maxWindowSize, -maxWindowSize, maxWindowSize * 2, maxWindowSize * 2 });

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushGeometryClipLayer (direct2d::rectListToPathGeometry(pimpl->sharedFactories->getDirect2DFactory(),
            rectangles, 
            currentState->currentTransform.getTransform(), 
            D2D1_FILL_MODE_ALTERNATE), 
            deviceContext);
    }
}

void Direct2DLowLevelGraphicsContext::clipToPath (const Path& path, const AffineTransform& transform)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushGeometryClipLayer(direct2d::pathToPathGeometry(pimpl->sharedFactories->getDirect2DFactory(),
            path, 
            currentState->currentTransform.getTransformWith (transform)), 
            deviceContext);
    }
}

void Direct2DLowLevelGraphicsContext::clipToImageAlpha (const Image& sourceImage, const AffineTransform& transform)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        auto const maskImage = sourceImage.convertedToFormat(Image::SingleChannel);
        Image::BitmapData bitmapData { maskImage, Image::BitmapData::readOnly };

        auto bitmapProperties = D2D1::BitmapProperties();
        bitmapProperties.pixelFormat.format = DXGI_FORMAT_A8_UNORM;
        bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

        ComSmartPtr<ID2D1Bitmap> bitmap;
        auto hr = deviceContext->CreateBitmap (D2D1_SIZE_U { (UINT32) maskImage.getWidth(),
                                                             (UINT32) maskImage.getHeight() },
                                               bitmapData.data,
                                               bitmapData.lineStride,
                                               bitmapProperties,
                                               bitmap.resetAndGetPointerAddress());
        if (SUCCEEDED (hr))
        {
            ComSmartPtr<ID2D1BitmapBrush> brush;
            auto matrix = direct2d::transformToMatrix (currentState->currentTransform.getTransformWith(transform));
            D2D1_BRUSH_PROPERTIES brushProps = { 1.0f, matrix };

            auto bitmapBrushProps = D2D1::BitmapBrushProperties (D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP);
            hr = deviceContext->CreateBitmapBrush (bitmap, bitmapBrushProps, brushProps, brush.resetAndGetPointerAddress());
            if (SUCCEEDED (hr))
            {
                auto layerParams = D2D1::LayerParameters();
                layerParams.contentBounds = direct2d::rectangleToRectF (maskImage.getBounds().toFloat().transformedBy (transform));
                layerParams.maskTransform = matrix;
                layerParams.opacityBrush = brush;

                currentState->pushLayer (layerParams, deviceContext);
            }
        }
    }
}

bool Direct2DLowLevelGraphicsContext::clipRegionIntersects (const Rectangle<int>& r)
{
    return getClipBounds().intersects (r);
}

Rectangle<int> Direct2DLowLevelGraphicsContext::getClipBounds() const
{
    return currentState->currentTransform.deviceSpaceToUserSpace (currentState->clipRegion);
}

bool Direct2DLowLevelGraphicsContext::isClipEmpty() const
{
    return getClipBounds().isEmpty();
}

void Direct2DLowLevelGraphicsContext::saveState()
{
    currentState = pimpl->pushSavedState();
}

void Direct2DLowLevelGraphicsContext::restoreState()
{
    currentState = pimpl->popSavedState();
    jassert(currentState);
}

void Direct2DLowLevelGraphicsContext::beginTransparencyLayer (float opacity)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushTransparencyLayer(opacity, deviceContext);
    }
}

void Direct2DLowLevelGraphicsContext::endTransparencyLayer()
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->popTopLayer(deviceContext);
    }
}

void Direct2DLowLevelGraphicsContext::setFill (const FillType& fillType)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->fillType = fillType;
        currentState->updateCurrentBrush(deviceContext);
    }
}

void Direct2DLowLevelGraphicsContext::setOpacity (float newOpacity)
{
    currentState->setOpacity(newOpacity);
}

void Direct2DLowLevelGraphicsContext::setInterpolationQuality (Graphics::ResamplingQuality quality)
{
    switch (quality)
    {
        case Graphics::ResamplingQuality::lowResamplingQuality:
            currentState->interpolationMode = D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
            break;

        case Graphics::ResamplingQuality::mediumResamplingQuality:
            currentState->interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR;
            break;

        case Graphics::ResamplingQuality::highResamplingQuality:
            currentState->interpolationMode = D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
            break;
    }
}

void Direct2DLowLevelGraphicsContext::fillRect (const Rectangle<int>& r, bool /*replaceExistingContents*/)
{
    fillRect (r.toFloat());
}

void Direct2DLowLevelGraphicsContext::fillRect (const Rectangle<float>& r)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return;
        }

        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));
        deviceContext->FillRectangle (direct2d::rectangleToRectF (r), currentState->currentBrush);
    }
}

void Direct2DLowLevelGraphicsContext::fillRectList (const RectangleList<float>& list)
{
    for (auto& r : list)
        fillRect (r);
}

bool Direct2DLowLevelGraphicsContext::drawRect (const Rectangle<float>& r, float lineThickness)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));
        deviceContext->DrawRectangle (direct2d::rectangleToRectF (r), currentState->currentBrush, lineThickness);
    }

    return true;
}

void Direct2DLowLevelGraphicsContext::fillPath (const Path& p, const AffineTransform& transform)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return;
        }

        if (auto geometry = direct2d::pathToPathGeometry (pimpl->sharedFactories->getDirect2DFactory(), p, transform))
        {
            deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));
            deviceContext->FillGeometry (geometry, currentState->currentBrush);
        }
    }
}

bool Direct2DLowLevelGraphicsContext::drawPath (const Path& p, const PathStrokeType& strokeType, const AffineTransform& transform)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        if (auto factory = pimpl->sharedFactories->getDirect2DFactory())
        {
            if (auto geometry = direct2d::pathToPathGeometry (factory, p, transform))
            {
                // JUCE JointStyle   ID2D1StrokeStyle
                // ---------------   ----------------
                // mitered           D2D1_LINE_JOIN_MITER
                // curved            D2D1_LINE_JOIN_ROUND
                // beveled           D2D1_LINE_JOIN_BEVEL
                //
                // JUCE EndCapStyle  ID2D1StrokeStyle
                // ----------------  ----------------
                // butt              D2D1_CAP_STYLE_FLAT
                // square            D2D1_CAP_STYLE_SQUARE
                // rounded           D2D1_CAP_STYLE_ROUND
                //
                auto lineJoin = D2D1_LINE_JOIN_MITER;
                switch (strokeType.getJointStyle())
                {
                    case PathStrokeType::JointStyle::mitered:
                        // already set
                        break;

                    case PathStrokeType::JointStyle::curved:
                        lineJoin = D2D1_LINE_JOIN_ROUND;
                        break;

                    case PathStrokeType::JointStyle::beveled:
                        lineJoin = D2D1_LINE_JOIN_BEVEL;
                        break;

                    default:
                        // invalid EndCapStyle
                        jassertfalse;
                        break;
                }

                auto capStyle = D2D1_CAP_STYLE_FLAT;
                switch (strokeType.getEndStyle())
                {
                    case PathStrokeType::EndCapStyle::butt:
                        // already set
                        break;

                    case PathStrokeType::EndCapStyle::square:
                        capStyle = D2D1_CAP_STYLE_SQUARE;
                        break;

                    case PathStrokeType::EndCapStyle::rounded:
                        capStyle = D2D1_CAP_STYLE_ROUND;
                        break;

                    default:
                        // invalid EndCapStyle
                        jassertfalse;
                        break;
                }

                D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties {
                    capStyle, capStyle, capStyle, lineJoin, 1.0f, D2D1_DASH_STYLE_SOLID, 0.0f
                };
                factory->CreateStrokeStyle (strokeStyleProperties, // TODO reuse the stroke style
                                                    nullptr,
                                                    0,
                                                    pimpl->strokeStyle.resetAndGetPointerAddress());

                deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));
                deviceContext->DrawGeometry (geometry, currentState->currentBrush, strokeType.getStrokeThickness(), pimpl->strokeStyle);
            }
        }
    }
    return true;
}

void Direct2DLowLevelGraphicsContext::drawImage (const Image& image, const AffineTransform& transform)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransformWith (transform)));

        auto argbImage = image.convertedToFormat(Image::ARGB);
        Image::BitmapData bitmapData { argbImage, Image::BitmapData::readOnly };

        auto bitmapProperties = D2D1::BitmapProperties();
        bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

        D2D1_SIZE_U size = { (UINT32) image.getWidth(), (UINT32) image.getHeight() };

        ComSmartPtr<ID2D1Bitmap> bitmap;
        deviceContext->CreateBitmap (size, bitmapData.data, bitmapData.lineStride, bitmapProperties, bitmap.resetAndGetPointerAddress());
        if (bitmap)
        {
            deviceContext->DrawImage (bitmap, currentState->interpolationMode);
        }
    }
}

void Direct2DLowLevelGraphicsContext::drawLine (const Line<float>& line)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return;
        }

        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));
        deviceContext->DrawLine (D2D1::Point2F (line.getStartX(), line.getStartY()),
                                 D2D1::Point2F (line.getEndX(), line.getEndY()),
                                 currentState->currentBrush);
    }
}

void Direct2DLowLevelGraphicsContext::setFont (const Font& newFont)
{
    currentState->setFont (newFont);
}

const Font& Direct2DLowLevelGraphicsContext::getFont()
{
    return currentState->font;
}

void Direct2DLowLevelGraphicsContext::drawGlyph (int glyphNumber, const AffineTransform& transform)
{
    pimpl->glyphRun.glyphIndices[0] = (uint16)glyphNumber;
    pimpl->glyphRun.glyphOffsets[0] = {};

    drawGlyphCommon(1, transform, {});
}

bool Direct2DLowLevelGraphicsContext::drawTextLayout (const AttributedString& text, const Rectangle<float>& area)
{
    if (currentState->fillType.isInvisible())
    {
        return true;
    }

    auto deviceContext = pimpl->getDeviceContext();
    auto directWriteFactory = pimpl->sharedFactories->getDirectWriteFactory();
    auto fontCollection = pimpl->sharedFactories->getSystemFonts();

    if (deviceContext && directWriteFactory && fontCollection)
    {
        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));

        auto translatedArea = area;
        auto textLayout = DirectWriteTypeLayout::createDirectWriteTextLayout(text,
            translatedArea,
            *directWriteFactory,
            *fontCollection,
            *deviceContext);
        if (textLayout)
        {
            deviceContext->DrawTextLayout (D2D1::Point2F (translatedArea.getX(), translatedArea.getY()),
                                            textLayout,
                                            currentState->currentBrush,
                                            D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    }

    return true;
}

void Direct2DLowLevelGraphicsContext::setScaleFactor (double scale_)
{
    pimpl->setScaleFactor (scale_);
}

double Direct2DLowLevelGraphicsContext::getScaleFactor() const
{
    return pimpl->getScaleFactor();
}

bool Direct2DLowLevelGraphicsContext::drawRoundedRectangle (Rectangle<float> area, float cornerSize, float lineThickness)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));
        D2D1_ROUNDED_RECT roundedRect {
            direct2d::rectangleToRectF (area),
            cornerSize,
            cornerSize
        };
        deviceContext->DrawRoundedRectangle (roundedRect, currentState->currentBrush, lineThickness);
    }

    return true;
}

bool Direct2DLowLevelGraphicsContext::fillRoundedRectangle (Rectangle<float> area, float cornerSize)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));
        D2D1_ROUNDED_RECT roundedRect {
            direct2d::rectangleToRectF (area),
            cornerSize,
            cornerSize
        };
        deviceContext->FillRoundedRectangle (roundedRect, currentState->currentBrush);
    }

    return true;
}

bool Direct2DLowLevelGraphicsContext::drawEllipse (Rectangle<float> area, float lineThickness)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));

        auto centre = area.getCentre();
        D2D1_ELLIPSE ellipse {
            { centre.x, centre.y },
            area.proportionOfWidth (0.5f),
            area.proportionOfHeight (0.5f)
        };
        deviceContext->DrawEllipse (ellipse, currentState->currentBrush, lineThickness, nullptr);
    }
    return true;
}

bool Direct2DLowLevelGraphicsContext::fillEllipse (Rectangle<float> area)
{
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        deviceContext->SetTransform (direct2d::transformToMatrix (currentState->currentTransform.getTransform()));

        auto centre = area.getCentre();
        D2D1_ELLIPSE ellipse {
            { centre.x, centre.y },
            area.proportionOfWidth (0.5f),
            area.proportionOfHeight (0.5f)
        };
        deviceContext->FillEllipse (ellipse, currentState->currentBrush);
    }
    return true;
}

void Direct2DLowLevelGraphicsContext::drawGlyphRun (Array<PositionedGlyph> const& glyphs, int startIndex, int numGlyphs, const AffineTransform& transform, Rectangle<float> underlineArea)
{
    if (numGlyphs > 0 && (startIndex + numGlyphs) <= glyphs.size())
    {
        if (currentState->fillType.isInvisible())
        {
            return;
        }

        //
        // Fill the array of glyph indices and offsets
        //
        pimpl->glyphRun.ensureStorageAllocated(numGlyphs);

        auto fontHorizontalScale = currentState->font.getHorizontalScale();
        auto inverseHScale = fontHorizontalScale > 0.0f ? 1.0f / fontHorizontalScale : 1.0f;

        auto indices = pimpl->glyphRun.glyphIndices.getData();
        auto offsets = pimpl->glyphRun.glyphOffsets.getData();

        for (int i = 0; i < numGlyphs; ++i)
        {
            auto const& glyph = glyphs[i + startIndex];
            indices[i] = (UINT16)glyph.getGlyphNumber();
            offsets[i] = { glyph.getLeft() * inverseHScale, -glyph.getBaselineY() }; // note the essential minus sign before the baselineY value; negative offset goes down, positive goes up (opposite from JUCE)
            jassert(pimpl->glyphRun.glyphAdvances[i] == 0.0f);
        }

        drawGlyphCommon(numGlyphs, transform, underlineArea);
    }
}

void Direct2DLowLevelGraphicsContext::drawGlyphCommon(int numGlyphs, const AffineTransform& transform, Rectangle<float> underlineArea)
{
    auto deviceContext = pimpl->getDeviceContext();
    if (! deviceContext)
    {
        return;
    }

    auto dwriteFontFace = currentState->getFontFace();
    if (dwriteFontFace.fontFace == nullptr)
    {
        return;
    }

    if (currentState->fillType.isInvisible())
    {
        return;
    }

    //
    // Draw the glyph run
    //
    auto scaledTransform = AffineTransform::scale (dwriteFontFace.fontHorizontalScale, 1.0f).followedBy (transform);
    auto deviceContextTransform = scaledTransform.followedBy (currentState->currentTransform.getTransform());
    deviceContext->SetTransform (direct2d::transformToMatrix (deviceContextTransform));

    DWRITE_GLYPH_RUN directWriteGlyphRun;
    directWriteGlyphRun.fontFace = dwriteFontFace.fontFace;
    directWriteGlyphRun.fontEmSize = dwriteFontFace.getEmSize();
    directWriteGlyphRun.glyphCount = numGlyphs;
    directWriteGlyphRun.glyphIndices = pimpl->glyphRun.glyphIndices.getData();
    directWriteGlyphRun.glyphAdvances = pimpl->glyphRun.glyphAdvances.getData();
    directWriteGlyphRun.glyphOffsets = pimpl->glyphRun.glyphOffsets.getData();
    directWriteGlyphRun.isSideways = FALSE;
    directWriteGlyphRun.bidiLevel = 0;

    //
    // The gradient brushes are position-dependent, so need to undo the device context transform
    //
    ClientSavedState::ScopedBrushTransformInverter brushTransformInverter { currentState, scaledTransform };

    deviceContext->DrawGlyphRun ({}, &directWriteGlyphRun, currentState->currentBrush);

    //
    // Draw the underline
    //
    if (!underlineArea.isEmpty())
    {
        fillRect(underlineArea);
    }
}

} // namespace juce
