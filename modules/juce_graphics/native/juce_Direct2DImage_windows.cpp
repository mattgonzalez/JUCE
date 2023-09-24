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
    // Direct2D native image type
    //

    class Direct2DPixelData : public ImagePixelData
    {
    public:
        Direct2DPixelData(Image::PixelFormat formatToUse, int w, int h, bool clearImage_, float dpiScalingFactor_)
            : ImagePixelData((formatToUse == Image::SingleChannel) ? Image::SingleChannel : Image::ARGB, w, h),
            pixelStride(4),
            lineStride((pixelStride* jmax(1, w) + 3) & ~3),
            clearImage(clearImage_),
            dpiScalingFactor(dpiScalingFactor_)
        {
        }

        Direct2DPixelData(Image::PixelFormat formatToUse, int w, int h, bool clearImage_)
            : Direct2DPixelData(formatToUse, w, h, clearImage_, 1.0f)
        {
        }

        static Direct2DPixelData::Ptr fromDirect2DBitmap(ID2D1Bitmap1* const bitmap, Rectangle<int> sourceArea)
        {
            float dpiX, dpiY;
            bitmap->GetDpi(&dpiX, &dpiY);

            Direct2DPixelData::Ptr pixelData = new Direct2DPixelData{ Image::ARGB, static_cast<int>(sourceArea.getWidth()), static_cast<int>(sourceArea.getHeight()), false, jmax(dpiX, dpiY) / (float)USER_DEFAULT_SCREEN_DPI};
            auto context = pixelData->createLowLevelContext();
            D2D1_POINT_2U p{ 0, 0 };
            D2D1_RECT_U rect{ (uint32)sourceArea.getX(), (uint32)sourceArea.getY(), (uint32)sourceArea.getRight(), (uint32)sourceArea.getBottom() };
            pixelData->targetBitmap->CopyFromBitmap(&p, bitmap, &rect);

            return pixelData;
        }

        ~Direct2DPixelData() override {}

        std::unique_ptr<LowLevelGraphicsContext> createLowLevelContext() override
        {
            sendDataChangeMessage();
            return std::make_unique<Direct2ImageContext>(this, Point<int> {}, RectangleList<int> { { width, height } }, clearImage);
        }

        void initialiseBitmapData(Image::BitmapData& bitmap, int x, int y, Image::BitmapData::ReadWriteMode mode) override
        {
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
                if (!targetBitmap)
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
                    D2D1_RECT_U   sourceRect{ (uint32)x, (uint32)y, (uint32)(x + width), (uint32)(y + height) };

                    //
                    // Copy from the painted target bitmap to the mappable bitmap
                    //
                    if (auto hr = mappableBitmap->CopyFromBitmap(&destPoint, targetBitmap, &sourceRect); FAILED(hr))
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
            bitmap.size = mappedRect.pitch * height;

            bitmap.dataReleaser = std::make_unique<Direct2DBitmapReleaser>(*this, mode);

            if (mode != Image::BitmapData::readOnly) sendDataChangeMessage();
        }

        ImagePixelData::Ptr clone() override
        {
            Direct2DPixelData::Ptr clone = new Direct2DPixelData{ pixelFormat, width, height, false, dpiScalingFactor };

            clone->createLowLevelContext();

            D2D1_POINT_2U point{ 0, 0 };
            D2D1_RECT_U   rect{ 0, 0, (uint32)width, (uint32)height };
            auto          hr = clone->targetBitmap->CopyFromBitmap(&point, targetBitmap, &rect);
            jassertquiet(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
                return clone;
            }

            return nullptr;
        }

        std::unique_ptr<ImageType> createType() const override
        {
            return std::make_unique<NativeImageType>();
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
                        D2D1_RECT_U rect{ 0, 0, (uint32)pixelData.width, (uint32)pixelData.height };

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

    private:
        friend class Direct2DGraphicsContext;
        friend class Direct2DImageContext;
        friend struct Direct2ImageContext::ImagePimpl;

        float const dpiScalingFactor;
        const int                 pixelStride, lineStride;
        bool const                clearImage;
        Rectangle<int>            targetBitmapSize;
        ComSmartPtr<ID2D1Bitmap1> targetBitmap;
        ComSmartPtr<ID2D1Bitmap1> mappableBitmap;
        D2D1_MAPPED_RECT          mappedRect{};

        // keep a reference to the DirectXFactories to retain the DLLs & factories
        SharedResourcePointer<DirectXFactories> factories;

        JUCE_LEAK_DETECTOR(Direct2DPixelData)
    };

    ImagePixelData::Ptr NativeImageType::create(Image::PixelFormat format, int width, int height, bool clearImage) const
    {
        return new Direct2DPixelData{ format, width, height, clearImage };
    }

    juce::ImagePixelData::Ptr Direct2DImageType::create(Image::PixelFormat format, int width, int height, bool shouldClearImage) const
    {
        width = detail::ceilAsInt(width * dpiScalingFactor);
        height = detail::ceilAsInt(height * dpiScalingFactor);
        return new Direct2DPixelData{ format, width, height, shouldClearImage, dpiScalingFactor };
    }

} // namespace juce
