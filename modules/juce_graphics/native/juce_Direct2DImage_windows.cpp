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
    //
    // Direct2D native pixel data
    //

    class Direct2DPixelData : public ImagePixelData
    {
    public:
        Direct2DPixelData(Image::PixelFormat formatToUse, direct2d::DPIScalableArea<int> area_, bool clearImage_)
            : ImagePixelData((formatToUse == Image::SingleChannel) ? Image::SingleChannel : Image::ARGB,
                area_.getDeviceIndependentWidth(),
                area_.getDeviceIndependentHeight()),
            area(area_),
            deviceIndependentClipArea(area_.getDeviceIndependentArea()),
            pixelStride((formatToUse == Image::SingleChannel) ? 1 : 4),
            lineStride((pixelStride* jmax(1, width) + 3) & ~3),
            clearImage(clearImage_)
        {
        }

        Direct2DPixelData(ReferenceCountedObjectPtr<Direct2DPixelData> source_, Rectangle<int> clipArea_)
            : ImagePixelData(source_->pixelFormat, source_->width, source_->height),
            area(source_->area),
            deviceIndependentClipArea(clipArea_ + source_->deviceIndependentClipArea.getPosition()),
            pixelStride(source_->pixelStride),
            lineStride(source_->lineStride),
            targetBitmap(source_->targetBitmap),
            clearImage(false)
        {
        }

        static Direct2DPixelData::Ptr fromDirect2DBitmap(ID2D1Bitmap1* const bitmap, direct2d::DPIScalableArea<int> area)
        {
            Direct2DPixelData::Ptr pixelData = new Direct2DPixelData{ Image::ARGB, area, false };
            pixelData->targetBitmap = bitmap;
            return pixelData;
        }

        ~Direct2DPixelData() override = default;

        std::unique_ptr<LowLevelGraphicsContext> createLowLevelContext() override
        {
            sendDataChangeMessage();

            auto context = std::make_unique<Direct2ImageContext>(this, deviceIndependentClipArea.getPosition(), RectangleList<int> { deviceIndependentClipArea }, clearImage);
            context->clipToRectangle(deviceIndependentClipArea);
            context->setOrigin(deviceIndependentClipArea.getPosition());
            return context;
        }

        void initialiseBitmapData(Image::BitmapData& bitmap, int x, int y, Image::BitmapData::ReadWriteMode mode) override
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

            if (mappedRect.bits == nullptr)
            {
                if (!targetBitmap || !mappableBitmap)
                {
                    //
                    // The low-level graphics context creates the bitmaps
                    //
                    createLowLevelContext();
                }

                jassert(mappableBitmap);

                if (mappableBitmap)
                {
                    D2D1_POINT_2U destPoint{ 0, 0 };
                    Rectangle<int> dipSourceRect{ x, y, width, height };
                    dipSourceRect = dipSourceRect.getIntersection(deviceIndependentClipArea);
                    auto scaledSourceRect = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea(dipSourceRect, area.getDPIScalingFactor());
                    auto sourceRectU = scaledSourceRect.getPhysicalAreaD2DRectU();

                    //
                    // Copy from the painted target bitmap to the mappable bitmap
                    //
                    if (auto hr = mappableBitmap->CopyFromBitmap(&destPoint, targetBitmap, &sourceRectU); FAILED(hr))
                    {
                        return;
                    }

                    //
                    // Map the mappable bitmap to CPU memory; ID2D1Bitmap::Map will allocate memory and populate mappedRect
                    //
                    mappedRect = {};
                    if (auto hr = mappableBitmap->Map(D2D1_MAP_OPTIONS_READ, &mappedRect); FAILED(hr))
                    {
                        return;
                    }
                }
            }

            bitmap.lineStride = mappedRect.pitch;
            bitmap.data = mappedRect.bits;
            bitmap.size = (size_t)mappedRect.pitch * height;

            auto bitmapDataScaledArea = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea({ width, height }, area.getDPIScalingFactor());
            bitmap.width = bitmapDataScaledArea.getPhysicalArea().getWidth();
            bitmap.height= bitmapDataScaledArea.getPhysicalArea().getHeight();

            bitmap.dataReleaser = std::make_unique<Direct2DBitmapReleaser>(*this, mode);

            if (mode != Image::BitmapData::readOnly) sendDataChangeMessage();
        }

        ImagePixelData::Ptr clone() override
        {
            Direct2DPixelData::Ptr clone = new Direct2DPixelData{ pixelFormat, area, false };

            clone->createLowLevelContext();

            D2D1_POINT_2U destinationPoint{ 0, 0 };
            auto sourceRectU = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea(deviceIndependentClipArea, area.getDPIScalingFactor()).getPhysicalAreaD2DRectU();
            auto hr = clone->targetBitmap->CopyFromBitmap(&destinationPoint, targetBitmap, &sourceRectU);
            jassertquiet(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
                return clone;
            }

            return nullptr;
        }

        ImagePixelData::Ptr clip(Rectangle<int> clipArea)
        {
            return new Direct2DPixelData{ this, clipArea };
        }

        float getDPIScalingFactor() const noexcept
        {
            return area.getDPIScalingFactor();
        }

        std::unique_ptr<ImageType> createType() const override
        {
            return std::make_unique<NativeImageType>(area.getDPIScalingFactor());
        }

        class Direct2DBitmapReleaser : public Image::BitmapData::BitmapDataReleaser
        {
        public:
            Direct2DBitmapReleaser(Direct2DPixelData& pixelData_, Image::BitmapData::ReadWriteMode mode_)
                : pixelData(pixelData_),
                mode(mode_)
            {
            }

            ~Direct2DBitmapReleaser() override
            {
                //
                // Unmap the mappable bitmap if it was mapped
                //
                // If the mappable bitmap was mapped, copy the mapped bitmap data to the target bitmap
                //
                if (pixelData.mappedRect.bits && pixelData.mappableBitmap)
                {
                    if (pixelData.targetBitmap && mode != Image::BitmapData::readOnly)
                    {
                        auto size = pixelData.mappableBitmap->GetPixelSize();

                        D2D1_RECT_U rect{ 0, 0, size.width, size.height };
                        pixelData.targetBitmap->CopyFromMemory(&rect, pixelData.mappedRect.bits, pixelData.mappedRect.pitch);
                    }

                    pixelData.mappableBitmap->Unmap();
                }

                pixelData.mappedRect = {};
            }

        private:
            Direct2DPixelData& pixelData;
            Image::BitmapData::ReadWriteMode mode;
        };

        using Ptr = ReferenceCountedObjectPtr<Direct2DPixelData>;

        Rectangle<int> const deviceIndependentClipArea;

    private:
        friend class Direct2DGraphicsContext;
        friend class Direct2DImageContext;
        friend struct Direct2ImageContext::ImagePimpl;

        direct2d::DPIScalableArea<int> area;
        const int                 pixelStride, lineStride;
        bool const                clearImage;
        ComSmartPtr<ID2D1Bitmap1> targetBitmap;
        ComSmartPtr<ID2D1Bitmap1> mappableBitmap;
        D2D1_MAPPED_RECT          mappedRect{};
        Uuid                      direct2DDeviceUniqueID = Uuid::null();

        JUCE_LEAK_DETECTOR(Direct2DPixelData)
    };

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
