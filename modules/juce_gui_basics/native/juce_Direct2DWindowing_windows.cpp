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
    #define JUCE_DIRECT2D 1

    #include <windows.h>
    #include <d2d1_2.h>
    #include <d3d11_1.h>
    #include <dcomp.h>
    #include <dwrite.h>
#include <dwmapi.h>
    #include <juce_core/juce_core.h>
    #include <juce_graphics/juce_graphics.h>
    #include <juce_gui_basics/juce_gui_basics.h>
    #include "juce_Windowing_windows.cpp"

#endif

#if JUCE_DIRECT2D

class Direct2DComponentPeer : public HWNDComponentPeer
{
private:
    #if JUCE_DIRECT2D_METRICS
    int64 lastPaintStartTicks = 0;
    #endif

public:
    enum
    {
        direct2DRenderingEngine = softwareRenderingEngine + 1
    };

    //==============================================================================
    Direct2DComponentPeer (Component& comp, int windowStyleFlags, HWND parent, bool nonRepainting, int renderingEngine)
        : HWNDComponentPeer (comp, windowStyleFlags, parent, nonRepainting, renderingEngine)
    {
    }

    void initialise() override
    {
        HWNDComponentPeer::initialise();
        updateDirect2DContext();
    }

    ~Direct2DComponentPeer() override
    {
        direct2DContext = nullptr;
    }

    DWORD adjustWindowStyleFlags (DWORD exStyleFlags) override
    {
        exStyleFlags &= ~WS_EX_LAYERED;
        exStyleFlags |= WS_EX_NOREDIRECTIONBITMAP;

        return exStyleFlags;
    }

    void updateBorderSize() override
    {
        HWNDComponentPeer::updateBorderSize();

        updateDirect2DSize();
    }

    void setAlpha (float newAlpha) override
    {
        if (direct2DContext)
        {
            direct2DContext->setWindowAlpha (newAlpha);
            component.repaint();
            return;
        }

        HWNDComponentPeer::setAlpha (newAlpha);
    }

    void repaint (const Rectangle<int>& area) override
    {
        if (usingDirect2DRendering())
        {
            direct2DContext->addDeferredRepaint (area);
            return;
        }

        HWNDComponentPeer::repaint (area);
    }

    void dispatchDeferredRepaints()
    {
        if (usingDirect2DRendering())
        {
            return;
        }

        HWNDComponentPeer::dispatchDeferredRepaints();
    }

    void performAnyPendingRepaintsNow() override
    {
        if (usingDirect2DRendering())
        {
            // repaint will happen on the next vblank
            return;
        }

        HWNDComponentPeer::performAnyPendingRepaintsNow();
    }

    Image createWindowSnapshot()
    {
        if (usingDirect2DRendering())
        {
            return direct2DContext->createSnapshot();
        }

        return {};
    }

    void blitImageToWindow(Image const & sourceImage, Point<int> origin, RectangleList<int> const& clipList)
    {
        if (offscreenDirect2DImage.getBounds() != sourceImage.getBounds())
        {
            Direct2DImageType imageType;
            offscreenDirect2DImage = Image{ imageType.create(Image::ARGB, sourceImage.getWidth(), sourceImage.getHeight(), true) };
        }

        {
            Graphics g{ offscreenDirect2DImage };
            g.drawImageAt(sourceImage, 0, 0);
        }

        if (direct2DContext)
        {
            for (auto area : clipList)
            {
                area += origin;
                direct2DContext->addDeferredRepaint(area);
            }
 
            if (direct2DContext->startFrame())
            {
                 for (auto const& area : clipList)
                 {
                     direct2DContext->drawImageSection (offscreenDirect2DImage, area, area.getTopLeft() + origin);
                 }

                direct2DContext->endFrame();
            }
        }
    }

private:
    #if JUCE_ETW_TRACELOGGING
    SharedResourcePointer<ETWEventProvider> etwEventProvider;
    #endif
    std::unique_ptr<Direct2DHwndContext> direct2DContext;
    Image offscreenDirect2DImage;

    void handlePaintMessage() override
    {
        if (usingDirect2DRendering())
        {
            direct2DContext->addInvalidWindowRegionToDeferredRepaints();
            return;
        }

    #if JUCE_DIRECT2D_METRICS
        auto paintStartTicks = Time::getHighResolutionTicks();
    #endif

        HWNDComponentPeer::handlePaintMessage();

    #if JUCE_DIRECT2D_METRICS
        if (lastPaintStartTicks > 0)
        {
            paintStats->addValueTicks (direct2d::PaintStats::frameInterval, paintStartTicks - lastPaintStartTicks);
            paintStats->addValueTicks (direct2d::PaintStats::messageThreadPaintDuration, Time::getHighResolutionTicks() - paintStartTicks);
        }
        lastPaintStartTicks = paintStartTicks;
    #endif
    }

    void onVBlank() override
    {
        HWNDComponentPeer::onVBlank();

        if (usingDirect2DRendering())
        {
            handleDirect2DPaint();
        }
    }

    bool usingDirect2DRendering() const noexcept
    {
        return currentRenderingEngine == direct2DRenderingEngine && direct2DContext;
    }

    bool isPaintReady() const noexcept override
    {
        if (direct2DContext)
        {
            return direct2DContext->isReady();
        }

        return false;
    }

    void createOffscreenImageGenerator() override
    {
        offscreenImageGenerator = std::make_unique<D2DTemporaryImage>();
    }

    void performPaint(HDC dc, HRGN rgn, int regionType, PAINTSTRUCT& paintStruct) override
    {
        int x = paintStruct.rcPaint.left;
        int y = paintStruct.rcPaint.top;
        int w = paintStruct.rcPaint.right - x;
        int h = paintStruct.rcPaint.bottom - y;

        if (w <= 0 || h <= 0)
        {
            // it's not possible to have a transparent window with a title bar at the moment!
            //jassert (! hasTitleBar());

            auto r = getWindowScreenRect(hwnd);
            x = y = 0;
            w = r.right - r.left;
            h = r.bottom - r.top;
        }

        {
            Image& offscreenImage = offscreenImageGenerator->getImage(true, w, h);

            RectangleList<int> contextClip;
            const Rectangle<int> clipBounds(w, h);

            bool needToPaintAll = true;

            if (regionType == COMPLEXREGION || regionType == SIMPLEREGION)
            {
                HRGN clipRgn = CreateRectRgnIndirect(&paintStruct.rcPaint);
                CombineRgn(rgn, rgn, clipRgn, RGN_AND);
                DeleteObject(clipRgn);

                std::aligned_storage_t<8192, alignof (RGNDATA)> rgnData;
                const DWORD res = GetRegionData(rgn, sizeof(rgnData), (RGNDATA*)&rgnData);

                if (res > 0 && res <= sizeof(rgnData))
                {
                    const RGNDATAHEADER* const hdr = &(((const RGNDATA*)&rgnData)->rdh);

                    if (hdr->iType == RDH_RECTANGLES
                        && hdr->rcBound.right - hdr->rcBound.left >= w
                        && hdr->rcBound.bottom - hdr->rcBound.top >= h)
                    {
                        needToPaintAll = false;

                        auto rects = unalignedPointerCast<const RECT*>((char*)&rgnData + sizeof(RGNDATAHEADER));

                        for (int i = (int)((RGNDATA*)&rgnData)->rdh.nCount; --i >= 0;)
                        {
                            if (rects->right <= x + w && rects->bottom <= y + h)
                            {
                                const int cx = jmax(x, (int)rects->left);
                                contextClip.addWithoutMerging(Rectangle<int>(cx - x, rects->top - y,
                                    rects->right - cx, rects->bottom - rects->top)
                                    .getIntersection(clipBounds));
                            }
                            else
                            {
                                needToPaintAll = true;
                                break;
                            }

                            ++rects;
                        }
                    }
                }
            }

            if (needToPaintAll)
            {
                contextClip.clear();
                contextClip.addWithoutMerging(Rectangle<int>(w, h));
            }

            ChildWindowClippingInfo childClipInfo = { dc, this, &contextClip, Point<int>(x, y), 0 };
            EnumChildWindows(hwnd, clipChildWindowCallback, (LPARAM)&childClipInfo);

            if (!contextClip.isEmpty())
            {
                for (auto& i : contextClip)
                    offscreenImage.clear(i);

                {
                    auto context = component.getLookAndFeel()
                        .createGraphicsContext(offscreenImage, { -x, -y }, contextClip);

                    context->addTransform(AffineTransform::scale((float)getPlatformScaleFactor()));
                    handlePaint(*context);
                }

                blitImageToWindow(offscreenImage, { x, y }, contextClip);
            }

            if (childClipInfo.savedDC != 0)
                RestoreDC(dc, childClipInfo.savedDC);
        }
    }

    void handleDirect2DPaint()
    {
    #if JUCE_DIRECT2D_METRICS
        auto paintStartTicks = Time::getHighResolutionTicks();
    #endif

        jassert (direct2DContext);

        //
        // Use the ID2D1DeviceContext to paint a swap chain buffer, then tell the swap chain to present
        // the next buffer.
        // 
        // Direct2DLowLevelGraphicsContext::startFrame checks if if there are any areas to be painted and if the 
        // renderer is ready to go; if so, startFrame allocates any needed Direct2D resources, 
        // and calls ID2D1DeviceContext::BeginDraw
        // 
        // handlePaint() makes various calls into the Direct2DLowLevelGraphicsContext which in turn calls
        // the appropriate ID2D1DeviceContext functions to draw rectangles, clip, set the fill color, etc.
        // 
        // Direct2DLowLevelGraphicsContext::endFrame calls ID2D1DeviceContext::EndDraw to finish painting 
        // and then tells the swap chain to present the next swap chain back buffer.
        //
        if (direct2DContext->startFrame())
        {
            handlePaint (*direct2DContext);
            direct2DContext->endFrame();

    #if JUCE_DIRECT2D_METRICS
            if (lastPaintStartTicks > 0)
            {
                paintStats->addValueTicks (direct2d::PaintStats::messageThreadPaintDuration,
                                           Time::getHighResolutionTicks() - paintStartTicks);
                paintStats->addValueTicks (direct2d::PaintStats::frameInterval, paintStartTicks - lastPaintStartTicks);
            }
            lastPaintStartTicks = paintStartTicks;
    #endif
            return;
        }
    }

    void handleDirect2DResize (int width, int height)
    {
        if (direct2DContext)
        {
            direct2DContext->setSize (width, height);
        }
    }

    void updateDirect2DSize()
    {
        if (direct2DContext && component.isVisible())
        {
            direct2DContext->updateSize();
        }
    }

    StringArray getAvailableRenderingEngines() override
    {
        auto engines = HWNDComponentPeer::getAvailableRenderingEngines();

        if (SystemStats::getOperatingSystemType() >= SystemStats::Windows8_1) engines.add ("Direct2D");

        return engines;
    }

    void updateDirect2DContext()
    {
        direct2DContext = std::make_unique<Direct2DHwndContext>(hwnd, (float)scaleFactor, component.isOpaque());
#if JUCE_DIRECT2D_METRICS
        direct2DContext->stats = paintStats;
#endif
        direct2DContext->setPhysicalPixelScaleFactor((float)getPlatformScaleFactor());

        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void setCurrentRenderingEngine ([[maybe_unused]] int index) override
    {
        if (index != currentRenderingEngine)
        {
            currentRenderingEngine = jlimit (0, getAvailableRenderingEngines().size() - 1, index);
            updateDirect2DContext();
        }
    }

    LRESULT handleSizeConstraining (RECT& r, const WPARAM wParam) override
    {
        auto result = HWNDComponentPeer::handleSizeConstraining (r, wParam);

        updateDirect2DSize();

        return result;
    }

    //==============================================================================
    LRESULT handleDPIChanging (int newDPI, RECT newRect) override
    {
        auto result = HWNDComponentPeer::handleDPIChanging (newDPI, newRect);

        if (direct2DContext)
        {
            direct2DContext->setPhysicalPixelScaleFactor ((float) scaleFactor);
        }

        return result;
    }

    LRESULT peerWindowProc (HWND messageHwnd, UINT message, WPARAM wParam, LPARAM lParam) override
    {
        //Logger::outputDebugString ("peerWindowProc d2d " + String::toHexString ((int) message));

        TRACE_LOG_PARENT_WINDOW_MESSAGE (message);

        switch (message)
        {
            case WM_PAINT:
            {
                if (usingDirect2DRendering())
                {
                    direct2DContext->addInvalidWindowRegionToDeferredRepaints();
                    return 0;
                }
                break;
            }

            case WM_NCCALCSIZE:
            {
                TRACE_LOG_D2D_RESIZE (WM_NCCALCSIZE);

                if (direct2DContext && component.isVisible())
                {
                    RECT* rect = (RECT*) lParam;
                    direct2DContext->setSize (rect->right - rect->left, rect->bottom - rect->top);
                }
                break;
            }

            case WM_SYSCOMMAND:
            {
                switch (wParam & 0xfff0)
                {
                    case SC_MAXIMIZE:
                    case SC_RESTORE:
                    {
                        if (messageHwnd == hwnd)
                        {
                            auto status = HWNDComponentPeer::peerWindowProc (messageHwnd, message, wParam, lParam);

                            updateDirect2DSize();

                            return status;
                        }

                        break;
                    }

                    case SC_MINIMIZE: break;
                }

                break;
            }

            case WM_SHOWWINDOW:
            {
                //
                // If this window is now shown (wParam != 0), tell the Direct2D LLGC to create resources
                // and paint the whole window immediately
                //
                if (direct2DContext && wParam)
                {
                    direct2DContext->handleShowWindow();
                    handleDirect2DPaint();
                }
                break;
            }

            default: break;
        }

        return HWNDComponentPeer::peerWindowProc (messageHwnd, message, wParam, lParam);
    }

    struct D2DTemporaryImage : public TemporaryImage
    {
        D2DTemporaryImage() {}
        ~D2DTemporaryImage() override = default;

        Image& getImage(bool /*transparent*/, int w, int h) override
        {
            auto format = Image::ARGB;

            if ((!image.isValid()) || image.getWidth() < w || image.getHeight() < h || image.getFormat() != format)
            {
                SoftwareImageType imageType;
                image = Image{ imageType.create(format, w, h, true) };
            }

            startTimer(3000);
            return image;
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(D2DTemporaryImage)
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Direct2DComponentPeer)
};

ComponentPeer* Component::createNewPeer (int styleFlags, void* parentHWND)
{
    auto peer = new Direct2DComponentPeer { *this, styleFlags, (HWND) parentHWND, false, Direct2DComponentPeer::direct2DRenderingEngine };
    peer->initialise();
    return peer;
}


#if JUCE_DIRECT2D_SNAPSHOT
Image createSnapshotOfNativeWindow(void* nativeWindowHandle)
{
    int numDesktopComponents = Desktop::getInstance().getNumComponents();
    for (int index = 0; index < numDesktopComponents; ++index)
    {
        auto component = Desktop::getInstance().getComponent(index);
        if (auto peer = component->getPeer(); peer && peer->getNativeHandle() == nativeWindowHandle)
        {
            if (auto direct2DPeer = dynamic_cast<Direct2DComponentPeer*>(peer))
            {
                return direct2DPeer->createWindowSnapshot();
            }
        }
    }

    return createGDISnapshotOfNativeWindow(nativeWindowHandle);
}
#endif

#endif
