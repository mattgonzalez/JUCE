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

struct ID2D1Bitmap1;

namespace juce
{

    //==============================================================================
    //
    // DPIScalableArea keeps track of an area for a window or a bitmap both in
    // terms of device-independent pixels and physical pixels, as well as the DPI
    // scaling factor.
    //

	namespace direct2d
    {
	    template <class valueType>
	    class DPIScalableArea
	    {
	    public:
	        static DPIScalableArea fromDeviceIndependentArea(Rectangle<valueType> dipArea, float dpiScalingFactor)
	        {
	            DPIScalableArea scalableArea;
	            scalableArea.deviceIndependentArea = dipArea;
	            scalableArea.dpiScalingFactor = dpiScalingFactor;
	
	            //
	            // These need to round to the nearest integer, so use roundToInt instead of the standard Rectangle methods
	            //
	            Rectangle<float> physicalArea = dipArea.toFloat() * dpiScalingFactor;
	            scalableArea.physicalArea =
	            {
	                roundToInt(physicalArea.getX()),
	                roundToInt(physicalArea.getY()),
	                roundToInt(physicalArea.getWidth()),
	                roundToInt(physicalArea.getHeight())
	            };
	
	            return scalableArea;
	        }
	
	        static DPIScalableArea fromPhysicalArea(Rectangle<valueType> physicalArea, float dpiScalingFactor)
	        {
	            DPIScalableArea scalableArea;
	            scalableArea.dpiScalingFactor = dpiScalingFactor;
	            scalableArea.physicalArea = physicalArea;
	
	            //
	            // These need to round to the nearest integer, so use roundToInt instead of the standard Rectangle methods
	            //
	            Rectangle<float> dipArea = physicalArea.toFloat() / dpiScalingFactor;
	            scalableArea.deviceIndependentArea =
	            {
	                roundToInt(dipArea.getX()),
	                roundToInt(dipArea.getY()),
	                roundToInt(dipArea.getWidth()),
	                roundToInt(dipArea.getHeight())
	            };
	
	            return scalableArea;
	        }
	
	        bool isEmpty() const noexcept
	        {
	            return deviceIndependentArea.isEmpty();
	        }
	
	        float getDPIScalingFactor() const noexcept
	        {
	            return dpiScalingFactor;
	        }
	
	        auto getDeviceIndependentArea() const noexcept
	        {
	            return deviceIndependentArea;
	        }
	
	        auto getPhysicalArea() const noexcept
	        {
	            return physicalArea;
	        }
	
	        D2D1_RECT_U getPhysicalAreaD2DRectU() const noexcept
	        {
	            return { (UINT32)physicalArea.getX(), (UINT32)physicalArea.getY(), (UINT32)physicalArea.getRight(), (UINT32)physicalArea.getBottom() };
	        }
	
	        D2D_SIZE_U getPhysicalAreaD2DSizeU() const noexcept
	        {
	            return { (UINT32)physicalArea.getWidth(), (UINT32)physicalArea.getHeight() };
	        }
	
	        valueType getDeviceIndependentWidth() const noexcept { return deviceIndependentArea.getWidth(); };
	
	        valueType getDeviceIndependentHeight() const noexcept { return deviceIndependentArea.getHeight(); };
	
	        void clipToPhysicalArea(Rectangle<valueType> clipArea)
	        {
	            *this = fromPhysicalArea(physicalArea.getIntersection(clipArea), dpiScalingFactor);
	        }
	
	        DPIScalableArea withZeroOrigin() const noexcept
	        {
	            return DPIScalableArea
	            {
	                deviceIndependentArea.withZeroOrigin(),
	                physicalArea.withZeroOrigin(),
	                dpiScalingFactor
	            };
	        }
	
	    private:
	        DPIScalableArea() = default;
	
	        DPIScalableArea(Rectangle<valueType> deviceIndependentArea_,
	            Rectangle<valueType> physicalArea_,
	            float dpiScalingFactor_) :
	            deviceIndependentArea(deviceIndependentArea_),
	            physicalArea(physicalArea_),
	            dpiScalingFactor(dpiScalingFactor_)
	        {
	        }
	
	        Rectangle<valueType> deviceIndependentArea;
	        Rectangle<valueType> physicalArea;
	        float dpiScalingFactor = 1.0f;
	    };
    }

class Direct2DPixelData : public ImagePixelData
{
public:
    Direct2DPixelData(Image::PixelFormat formatToUse, direct2d::DPIScalableArea<int> area_, bool clearImage_, DirectX::DXGI::Adapter::Ptr adapter_ = nullptr);
    Direct2DPixelData(ReferenceCountedObjectPtr<Direct2DPixelData> source_, Rectangle<int> clipArea_, DirectX::DXGI::Adapter::Ptr adapter_ = nullptr);

    static ReferenceCountedObjectPtr<Direct2DPixelData> fromDirect2DBitmap(ID2D1Bitmap1* const bitmap, 
		DirectX::DXGI::Adapter::Ptr adapter, 
		direct2d::DPIScalableArea<int> area);

    ~Direct2DPixelData() override = default;

	ID2D1Bitmap1* getTargetBitmap() const noexcept;

    std::unique_ptr<LowLevelGraphicsContext> createLowLevelContext() override;
    
    void initialiseBitmapData(Image::BitmapData& bitmap, int x, int y, Image::BitmapData::ReadWriteMode mode) override;

    ImagePixelData::Ptr clone() override;

    ImagePixelData::Ptr clip(Rectangle<int> clipArea);

    float getDPIScalingFactor() const noexcept;

    std::unique_ptr<ImageType> createType() const override;

    class Direct2DBitmapReleaser : public Image::BitmapData::BitmapDataReleaser
    {
    public:
        Direct2DBitmapReleaser(Direct2DPixelData& pixelData_, Image::BitmapData::ReadWriteMode mode_);
        ~Direct2DBitmapReleaser() override;

    private:
        Direct2DPixelData& pixelData;
        Image::BitmapData::ReadWriteMode mode;
    };

    using Ptr = ReferenceCountedObjectPtr<Direct2DPixelData>;

    Rectangle<int> const deviceIndependentClipArea;

private:
    DirectX::DXGI::Adapter::Ptr adapter;
    direct2d::DeviceResources deviceResources;
    direct2d::DPIScalableArea<int> area;
    const int                 pixelStride, lineStride;
    bool const                clearImage;

    class TargetBitmap : public direct2d::Direct2DBitmap
    {
    public:
        void create(ID2D1DeviceContext1* deviceContext_,
            Image::PixelFormat format,
            direct2d::DPIScalableArea<int> area_,
            int lineStride)
        {
			createBitmap(deviceContext_,
                format,
                area_.getPhysicalAreaD2DSizeU(),
                lineStride,
                area_.getDPIScalingFactor(),
                D2D1_BITMAP_OPTIONS_TARGET);
            if (bitmap)
            {
                //
                // The bitmap may be slightly too large due
                // to DPI scaling, so fill it with transparent black
                //
                deviceContext_->SetTarget(bitmap);
                deviceContext_->BeginDraw();
                deviceContext_->Clear();
                deviceContext_->EndDraw();
                deviceContext_->SetTarget(nullptr);
            }
        }
    } targetBitmap;

    class MappableBitmap : public direct2d::Direct2DBitmap
    {
    public:
        ID2D1Bitmap* createAndMap(ID2D1Bitmap1* sourceBitmap,
            Rectangle<int> sourceRectangle,
            ID2D1DeviceContext1* deviceContext_,
            Image::PixelFormat format,
			Rectangle<int> deviceIndependentClipArea,
			float dpiScaleFactor,
            int lineStride)
        {
			createBitmap(deviceContext_,
                format,
				sourceBitmap->GetPixelSize(),
                lineStride,
                dpiScaleFactor,
                D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);

            if (bitmap)
            {
                D2D1_POINT_2U destPoint{ 0, 0 };
                sourceRectangle = sourceRectangle.getIntersection(deviceIndependentClipArea);
                auto scaledSourceRect = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea(sourceRectangle, dpiScaleFactor);
                auto sourceRectU = scaledSourceRect.getPhysicalAreaD2DRectU();

                //
                // Copy from the painted target bitmap to the mappable bitmap
                //
                if (auto hr = bitmap->CopyFromBitmap(&destPoint, sourceBitmap, &sourceRectU); FAILED(hr))
                {
                    return nullptr;
                }

                //
                // Map the mappable bitmap to CPU memory; ID2D1Bitmap::Map will allocate memory and populate mappedRect
                //
                mappedRect = {};
                if (auto hr = bitmap->Map(D2D1_MAP_OPTIONS_READ, &mappedRect); FAILED(hr))
                {
                    return nullptr;
                }
            }

            return bitmap;
        }

		void unmap(ID2D1Bitmap1* targetBitmap, Image::BitmapData::ReadWriteMode mode)
		{
            //
			// Unmap the mappable bitmap if it was mapped
			//
			// If the mappable bitmap was mapped, copy the mapped bitmap data to the target bitmap
			//
            if (mappedRect.bits && bitmap)
            {
                if (targetBitmap && mode != Image::BitmapData::readOnly)
                {
                    auto size = bitmap->GetPixelSize();

                    D2D1_RECT_U rect{ 0, 0, size.width, size.height };
                    targetBitmap->CopyFromMemory(&rect, mappedRect.bits, mappedRect.pitch);
                }

                bitmap->Unmap();
            }

            mappedRect = {};
		}

        D2D1_MAPPED_RECT          mappedRect{};
    } mappableBitmap;

	void createTargetBitmap();

    JUCE_LEAK_DETECTOR(Direct2DPixelData)
};

} // namespace juce
