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

    struct Direct2DImageContext::ImagePimpl : public Direct2DGraphicsContext::Pimpl

    {
    private:
        Direct2DPixelData::Ptr    direct2DPixelData;
        Point<int> const          origin;
        Rectangle<int> frameSizePhysicalPixels;
        RectangleList<int> const& initialClip;

        HRESULT prepare() override
        {
            HRESULT hr = S_OK;

            if (hr = Pimpl::prepare(); FAILED(hr))
            {
                return hr;
            }

#if 0
            auto& deviceContext = deviceResources.deviceContext.context;

            D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
            bitmapProperties.dpiX = dpiScalingFactor * USER_DEFAULT_SCREEN_DPI;;
            bitmapProperties.dpiY = bitmapProperties.dpiX;
            bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            bitmapProperties.pixelFormat.format =
                (direct2DPixelData->pixelFormat == Image::SingleChannel) ? DXGI_FORMAT_A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;

            frameSizePhysicalPixels =
            {
                detail::ceilAsInt(direct2DPixelData->width * dpiScalingFactor),
                detail::ceilAsInt(direct2DPixelData->height * dpiScalingFactor)
            };

            D2D_SIZE_U size
            {
                static_cast<uint32>(frameSizePhysicalPixels.getWidth()),
                static_cast<uint32>(frameSizePhysicalPixels.getHeight())
            };

            if (!direct2DPixelData->targetBitmap)
            {
                bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
                hr = deviceContext->CreateBitmap(
                    size,
                    nullptr,
                    direct2DPixelData->lineStride,
                    bitmapProperties,
                    direct2DPixelData->targetBitmap.resetAndGetPointerAddress());

                if (SUCCEEDED(hr))
                {
                    //
                    // The bitmap may be slightly too large due
                    // to DPI scaling, so fill it with transparent black
                    //
                    deviceContext->SetTarget(direct2DPixelData->targetBitmap);
                    deviceContext->BeginDraw();
                    deviceContext->Clear();
                    deviceContext->EndDraw();
                    deviceContext->SetTarget(nullptr);

                    //
                    // Store the unique ID for the Direct2D device
                    //
                    direct2DPixelData->direct2DDeviceUniqueID = getDirect2DDeviceUniqueID();
                }

                direct2DPixelData->mappableBitmap = nullptr;
            }

            if (!direct2DPixelData->mappableBitmap)
            {
                bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
                hr = deviceContext->CreateBitmap(
                    size,
                    nullptr,
                    direct2DPixelData->lineStride,
                    bitmapProperties,
                    direct2DPixelData->mappableBitmap.resetAndGetPointerAddress());
            }

            jassert(SUCCEEDED(hr));

#endif

            return hr;
        }

        void teardown() override
        {
            Pimpl::teardown();

//             direct2DPixelData->mappableBitmap = nullptr;
//             direct2DPixelData->targetBitmap = nullptr;
        }

        void adjustPaintAreas(RectangleList<int>& paintAreas) override
        {
            paintAreas = getFrameSize();
        }

        JUCE_DECLARE_WEAK_REFERENCEABLE(ImagePimpl)

    public:
        ImagePimpl(Direct2DImageContext& owner_,
            Direct2DPixelData::Ptr                direct2DPixelData_,
            Point<int>                            origin_,
            const RectangleList<int>& initialClip_)
            : Pimpl(owner_, false /* opaque */),
            direct2DPixelData(direct2DPixelData_),
            origin(origin_),
            initialClip(initialClip_)
        {
            adapter = DirectX::getInstance()->dxgi.adapters.getDefaultAdapter();

            prepare();
        }

        ~ImagePimpl() override {}

        Rectangle<int> getFrameSize() override
        {
            return { direct2DPixelData->width, direct2DPixelData->height };
        }

        ID2D1Image* getDeviceContextTarget() override
        {
            return direct2DPixelData->getTargetBitmap(getDirect2DDeviceUniqueID());
        }
    };

    //==============================================================================

    class Direct2DImageContext::Bitmap
    {
    public:
        Bitmap(ID2D1Bitmap1* bitmap_, Uuid direct2DDeviceID_) :
            bitmap(bitmap_),
            direct2DDeviceID(direct2DDeviceID_)
        {
        }

        ComSmartPtr<ID2D1Bitmap1> bitmap;
        Uuid const direct2DDeviceID;
    };

    Direct2DImageContext::Bitmap Direct2DImageContext::createBitmap(Image::PixelFormat format, Rectangle<int> size, float dpiScaleFactor, int lineStride)
    {
        D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
        bitmapProperties.dpiX = dpiScaleFactor * USER_DEFAULT_SCREEN_DPI;;
        bitmapProperties.dpiY = bitmapProperties.dpiX;
        bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bitmapProperties.pixelFormat.format = format == Image::SingleChannel ? DXGI_FORMAT_A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
        bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;

        D2D_SIZE_U sizeU{ (uint32)size.getWidth(), (uint32)size.getHeight() };

        ComSmartPtr<ID2D1Bitmap1> bitmap;
        auto hr = getPimpl()->getDeviceContext()->CreateBitmap(
            sizeU,
            nullptr,
            lineStride,
            bitmapProperties,
            bitmap.resetAndGetPointerAddress());
        jassertquiet(SUCCEEDED(hr));
        return { bitmap, getPimpl()->getDirect2DDeviceUniqueID() };
    }

    //==============================================================================
    Direct2DImageContext::Direct2DImageContext(Direct2DPixelData::Ptr direct2DPixelData_)
        : Direct2DImageContext(direct2DPixelData_,
            Point<int> {},
            Rectangle<int> { direct2DPixelData_->width, direct2DPixelData_->height })
    {
    }

    Direct2DImageContext::Direct2DImageContext(Direct2DPixelData::Ptr    direct2DPixelData_,
        Point<int>                origin,
        const RectangleList<int>& initialClip,
        bool                      clearImage_)
        : clearImage(clearImage_),
        pimpl(new ImagePimpl{ *this, direct2DPixelData_, origin, initialClip })
    {
        setPhysicalPixelScaleFactor(direct2DPixelData_->getDPIScalingFactor());
    }

    Direct2DImageContext::~Direct2DImageContext()
    {
        endFrame();
    }

    Direct2DGraphicsContext::Pimpl* Direct2DImageContext::getPimpl() const noexcept
    {
        return pimpl.get();
    }

    void Direct2DImageContext::clearTargetBuffer()
    {
        if (clearImage)
        {
            pimpl->getDeviceContext()->Clear(pimpl->backgroundColor);
        }
    }

} // namespace juce
