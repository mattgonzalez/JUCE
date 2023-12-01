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
#include "juce_DirectX_windows.h"
#include "juce_Direct2DImage_windows.h"

#endif

namespace juce
{

    //==============================================================================
    //
    // Direct2D pixel data
    //

    Direct2DPixelData::Direct2DPixelData(Image::PixelFormat formatToUse, 
        direct2d::DPIScalableArea<int> area_, 
        bool clearImage_,
        DirectX::DXGI::Adapter::Ptr adapter_)
        : ImagePixelData((formatToUse == Image::SingleChannel) ? Image::SingleChannel : Image::ARGB,
            area_.getDeviceIndependentWidth(),
            area_.getDeviceIndependentHeight()),
        adapter(adapter_),
        area(area_.withZeroOrigin()),
        deviceIndependentClipArea(area_.withZeroOrigin().getDeviceIndependentArea()),
        pixelStride((formatToUse == Image::SingleChannel) ? 1 : 4),
        lineStride((pixelStride* jmax(1, width) + 3) & ~3),
        clearImage(clearImage_)
    {
        createTargetBitmap();
    }

    Direct2DPixelData::Direct2DPixelData(ReferenceCountedObjectPtr<Direct2DPixelData> source_, 
        Rectangle<int> clipArea_,
        DirectX::DXGI::Adapter::Ptr adapter_)
        : ImagePixelData(source_->pixelFormat, source_->width, source_->height),
        adapter(adapter_),
        area(source_->area.withZeroOrigin()),
        deviceIndependentClipArea(clipArea_ + source_->deviceIndependentClipArea.getPosition()),
        pixelStride(source_->pixelStride),
        lineStride(source_->lineStride),
        targetBitmap(source_->targetBitmap),
        clearImage(false)
    {
        createTargetBitmap();
    }

    void Direct2DPixelData::createTargetBitmap()
    {
        if (! adapter)
        {
            adapter = DirectX::getInstance()->dxgi.adapters.getDefaultAdapter();
        }

        deviceResources.create(adapter, area.getDPIScalingFactor());

        targetBitmap.create(deviceResources.deviceContext.context, adapter->direct2DDeviceUniqueID, pixelFormat, area, lineStride);
    }

    ReferenceCountedObjectPtr<Direct2DPixelData> Direct2DPixelData::fromDirect2DBitmap(ID2D1Bitmap1* const bitmap, 
        DirectX::DXGI::Adapter::Ptr adapter,
        direct2d::DPIScalableArea<int> area)
    {
        Direct2DPixelData::Ptr pixelData = new Direct2DPixelData{ Image::ARGB, area, false };
        pixelData->targetBitmap.set(bitmap, adapter->direct2DDeviceUniqueID);
        return pixelData;
    }

    std::unique_ptr<LowLevelGraphicsContext> Direct2DPixelData::createLowLevelContext()
    {
        sendDataChangeMessage();

        auto context = std::make_unique<Direct2ImageContext>(clearImage);
        context->startFrame(targetBitmap.get());
        context->setPhysicalPixelScaleFactor(getDPIScalingFactor());
        context->clipToRectangle(deviceIndependentClipArea);
        context->setOrigin(deviceIndependentClipArea.getPosition());
        return context;
    }

    ID2D1Bitmap1* Direct2DPixelData::getTargetBitmap(/*Uuid const& expectedDirect2DDeviceID*/) const noexcept
    {
        return targetBitmap.get(/*expectedDirect2DDeviceID*/);
    }

    void Direct2DPixelData::initialiseBitmapData(Image::BitmapData& bitmap, int x, int y, Image::BitmapData::ReadWriteMode mode)
    {
        x += deviceIndependentClipArea.getX();
        y += deviceIndependentClipArea.getY();

        //
        // Use a mappable Direct2D bitmap to read the contents of the bitmap from the CPU back to the CPU
        //
        // Mapping the bitmap to the CPU means this class can read the pixel data, but the mappable bitmap
        // cannot be a render target
        //
        // So - the Direct2D image low-level graphics context allocates two bitmaps - the target bitmap and the mappable bitmap.
        // initialiseBitmapData copies the contents of the target bitmap to the mappable bitmap, then maps that mappable bitmap to the
        // CPU.
        //
        // Ultimately the data releaser copies the bitmap data from the CPU back to the GPU
        //
        // Target bitmap -> mappable bitmap -> mapped bitmap data -> target bitmap
        //
        bitmap.size = 0;
        bitmap.pixelFormat = pixelFormat;
        bitmap.pixelStride = pixelStride;
        bitmap.data = nullptr;

        if (auto sourceBitmap = getTargetBitmap())
        {
            mappableBitmap.createAndMap(sourceBitmap,
                Rectangle<int>{ x, y, width, height },
                deviceResources.deviceContext.context,
                adapter->direct2DDeviceUniqueID,
                pixelFormat,
                deviceIndependentClipArea,
                area.getDPIScalingFactor(),
                lineStride);
        }

        bitmap.lineStride = mappableBitmap.mappedRect.pitch;
        bitmap.data = mappableBitmap.mappedRect.bits;
        bitmap.size = (size_t)mappableBitmap.mappedRect.pitch * height;

        auto bitmapDataScaledArea = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea({ width, height }, area.getDPIScalingFactor());
        bitmap.width = bitmapDataScaledArea.getPhysicalArea().getWidth();
        bitmap.height = bitmapDataScaledArea.getPhysicalArea().getHeight();

        bitmap.dataReleaser = std::make_unique<Direct2DBitmapReleaser>(*this, mode);

        if (mode != Image::BitmapData::readOnly) sendDataChangeMessage();
    }

    ImagePixelData::Ptr Direct2DPixelData::clone()
    {
        return clip({ width, height });
    }

    ImagePixelData::Ptr Direct2DPixelData::clip(Rectangle<int> sourceArea)
    {
        sourceArea = sourceArea.getIntersection({ width, height });

        auto clipped = new Direct2DPixelData{ pixelFormat,
            direct2d::DPIScalableArea<int>::fromDeviceIndependentArea(sourceArea, area.getDPIScalingFactor()), 
            false, 
            adapter };

        D2D1_POINT_2U destinationPoint{ 0, 0 };
        auto sourceRectU = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea(sourceArea, area.getDPIScalingFactor()).getPhysicalAreaD2DRectU();
        auto hr = clipped->getTargetBitmap()->CopyFromBitmap(&destinationPoint, getTargetBitmap(), &sourceRectU);
        jassertquiet(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
        {
            return clipped;
        }

        return nullptr;
    }

    float Direct2DPixelData::getDPIScalingFactor() const noexcept
    {
        return area.getDPIScalingFactor();
    }

    std::unique_ptr<ImageType> Direct2DPixelData::createType() const
    {
        return std::make_unique<NativeImageType>(area.getDPIScalingFactor());
    }

    Direct2DPixelData::Direct2DBitmapReleaser::Direct2DBitmapReleaser(Direct2DPixelData& pixelData_, Image::BitmapData::ReadWriteMode mode_)
        : pixelData(pixelData_),
        mode(mode_)
    {
    }

    Direct2DPixelData::Direct2DBitmapReleaser::~Direct2DBitmapReleaser()
    {
        pixelData.mappableBitmap.unmap(pixelData.targetBitmap.get(), mode);
    }

    //==============================================================================
    //
    // Direct2D native image type
    //

    ImagePixelData::Ptr NativeImageType::create(Image::PixelFormat format, int width, int height, bool clearImage) const
    {
        auto area = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea({ width, height }, scaleFactor);
        return new Direct2DPixelData{ format, area, clearImage };
    }

} // namespace juce
