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

#endif

namespace juce
{

    //==============================================================================

    struct Direct2ImageContext::ImagePimpl : public Direct2DGraphicsContext::Pimpl

    {
    private:
        Direct2DPixelData::Ptr    direct2DPixelData;
        Point<int> const          origin;
        RectangleList<int> const& initialClip;

        HRESULT prepare() override
        {
            HRESULT hr = S_OK;

            if (hr = Pimpl::prepare(); FAILED(hr))
            {
                return hr;
            }

            float dpiX = USER_DEFAULT_SCREEN_DPI;
            float dpiY = USER_DEFAULT_SCREEN_DPI;
            deviceResources.deviceContext.context->GetDpi(&dpiX, &dpiY);

            D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
            bitmapProperties.dpiX = dpiX;
            bitmapProperties.dpiY = dpiY;
            bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            bitmapProperties.pixelFormat.format =
                (direct2DPixelData->pixelFormat == Image::SingleChannel) ? DXGI_FORMAT_A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;

            if (!direct2DPixelData->targetBitmap)
            {
                bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
                hr = deviceResources.deviceContext.context->CreateBitmap(
                    D2D1_SIZE_U{ (uint32)direct2DPixelData->width, (uint32)direct2DPixelData->height },
                    nullptr,
                    direct2DPixelData->lineStride,
                    bitmapProperties,
                    direct2DPixelData->targetBitmap.resetAndGetPointerAddress());

                direct2DPixelData->mappableBitmap = nullptr;
            }

            if (!direct2DPixelData->mappableBitmap)
            {
                bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
                hr = deviceResources.deviceContext.context->CreateBitmap(
                    D2D1_SIZE_U{ (uint32)direct2DPixelData->width, (uint32)direct2DPixelData->height },
                    nullptr,
                    direct2DPixelData->lineStride,
                    bitmapProperties,
                    direct2DPixelData->mappableBitmap.resetAndGetPointerAddress());
            }

            jassert(SUCCEEDED(hr));

            return hr;
        }

        void teardown() override
        {
            Pimpl::teardown();

            direct2DPixelData->mappableBitmap = nullptr;
            direct2DPixelData->targetBitmap = nullptr;
        }

        void adjustPaintAreas(RectangleList<int>& paintAreas) override
        {
            paintAreas = getFrameSize();
        }

        Rectangle<int> getFrameSize() override
        {
            return { direct2DPixelData->width, direct2DPixelData->height };
        }

        ID2D1Image* const getDeviceContextTarget()
        {
            return direct2DPixelData->targetBitmap;
        }

        JUCE_DECLARE_WEAK_REFERENCEABLE(ImagePimpl)

    public:
        ImagePimpl(Direct2ImageContext& owner_,
            Direct2DPixelData::Ptr                direct2DPixelData_,
            Point<int>                            origin_,
            const RectangleList<int>& initialClip_)
            : Pimpl(owner_, 1.0, false /* opaque */),
            direct2DPixelData(direct2DPixelData_),
            origin(origin_),
            initialClip(initialClip_)
        {
            adapter = factories->getDefaultAdapter();
        }

        ~ImagePimpl() override {}
    };

    //==============================================================================
    Direct2ImageContext::Direct2ImageContext(Direct2DPixelData::Ptr direct2DPixelData_)
        : Direct2ImageContext(direct2DPixelData_,
            Point<int> {},
            Rectangle<int> { direct2DPixelData_->width, direct2DPixelData_->height })
    {
    }

    Direct2ImageContext::Direct2ImageContext(Direct2DPixelData::Ptr    direct2DPixelData_,
        Point<int>                origin,
        const RectangleList<int>& initialClip,
        bool                      clearImage_)
        : clearImage(clearImage_),
        pimpl(new ImagePimpl{ *this, direct2DPixelData_, origin, initialClip })
    {
        startFrame();
    }

    Direct2ImageContext::~Direct2ImageContext()
    {
        endFrame();
    }

    Direct2DGraphicsContext::Pimpl* const Direct2ImageContext::getPimpl() const noexcept
    {
        return pimpl.get();
    }

    void Direct2ImageContext::clearTargetBuffer()
    {
        if (clearImage)
        {
            pimpl->getDeviceContext()->Clear(pimpl->backgroundColor);
        }
    }

} // namespace juce
