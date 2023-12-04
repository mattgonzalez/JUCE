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
#include "juce_Direct2DHelpers_windows.cpp"

#endif

namespace juce
{

    //==============================================================================

    struct Direct2ImageContext::ImagePimpl : public Direct2DGraphicsContext::Pimpl
    {
    private:
        ComSmartPtr<ID2D1Bitmap1> targetBitmap;
        Point<int> const          origin;

        void adjustPaintAreas(RectangleList<int>& paintAreas) override
        {
            paintAreas = getFrameSize();
        }

        JUCE_DECLARE_WEAK_REFERENCEABLE(ImagePimpl)

    public:
        ImagePimpl(Direct2ImageContext& owner_)
            : Pimpl(owner_, false /* opaque */)
        {
            adapter = DirectX::getInstance()->dxgi.adapters.getDefaultAdapter();
        }

        ~ImagePimpl() override {}

        void setTargetBitmap(ID2D1Bitmap1* targetBitmap_)
        {
            targetBitmap = targetBitmap_;
        }

        void setScaleFactor(float scale_) override
        {
            Pimpl::setScaleFactor(scale_);

            if (deviceResources.deviceContext.context)
            {
                auto dpi = USER_DEFAULT_SCREEN_DPI * dpiScalingFactor;
                deviceResources.deviceContext.context->SetDpi(dpi, dpi);
            }
        }

        Rectangle<int> getFrameSize() override
        {
            if (targetBitmap)
            {
                auto size = targetBitmap->GetSize();
                return Rectangle<float>{ size.width, size.height }.getSmallestIntegerContainer();
            }

            return {};
        }

        ID2D1Image* getDeviceContextTarget() override
        {
            return targetBitmap;
        }

    };

    //==============================================================================

    Direct2ImageContext::Direct2ImageContext(bool clearImage_) :
        pimpl(new ImagePimpl{ *this }),
        clearImage(clearImage_)
    {
    }

    Direct2ImageContext::~Direct2ImageContext()
    {
        endFrame();
    }

    Direct2DGraphicsContext::Pimpl* Direct2ImageContext::getPimpl() const noexcept
    {
        return pimpl.get();
    }

    void Direct2ImageContext::startFrame(ID2D1Bitmap1* bitmap, float dpiScaleFactor)
    {
        pimpl->setTargetBitmap(bitmap);
        pimpl->setScaleFactor(dpiScaleFactor);

        Direct2DGraphicsContext::startFrame();
    }

    void Direct2ImageContext::clearTargetBuffer()
    {
        if (clearImage)
        {
            pimpl->getDeviceContext()->Clear(pimpl->backgroundColor);
        }
    }

} // namespace juce
