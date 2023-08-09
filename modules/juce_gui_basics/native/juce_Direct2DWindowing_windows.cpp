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
#define JUCE_WINDOWS 1

#include <windows.h>
#include <d2d1_2.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dwrite.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include "juce_Windowing_windows.cpp"

#endif

#if JUCE_DIRECT2D

class Direct2DComponentPeer : public HWNDComponentPeer, public direct2d::SwapChainListener
{
public:
    enum
    {
        direct2DRenderingEngine = softwareRenderingEngine + 1
    };

    //==============================================================================
    Direct2DComponentPeer(Component& comp, int windowStyleFlags, HWND parent, bool nonRepainting, int renderingEngine) :
        HWNDComponentPeer(comp, 
            windowStyleFlags, 
            parent,
            nonRepainting,
            renderingEngine,
            [this](DWORD exStyleFlags)
            {
                if (currentRenderingEngine == direct2DRenderingEngine)
                {
                    exStyleFlags &= ~WS_EX_TRANSPARENT;
                    exStyleFlags &= ~WS_EX_LAYERED;
                    exStyleFlags |= WS_EX_NOREDIRECTIONBITMAP;
                }

                return exStyleFlags;
            })
    {
        updateDirect2DContext();
    }

    ~Direct2DComponentPeer() override
    {
        direct2DContext = nullptr;
    }

    void updateBorderSize() override
    {
        HWNDComponentPeer::updateBorderSize();

        handleDirect2DResize();
    }

    void setAlpha (float newAlpha) override
    {
        if (direct2DContext)
        {
            // nothing to do! already covered by paintEntireComponent
            component.repaint();
            return;
        }

        HWNDComponentPeer::setAlpha(newAlpha);
    }

    void repaint (const Rectangle<int>& area) override
    {
        if (direct2DContext)
        {
            direct2DContext->addDeferredRepaint (area);
            return;
        }

        HWNDComponentPeer::repaint(area);
    }

    void dispatchDeferredRepaints()
    {
        if (direct2DContext)
        {
            return;
        }

        HWNDComponentPeer::dispatchDeferredRepaints();
    }

    void performAnyPendingRepaintsNow() override
    {
        if (direct2DContext)
        {
            // repaint will happen on the next vblank
            return;
        }

        HWNDComponentPeer::performAnyPendingRepaintsNow();
    }

private:
#if JUCE_ETW_TRACELOGGING
    SharedResourcePointer<ETWEventProvider> etwEventProvider;
#endif
    std::unique_ptr<Direct2DLowLevelGraphicsContext> direct2DContext;

    void handlePaintMessage() override
    {
        if (direct2DContext != nullptr)
        {
            direct2DContext->addInvalidWindowRegionToDeferredRepaints();
            return;
        }

        HWNDComponentPeer::handlePaintMessage();
    }

    void swapChainSignaledReady()
    {
        //         vBlankListeners.call ([] (auto& l)
        //                               { l.onVBlank(); });

        if (direct2DContext)
        {
            handleDirect2DPaint();
        }
    }

    void handleDirect2DSwapChainReady()
    {
        vBlankListeners.call ([] (auto& l) { l.onVBlank(); });

        if (direct2DContext)
        {
            handleDirect2DPaint();
        }
    }

    void handleDirect2DPaint()
    {
        jassert (direct2DContext);

        //
        // startFrame returns true if there are any areas to be painted and if the renderer is ready to go
        //
        if (direct2DContext->startFrame())
        {
            handlePaint (*direct2DContext);
            direct2DContext->endFrame();
            return;
        }
    }

    void handleDirect2DResize (int width, int height)
    {
        if (direct2DContext)
        {
            direct2DContext->resize (width, height);
        }
    }

    void handleDirect2DResize()
    {
        if (direct2DContext)
        {
            direct2DContext->resize();
        }
    }

    StringArray getAvailableRenderingEngines() override
    {
        auto engines = HWNDComponentPeer::getAvailableRenderingEngines();

        if (SystemStats::getOperatingSystemType() >= SystemStats::Windows8_1)
            engines.add ("Direct2D");

        return engines;
    }

    void updateDirect2DContext()
    {
        if (currentRenderingEngine == direct2DRenderingEngine && !direct2DContext)
        {
            VBlankDispatcher::getInstance()->removeListener (*this);

            direct2DContext = std::make_unique<Direct2DLowLevelGraphicsContext>(hwnd, this, component.isOpaque());
#if JUCE_DIRECT2D_METRICS
            direct2DContext->stats = paintStats;
#endif
            direct2DContext->setScaleFactor (getPlatformScaleFactor());
        }
    }

    void setCurrentRenderingEngine ([[maybe_unused]] int index) override
    {
        //
        // The WS_EX_NOREDIRECTIONBITMAP flag is required for Direct2D and 
        // can only be configured when the window is created. Changing the renderer requires recreating the window.
        // 
        // Recreate the window asynchronously to avoid an infinite recursive cycle of parentHierarchyChanged -> setCurrentRenderingEngine...
        // 
        if (index != currentRenderingEngine)
        {
            currentRenderingEngine = jlimit (0, getAvailableRenderingEngines().size() - 1, index);
            component.getProperties().set ("Direct2D", currentRenderingEngine == direct2DRenderingEngine);

            Component::SafePointer<Component> safeComponent = &getComponent();
            MessageManager::callAsync ([safeComponent]() {
                if (safeComponent)
                {
                    if (auto peer = safeComponent->getPeer())
                    {
                        auto componentStyleFlags = peer->getStyleFlags();
                        safeComponent->removeFromDesktop();
                        safeComponent->addToDesktop (componentStyleFlags);
                    }
                } });
        }
    }

    LRESULT handleSizeConstraining (RECT& r, const WPARAM wParam) override
    {
        auto result = HWNDComponentPeer::handleSizeConstraining(r, wParam);

        handleDirect2DResize();

        return result;
    }

    //==============================================================================
    LRESULT handleDPIChanging (int newDPI, RECT newRect) override
    {
        auto result = HWNDComponentPeer::handleDPIChanging(newDPI, newRect);

        if (direct2DContext)
        {
            direct2DContext->setScaleFactor(scaleFactor);
        }

        return result;
    }

    LRESULT peerWindowProc (HWND h, UINT message, WPARAM wParam, LPARAM lParam) override
    {
        //DBG ("D2D peerWindowProc " << String::toHexString ((int) message));

        TRACE_LOG_PARENT_WINDOW_MESSAGE(message);

        switch (message)
        {
            case WM_NCCALCSIZE:
            {
                TRACE_LOG_D2D_RESIZE (WM_NCCALCSIZE);
                if (direct2DContext)
                {
                    RECT* rect = (RECT*) lParam;
                    direct2DContext->resize (rect->right - rect->left, rect->bottom - rect->top);
                }
                break;
            }

            case WM_ENTERSIZEMOVE:
            {
                if (direct2DContext)
                {
                    direct2DContext->startResizing();
                }
                break;
            }

            case WM_EXITSIZEMOVE:
            {
                if (direct2DContext)
                {
                    direct2DContext->finishResizing();
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
                        auto status = HWNDComponentPeer::peerWindowProc(h, message, wParam, lParam);

                        handleDirect2DResize();

                        return status;
                    }

                    case SC_MINIMIZE:
                        break;
                }

                break;
            }

            case Direct2DLowLevelGraphicsContext::customMessageID:
            {
                if (direct2DContext)
                {
                    direct2DContext->handleChildWindowChange (wParam);
                    handleDirect2DPaint();
                }
                break;
            }

            default: 
                break;
        }

        return HWNDComponentPeer::peerWindowProc(h, message, wParam, lParam);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Direct2DComponentPeer)
};

ComponentPeer* Component::createNewPeer (int styleFlags, void* parentHWND)
{
    int renderingEngine = getProperties()["Direct2D"] ? Direct2DComponentPeer::direct2DRenderingEngine : HWNDComponentPeer::softwareRenderingEngine;
    return new Direct2DComponentPeer{ *this, styleFlags, (HWND)parentHWND, false, renderingEngine };
}

#endif
