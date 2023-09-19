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
    Direct2DPixelData (Image::PixelFormat formatToUse, int w, int h, bool clearImage)
        : ImagePixelData ((formatToUse == Image::SingleChannel) ? Image::SingleChannel : Image::ARGB, w, h),
          pixelStride (4),
          lineStride ((pixelStride * jmax (1, w) + 3) & ~3),
        imageDataSize((size_t)lineStride* (size_t)jmax(1, h))
    {
    }

    ~Direct2DPixelData() override {}

    std::unique_ptr<LowLevelGraphicsContext> createLowLevelContext() override
    {
        sendDataChangeMessage();
        return std::make_unique<Direct2DLowLevelGraphicsImageContext> (this, Point<int> {}, RectangleList<int> { { width, height } });
    }

    void initialiseBitmapData (Image::BitmapData& bitmap, int x, int y, Image::BitmapData::ReadWriteMode mode) override
    {
        bitmap.size        = imageDataSize;
        bitmap.pixelFormat = pixelFormat;
        bitmap.pixelStride = pixelStride;
        bitmap.data = nullptr;

        if (mappedRect.bits == nullptr)
        {
            if (!targetBitmap)
            {
                createLowLevelContext();
            }

            jassert(mappableBitmap);

            if (mappableBitmap)
            {
                D2D1_POINT_2U destPoint { 0, 0 };
                D2D1_RECT_U   sourceRect { (uint32)x, (uint32)y, (uint32) (width - x), (uint32) (height - y) };

                //
                // Copy from the painted target bitmap to the mappable bitmap
                //
                if (auto hr = mappableBitmap->CopyFromBitmap (&destPoint, targetBitmap, &sourceRect); FAILED(hr))
                {
                    return;
                }

                //
                // Map the mappable bitmap to CPU memory; ID2D1Bitmap::Map will allocate memory and populate mappedRect
                //
                mappedRect = {};
                if (auto hr = mappableBitmap->Map (D2D1_MAP_OPTIONS_READ, &mappedRect); FAILED (hr))
                {
                    return;
                }
            }
        }

        bitmap.lineStride = mappedRect.pitch;
        bitmap.data       = mappedRect.bits;

        bitmap.dataReleaser = std::make_unique<Direct2DBitmapReleaser> (*this, mode);

        if (mode != Image::BitmapData::readOnly) sendDataChangeMessage();
    }

    ImagePixelData::Ptr clone() override
    {
        jassertfalse;
        return nullptr;
        //         auto s = new SoftwarePixelData (pixelFormat, width, height, false);
        //         memcpy (s->imageData, imageData, (size_t) lineStride * (size_t) height);
        //         return *s;
    }

    std::unique_ptr<ImageType> createType() const override
    {
        return std::make_unique<NativeImageType>();
    }

    class Direct2DBitmapReleaser : public Image::BitmapData::BitmapDataReleaser
    {
    public:
        Direct2DBitmapReleaser (Direct2DPixelData& pixelData_, Image::BitmapData::ReadWriteMode mode_)
            : pixelData (pixelData_), mode(mode_)
        {
        }

        ~Direct2DBitmapReleaser() override
        {
            if (pixelData.mappedRect.bits && pixelData.mappableBitmap)
            {
                if (pixelData.targetBitmap && mode != Image::BitmapData::readOnly)
                {
                    D2D1_RECT_U   rect { 0, 0, (uint32) pixelData.width, (uint32) pixelData.height };

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
    friend class Direct2DLowLevelGraphicsHwndContext;
    friend struct Direct2DLowLevelGraphicsImageContext::Pimpl;

    const int        pixelStride, lineStride;
    size_t const imageDataSize;
    ComSmartPtr<ID2D1Bitmap1>   targetBitmap;
    ComSmartPtr<ID2D1Bitmap1> mappableBitmap;
    D2D1_MAPPED_RECT mappedRect {};

    // keep a reference to the DirectXFactories so the bitmap is freed before the DLLs
    SharedResourcePointer<DirectXFactories> factories;

    JUCE_LEAK_DETECTOR (Direct2DPixelData)
};

ImagePixelData::Ptr NativeImageType::create (Image::PixelFormat format, int width, int height, bool clearImage) const
{
    return new Direct2DPixelData { format, width, height, clearImage };
}

//==============================================================================
//
// Saved state struct to handle saveState() and restoreState()
//

struct Direct2DLowLevelGraphicsImageContext::SavedState
{
private:
    //==============================================================================
    //
    // PushedLayer represents a Direct2D clipping or transparency layer
    //
    // D2D layers have to be pushed into the device context. Every push has to be
    // matched with a pop.
    //
    // D2D has special layers called "axis aligned clip layers" which clip to an
    // axis-aligned rectangle. Pushing an axis-aligned clip layer must be matched
    // with a call to deviceContext->PopAxisAlignedClip() in the reverse order
    // in which the layers were pushed.
    //
    // So if the pushed layer stack is built like this:
    //
    // PushLayer()
    // PushLayer()
    // PushAxisAlignedClip()
    // PushLayer()
    //
    // the layer stack must be popped like this:
    //
    // PopLayer()
    // PopAxisAlignedClip()
    // PopLayer()
    // PopLayer()
    //
    // PushedLayer, PushedAxisAlignedClipLayer, and LayerPopper all exist just to unwind the
    // layer stack accordingly.
    //
    struct PushedLayer
    {
        PushedLayer()               = default;
        PushedLayer (PushedLayer&&) = default;

        void pop (ID2D1DeviceContext* deviceContext)
        {
            deviceContext->PopLayer();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PushedLayer)
    };

    struct PushedAxisAlignedClipLayer
    {
        PushedAxisAlignedClipLayer()                              = default;
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
    SavedState (Rectangle<int> frameSize_, ComSmartPtr<ID2D1SolidColorBrush>& colourBrush_, direct2d::DeviceContext& deviceContext_)
        : deviceContext (deviceContext_),
          clipRegion (frameSize_),
          colourBrush (colourBrush_)
    {
        currentBrush = colourBrush;
    }

    //
    // Constructor for subsequent entries
    //
    SavedState (SavedState const* const previousState_)
        : currentTransform (previousState_->currentTransform),
          deviceContext (previousState_->deviceContext),
          clipRegion (previousState_->clipRegion),
          font (previousState_->font),
          currentBrush (previousState_->currentBrush),
          colourBrush (previousState_->colourBrush),
          bitmapBrush (previousState_->bitmapBrush),
          linearGradient (previousState_->linearGradient),
          radialGradient (previousState_->radialGradient),
          gradientStops (previousState_->gradientStops),
          fillType (previousState_->fillType),
          interpolationMode (previousState_->interpolationMode)
    {
    }

    ~SavedState()
    {
        jassert (pushedLayers.size() == 0);
        clearFill();
    }

    void pushLayer (const D2D1_LAYER_PARAMETERS& layerParameters)
    {
        //
        // Clipping and transparency are all handled by pushing Direct2D layers. The SavedState creates an internal stack
        // of Layer objects to keep track of how many layers need to be popped.
        //
        // Pass nullptr for the PushLayer layer parameter to allow Direct2D to manage the layers (Windows 8 or later)
        //
        deviceContext.resetTransform();
        deviceContext.context->PushLayer (layerParameters, nullptr);

        pushedLayers.push (PushedLayer {});
    }

    void pushGeometryClipLayer (ComSmartPtr<ID2D1Geometry> geometry)
    {
        if (geometry != nullptr)
        {
            pushLayer (D2D1::LayerParameters (D2D1::InfiniteRect(), geometry));
        }
    }

    void pushTransformedRectangleGeometryClipLayer (ComSmartPtr<ID2D1RectangleGeometry> geometry, AffineTransform const& transform)
    {
        jassert (geometry != nullptr);
        auto layerParameters          = D2D1::LayerParameters (D2D1::InfiniteRect(), geometry);
        layerParameters.maskTransform = direct2d::transformToMatrix (transform);
        pushLayer (layerParameters);
    }

    void pushAxisAlignedClipLayer (Rectangle<int> r)
    {
        deviceContext.setTransform (currentTransform.getTransform());
        deviceContext.context->PushAxisAlignedClip (direct2d::rectangleToRectF (r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        pushedLayers.push (PushedAxisAlignedClipLayer {});
    }

    void pushTransparencyLayer (float opacity)
    {
        pushLayer ({ D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity });
    }

    void popLayers()
    {
        LayerPopper layerPopper { deviceContext.context };

        while (pushedLayers.size() > 0)
        {
            auto& pushedLayer = pushedLayers.top();

            std::visit (layerPopper, pushedLayer);

            pushedLayers.pop();
        }
    }

    void popTopLayer()
    {
        LayerPopper layerPopper { deviceContext.context };

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

    void setOpacity (float newOpacity)
    {
        fillType.setOpacity (newOpacity);
    }

    void clearFill()
    {
        gradientStops  = nullptr;
        linearGradient = nullptr;
        radialGradient = nullptr;
        bitmapBrush    = nullptr;
        currentBrush   = nullptr;
    }

    //
    // Translate a JUCE FillType to a Direct2D brush
    //
    void updateCurrentBrush()
    {
        if (fillType.isColour())
        {
            //
            // Reuse the same colour brush
            //
            currentBrush = (ID2D1Brush*) colourBrush;
        }
        else if (fillType.isTiledImage())
        {
            D2D1_BRUSH_PROPERTIES brushProps = { fillType.getOpacity(), direct2d::transformToMatrix (fillType.transform) };
            auto                  bmProps    = D2D1::BitmapBrushProperties (D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP);

            auto image = fillType.image;

            D2D1_SIZE_U size = { (UINT32) image.getWidth(), (UINT32) image.getHeight() };
            auto        bp   = D2D1::BitmapProperties();

            image = image.convertedToFormat (Image::ARGB);
            Image::BitmapData bd (image, Image::BitmapData::readOnly);
            bp.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
            bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

            ComSmartPtr<ID2D1Bitmap> tiledImageBitmap;
            auto hr = deviceContext.context->CreateBitmap (size, bd.data, bd.lineStride, bp, tiledImageBitmap.resetAndGetPointerAddress());
            jassert (SUCCEEDED (hr));
            if (SUCCEEDED (hr))
            {
                hr = deviceContext.context->CreateBitmapBrush (tiledImageBitmap,
                                                               bmProps,
                                                               brushProps,
                                                               bitmapBrush.resetAndGetPointerAddress());
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
            const int             numColors  = fillType.gradient->getNumColours();

            HeapBlock<D2D1_GRADIENT_STOP> stops (numColors);

            for (int i = fillType.gradient->getNumColours(); --i >= 0;)
            {
                stops[i].color    = direct2d::colourToD2D (fillType.gradient->getColour (i));
                stops[i].position = (FLOAT) fillType.gradient->getColourPosition (i);
            }

            deviceContext.context->CreateGradientStopCollection (stops.getData(), numColors, gradientStops.resetAndGetPointerAddress());

            if (fillType.gradient->isRadial)
            {
                const auto p1    = fillType.gradient->point1;
                const auto p2    = fillType.gradient->point2;
                const auto r     = p1.getDistanceFrom (p2);
                const auto props = D2D1::RadialGradientBrushProperties ({ p1.x, p1.y }, {}, r, r);

                deviceContext.context->CreateRadialGradientBrush (props,
                                                                  brushProps,
                                                                  gradientStops,
                                                                  radialGradient.resetAndGetPointerAddress());
                currentBrush = radialGradient;
            }
            else
            {
                const auto p1    = fillType.gradient->point1;
                const auto p2    = fillType.gradient->point2;
                const auto props = D2D1::LinearGradientBrushProperties ({ p1.x, p1.y }, { p2.x, p2.y });

                deviceContext.context->CreateLinearGradientBrush (props,
                                                                  brushProps,
                                                                  gradientStops,
                                                                  linearGradient.resetAndGetPointerAddress());

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

    struct TranslationOrTransform : public RenderingHelpers::TranslationOrTransform
    {
        bool isAxisAligned() const noexcept
        {
            return isOnlyTranslated || (complexTransform.mat01 == 0.0f && complexTransform.mat11 == 0.0f);
        }
    } currentTransform;
    direct2d::DeviceContext& deviceContext;
    Rectangle<int>           clipRegion;

    Font font;

    ID2D1Brush*                              currentBrush = nullptr;
    ComSmartPtr<ID2D1SolidColorBrush>&       colourBrush; // reference to shared colour brush
    ComSmartPtr<ID2D1BitmapBrush>            bitmapBrush;
    ComSmartPtr<ID2D1LinearGradientBrush>    linearGradient;
    ComSmartPtr<ID2D1RadialGradientBrush>    radialGradient;
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
        ScopedBrushTransformInverter (SavedState const* const state_, AffineTransform const& transformToInvert_)
            : state (state_)
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

        SavedState const* const state;
        bool                    resetTransform = false;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SavedState)
};

//==============================================================================

struct Direct2DLowLevelGraphicsImageContext::Pimpl
{
private:
    Direct2DLowLevelGraphicsImageContext& owner;

    direct2d::DeviceResources deviceResources;

    std::stack<std::unique_ptr<Direct2DLowLevelGraphicsImageContext::SavedState>> savedClientStates;

    int                       frameNumber = 0;
    Direct2DPixelData::Ptr    direct2DPixelData;
    Point<int> const          origin;
    RectangleList<int> const& initialClip;

    HRESULT prepare()
    {
        HRESULT hr = S_OK;

        if (! deviceResources.canPaint())
        {
            if (hr = deviceResources.create (factories->getDefaultAdapter(), 1.0); FAILED (hr))
            {
                return hr;
            }
        }

        D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
        bitmapProperties.pixelFormat.alphaMode   = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bitmapProperties.pixelFormat.format = (direct2DPixelData->pixelFormat == Image::SingleChannel) ? DXGI_FORMAT_A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;

        if (! direct2DPixelData->targetBitmap)
        {
            bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
            hr                                       = deviceResources.deviceContext.context->CreateBitmap (
                D2D1_SIZE_U { (uint32) direct2DPixelData->width, (uint32) direct2DPixelData->height },
                nullptr,
                direct2DPixelData->lineStride,
                bitmapProperties,
                direct2DPixelData->targetBitmap.resetAndGetPointerAddress());

            direct2DPixelData->mappableBitmap = nullptr;
        }

        if (! direct2DPixelData->mappableBitmap)
        {
            bitmapProperties.bitmapOptions           = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
            hr                                       = deviceResources.deviceContext.context->CreateBitmap (
                D2D1_SIZE_U { (uint32) direct2DPixelData->width, (uint32) direct2DPixelData->height },
                nullptr,
                direct2DPixelData->lineStride,
                bitmapProperties,
                direct2DPixelData->mappableBitmap.resetAndGetPointerAddress());
        }

        jassert (SUCCEEDED (hr));

        return hr;
    }

    void teardown()
    {
        deviceResources.release();
    }

    JUCE_DECLARE_WEAK_REFERENCEABLE (Pimpl)

public:
    Pimpl (Direct2DLowLevelGraphicsImageContext& owner_,
           Direct2DPixelData::Ptr                direct2DPixelData_,
           Point<int>                            origin_,
           const RectangleList<int>&             initialClip_)
        : owner (owner_),
          direct2DPixelData (direct2DPixelData_),
          origin (origin_),
          initialClip (initialClip_)
    {
        D2D1_RECT_F rect { 0.0f, 0.0f, 1.0f, 1.0f };
        factories->getDirect2DFactory()->CreateRectangleGeometry (rect, rectangleGeometryUnitSize.resetAndGetPointerAddress());

        prepare();
    }

    ~Pimpl()
    {
        popAllSavedStates();

        direct2DPixelData = nullptr;

        teardown();
    }

    SavedState* startFrame (RectangleList<int>& paintAreas)
    {
        prepare();

        //
        // Paint if:
        //      resources are allocated
        //
        if (! deviceResources.canPaint())
        {
            return nullptr;
        }

        //
        // Always paint the whole image for now
        //
        Rectangle<int> paintBounds { direct2DPixelData->width, direct2DPixelData->height };
        paintAreas = paintBounds;

        //
        // Init device context transform
        //
        deviceResources.deviceContext.resetTransform();

        //
        // Init the saved state stack
        //
        auto firstSavedState = pushFirstSavedState (paintBounds);

        //
        // Start drawing
        //
        deviceResources.deviceContext.context->SetTarget (direct2DPixelData->targetBitmap);
        deviceResources.deviceContext.context->BeginDraw();

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
        // SetTarget(nullptr) so the device context doesn't hold a reference to the swap chain buffer
        //
        auto hr = deviceResources.deviceContext.context->EndDraw();
        deviceResources.deviceContext.context->SetTarget (nullptr);

        jassert (SUCCEEDED (hr));

        TRACE_LOG_D2D_PAINT_END (frameNumber);

        if (FAILED (hr))
        {
            teardown();
        }

        frameNumber++;
    }

    double getScaleFactor() const
    {
        return 1.0;
    }

    SavedState* getCurrentSavedState() const
    {
        return savedClientStates.size() > 0 ? savedClientStates.top().get() : nullptr;
    }

    SavedState* pushFirstSavedState (Rectangle<int> initialClipRegion)
    {
        jassert (savedClientStates.size() == 0);

        savedClientStates.push (
            std::make_unique<SavedState> (initialClipRegion, deviceResources.colourBrush, deviceResources.deviceContext));

        return getCurrentSavedState();
    }

    SavedState* pushSavedState()
    {
        jassert (savedClientStates.size() > 0);

        savedClientStates.push (std::make_unique<SavedState> (savedClientStates.top().get()));

        return getCurrentSavedState();
    }

    SavedState* popSavedState()
    {
        savedClientStates.top()->popLayers();
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

    inline ID2D1DeviceContext1* const getDeviceContext() const noexcept
    {
        return deviceResources.deviceContext.context;
    }

    void setDeviceContextTransform (AffineTransform transform)
    {
        deviceResources.deviceContext.setTransform (transform);
    }

    SharedResourcePointer<DirectXFactories> factories;
    ComSmartPtr<ID2D1RectangleGeometry>     rectangleGeometryUnitSize;
    direct2d::DirectWriteGlyphRun           glyphRun;
    D2D1_COLOR_F                            backgroundColor {};

#if JUCE_DIRECT2D_METRICS
    int64 paintStartTicks = 0;
    int64 paintEndTicks   = 0;
#endif
};

//==============================================================================
Direct2DLowLevelGraphicsImageContext::Direct2DLowLevelGraphicsImageContext (Direct2DPixelData::Ptr direct2DPixelData_)
    : Direct2DLowLevelGraphicsImageContext (direct2DPixelData_,
                                            Point<int> {},
                                            Rectangle<int> { direct2DPixelData_->width, direct2DPixelData_->height })
{
}

Direct2DLowLevelGraphicsImageContext::Direct2DLowLevelGraphicsImageContext (Direct2DPixelData::Ptr    direct2DPixelData_,
                                                                            Point<int>                origin,
                                                                            const RectangleList<int>& initialClip)
    : currentState (nullptr),
      pimpl (new Pimpl { *this, direct2DPixelData_, origin, initialClip })
{
    startFrame();
}

Direct2DLowLevelGraphicsImageContext::~Direct2DLowLevelGraphicsImageContext()
{
    endFrame();
}

bool Direct2DLowLevelGraphicsImageContext::startFrame()
{
    TRACE_LOG_D2D_START_FRAME;

    RectangleList<int> paintAreas;
    if (currentState = pimpl->startFrame (paintAreas); currentState != nullptr)
    {
        if (auto deviceContext = pimpl->getDeviceContext())
        {
            //
            // Clip without transforming
            //
            // Clear() only works with axis-aligned clip layers, so if the window alpha is less than 1.0f, the clip region has to be the union
            // of all the paint areas
            //
            if (paintAreas.getNumRectangles() == 1)
            {
                currentState->pushAxisAlignedClipLayer (paintAreas.getRectangle (0));
            }
            else
            {
                currentState->pushGeometryClipLayer (direct2d::rectListToPathGeometry (pimpl->factories->getDirect2DFactory(),
                                                                                       paintAreas,
                                                                                       AffineTransform {},
                                                                                       D2D1_FILL_MODE_WINDING));
            }

            deviceContext->Clear (pimpl->backgroundColor);

            setFont (currentState->font);

            currentState->updateCurrentBrush();
        }

        return true;
    }

    return false;
}

void Direct2DLowLevelGraphicsImageContext::endFrame()
{
    pimpl->popAllSavedStates();
    currentState = nullptr;

    pimpl->finishFrame();
}

void Direct2DLowLevelGraphicsImageContext::setOrigin (Point<int> o)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::setOrigin);
    currentState->currentTransform.setOrigin (o);
}

void Direct2DLowLevelGraphicsImageContext::addTransform (const AffineTransform& transform)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::addTransform);
    currentState->currentTransform.addTransform (transform);
}

float Direct2DLowLevelGraphicsImageContext::getPhysicalPixelScaleFactor()
{
    return currentState->currentTransform.getPhysicalPixelScaleFactor();
}

bool Direct2DLowLevelGraphicsImageContext::clipToRectangle (const Rectangle<int>& r)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::clipToRectangle);

    //
    // Transform the rectangle and update the current clip region
    //
    auto currentTransform    = currentState->currentTransform.getTransform();
    auto transformedR        = r.transformedBy (currentTransform);
    currentState->clipRegion = currentState->clipRegion.getIntersection (transformedR);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->currentTransform.isAxisAligned())
        {
            //
            // The current world transform is axis-aligned; push an axis aligned clip layer for better
            // performance
            //
            currentState->pushAxisAlignedClipLayer (r);
        }
        else
        {
            //
            // The current world transform is more complex; push a transformed geometry clip layer
            //
            // Instead of allocating a Geometry and then discarding it, use the ID2D1RectangleGeometry already
            // created by the pimpl. rectangleGeometryUnitSize is a 1x1 rectangle at the origin,
            // so pass a transform that scales, translates, and then applies the world transform.
            //
            auto transform = AffineTransform::scale (static_cast<float> (r.getWidth()), static_cast<float> (r.getHeight()))
                                 .translated (r.toFloat().getTopLeft())
                                 .followedBy (currentState->currentTransform.getTransform());

            currentState->pushTransformedRectangleGeometryClipLayer (pimpl->rectangleGeometryUnitSize, transform);
        }
    }

    return ! isClipEmpty();
}

bool Direct2DLowLevelGraphicsImageContext::clipToRectangleList (const RectangleList<int>& clipRegion)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::clipToRectangleList);

    //
    // Just one rectangle?
    //
    if (clipRegion.getNumRectangles() == 1)
    {
        return clipToRectangle (clipRegion.getRectangle (0));
    }

    //
    // Transform the rectangles and update the current clip region
    //
    auto const currentTransform = currentState->currentTransform.getTransform();
    auto       transformedR     = clipRegion.getBounds().transformedBy (currentTransform);
    currentState->clipRegion    = currentState->clipRegion.getIntersection (transformedR);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushGeometryClipLayer (direct2d::rectListToPathGeometry (pimpl->factories->getDirect2DFactory(),
                                                                               clipRegion,
                                                                               currentState->currentTransform.getTransform(),
                                                                               D2D1_FILL_MODE_WINDING));
    }

    return ! isClipEmpty();
}

void Direct2DLowLevelGraphicsImageContext::excludeClipRectangle (const Rectangle<int>& r)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::excludeClipRectangle);

    //
    // To exclude the rectangle r, build a rectangle list with r as the first rectangle and a very large rectangle as the second.
    //
    // Then, convert that rectangle list to a geometry, but specify D2D1_FILL_MODE_ALTERNATE so the inside of r is *outside*
    // the geometry and everything else on the screen is inside the geometry.
    //
    // Have to use addWithoutMerging to build the rectangle list to keep the rectangles separate.
    //
    RectangleList<int> rectangles { r };
    rectangles.addWithoutMerging ({ -maxFrameSize, -maxFrameSize, maxFrameSize * 2, maxFrameSize * 2 });

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushGeometryClipLayer (direct2d::rectListToPathGeometry (pimpl->factories->getDirect2DFactory(),
                                                                               rectangles,
                                                                               currentState->currentTransform.getTransform(),
                                                                               D2D1_FILL_MODE_ALTERNATE));
    }
}

void Direct2DLowLevelGraphicsImageContext::clipToPath (const Path& path, const AffineTransform& transform)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::clipToPath);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushGeometryClipLayer (direct2d::pathToPathGeometry (pimpl->factories->getDirect2DFactory(),
                                                                           path,
                                                                           currentState->currentTransform.getTransformWith (transform)));
    }
}

void Direct2DLowLevelGraphicsImageContext::clipToImageAlpha (const Image& sourceImage, const AffineTransform& transform)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::clipToImageAlpha);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        //
        // Convert sourceImage to single-channel alpha-only maskImage
        //
        auto const        maskImage = sourceImage.convertedToFormat (Image::SingleChannel);
        Image::BitmapData bitmapData { maskImage, Image::BitmapData::readOnly };

        auto bitmapProperties                  = D2D1::BitmapProperties();
        bitmapProperties.pixelFormat.format    = DXGI_FORMAT_A8_UNORM;
        bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

        //
        // Convert maskImage to a Direct2D bitmap
        //
        ComSmartPtr<ID2D1Bitmap> bitmap;
        auto hr = deviceContext->CreateBitmap (D2D1_SIZE_U { (UINT32) maskImage.getWidth(), (UINT32) maskImage.getHeight() },
                                               bitmapData.data,
                                               bitmapData.lineStride,
                                               bitmapProperties,
                                               bitmap.resetAndGetPointerAddress());
        if (SUCCEEDED (hr))
        {
            //
            // Make a transformed bitmap brush using the bitmap
            //
            // As usual, apply the current transform first *then* the transform parameter
            //
            ComSmartPtr<ID2D1BitmapBrush> brush;
            auto                          brushTransform = currentState->currentTransform.getTransformWith (transform);
            auto                          matrix         = direct2d::transformToMatrix (brushTransform);
            D2D1_BRUSH_PROPERTIES         brushProps     = { 1.0f, matrix };

            auto bitmapBrushProps = D2D1::BitmapBrushProperties (D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP);
            hr = deviceContext->CreateBitmapBrush (bitmap, bitmapBrushProps, brushProps, brush.resetAndGetPointerAddress());
            if (SUCCEEDED (hr))
            {
                //
                // Push the clipping layer onto the layer stack
                //
                // Don't maskTransform in the LayerParameters struct; that only applies to geometry clipping
                // Do set the contentBounds member, transformed appropriately
                //
                auto layerParams          = D2D1::LayerParameters();
                auto transformedBounds    = maskImage.getBounds().toFloat().transformedBy (brushTransform);
                layerParams.contentBounds = direct2d::rectangleToRectF (transformedBounds);
                layerParams.opacityBrush  = brush;

                currentState->pushLayer (layerParams);
            }
        }
    }
}

bool Direct2DLowLevelGraphicsImageContext::clipRegionIntersects (const Rectangle<int>& r)
{
    return getClipBounds().intersects (r);
}

Rectangle<int> Direct2DLowLevelGraphicsImageContext::getClipBounds() const
{
    return currentState->currentTransform.deviceSpaceToUserSpace (currentState->clipRegion);
}

bool Direct2DLowLevelGraphicsImageContext::isClipEmpty() const
{
    return getClipBounds().isEmpty();
}

void Direct2DLowLevelGraphicsImageContext::saveState()
{
    TRACE_LOG_D2D_PAINT_CALL (etw::saveState);

    currentState = pimpl->pushSavedState();
}

void Direct2DLowLevelGraphicsImageContext::restoreState()
{
    TRACE_LOG_D2D_PAINT_CALL (etw::restoreState);

    currentState = pimpl->popSavedState();
    jassert (currentState);
}

void Direct2DLowLevelGraphicsImageContext::beginTransparencyLayer (float opacity)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::beginTransparencyLayer);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->pushTransparencyLayer (opacity);
    }
}

void Direct2DLowLevelGraphicsImageContext::endTransparencyLayer()
{
    TRACE_LOG_D2D_PAINT_CALL (etw::endTransparencyLayer);
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->popTopLayer();
    }
}

void Direct2DLowLevelGraphicsImageContext::setFill (const FillType& fillType)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::setFill);
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->fillType = fillType;
        currentState->updateCurrentBrush();
    }
}

void Direct2DLowLevelGraphicsImageContext::setOpacity (float newOpacity)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::setOpacity);

    currentState->setOpacity (newOpacity);
    if (auto deviceContext = pimpl->getDeviceContext())
    {
        currentState->updateCurrentBrush();
    }
}

void Direct2DLowLevelGraphicsImageContext::setInterpolationQuality (Graphics::ResamplingQuality quality)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::setInterpolationQuality);

    switch (quality)
    {
        case Graphics::ResamplingQuality::lowResamplingQuality:
            currentState->interpolationMode = D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
            break;

        case Graphics::ResamplingQuality::mediumResamplingQuality: currentState->interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR; break;

        case Graphics::ResamplingQuality::highResamplingQuality:
            currentState->interpolationMode = D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
            break;
    }
}

void Direct2DLowLevelGraphicsImageContext::fillRect (const Rectangle<int>& r, bool /*replaceExistingContents*/)
{
    fillRect (r.toFloat());
}

void Direct2DLowLevelGraphicsImageContext::fillRect (const Rectangle<float>& r)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::fillRect);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return;
        }

        updateDeviceContextTransform();
        deviceContext->FillRectangle (direct2d::rectangleToRectF (r), currentState->currentBrush);
    }
}

void Direct2DLowLevelGraphicsImageContext::fillRectList (const RectangleList<float>& list)
{
    for (auto& r : list) fillRect (r);
}

bool Direct2DLowLevelGraphicsImageContext::drawRect (const Rectangle<float>& r, float lineThickness)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawRect);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        updateDeviceContextTransform();
        deviceContext->DrawRectangle (direct2d::rectangleToRectF (r), currentState->currentBrush, lineThickness);
    }

    return true;
}

void Direct2DLowLevelGraphicsImageContext::fillPath (const Path& p, const AffineTransform& transform)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::fillPath);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return;
        }

        if (auto geometry = direct2d::pathToPathGeometry (pimpl->factories->getDirect2DFactory(), p, transform))
        {
            updateDeviceContextTransform();
            deviceContext->FillGeometry (geometry, currentState->currentBrush);
        }
    }
}

bool Direct2DLowLevelGraphicsImageContext::drawPath (const Path& p, const PathStrokeType& strokeType, const AffineTransform& transform)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawPath);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        if (auto factory = pimpl->factories->getDirect2DFactory())
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

                    case PathStrokeType::JointStyle::curved: lineJoin = D2D1_LINE_JOIN_ROUND; break;

                    case PathStrokeType::JointStyle::beveled: lineJoin = D2D1_LINE_JOIN_BEVEL; break;

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

                    case PathStrokeType::EndCapStyle::square: capStyle = D2D1_CAP_STYLE_SQUARE; break;

                    case PathStrokeType::EndCapStyle::rounded: capStyle = D2D1_CAP_STYLE_ROUND; break;

                    default:
                        // invalid EndCapStyle
                        jassertfalse;
                        break;
                }

                D2D1_STROKE_STYLE_PROPERTIES  strokeStyleProperties { capStyle, capStyle, capStyle, lineJoin, 1.0f, D2D1_DASH_STYLE_SOLID,
                                                                     0.0f };
                ComSmartPtr<ID2D1StrokeStyle> strokeStyle;
                factory->CreateStrokeStyle (strokeStyleProperties, // TODO reuse the stroke style
                                            nullptr,
                                            0,
                                            strokeStyle.resetAndGetPointerAddress());

                updateDeviceContextTransform();
                deviceContext->DrawGeometry (geometry, currentState->currentBrush, strokeType.getStrokeThickness(), strokeStyle);
            }
        }
    }
    return true;
}

void Direct2DLowLevelGraphicsImageContext::drawImage (const Image& image, const AffineTransform& transform)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawImage);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        updateDeviceContextTransform (transform);

        auto              argbImage = image.convertedToFormat (Image::ARGB);
        Image::BitmapData bitmapData { argbImage, Image::BitmapData::readOnly };

        auto bitmapProperties                  = D2D1::BitmapProperties();
        bitmapProperties.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
        bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

        D2D1_SIZE_U size = { (UINT32) image.getWidth(), (UINT32) image.getHeight() };

        ComSmartPtr<ID2D1Bitmap> bitmap;
        deviceContext->CreateBitmap (size, bitmapData.data, bitmapData.lineStride, bitmapProperties, bitmap.resetAndGetPointerAddress());
        if (bitmap)
        {
            deviceContext->DrawBitmap (bitmap, nullptr, currentState->fillType.getOpacity(), currentState->interpolationMode, nullptr, {});
        }
    }
}

void Direct2DLowLevelGraphicsImageContext::drawLine (const Line<float>& line)
{
    drawLine (line, 1.0f);
}

bool Direct2DLowLevelGraphicsImageContext::drawLine (const Line<float>& line, float lineThickness)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawLine);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        updateDeviceContextTransform();
        deviceContext->DrawLine (D2D1::Point2F (line.getStartX(), line.getStartY()),
                                 D2D1::Point2F (line.getEndX(), line.getEndY()),
                                 currentState->currentBrush,
                                 lineThickness);
    }

    return true;
}

void Direct2DLowLevelGraphicsImageContext::setFont (const Font& newFont)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::setFont);

    currentState->setFont (newFont);
}

const Font& Direct2DLowLevelGraphicsImageContext::getFont()
{
    return currentState->font;
}

void Direct2DLowLevelGraphicsImageContext::drawGlyph (int glyphNumber, const AffineTransform& transform)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawGlyph);

    pimpl->glyphRun.glyphIndices[0] = (uint16) glyphNumber;
    pimpl->glyphRun.glyphOffsets[0] = {};

    drawGlyphCommon (1, currentState->font, transform, {});
}

bool Direct2DLowLevelGraphicsImageContext::drawTextLayout (const AttributedString& text, const Rectangle<float>& area)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawTextLayout);

    if (currentState->fillType.isInvisible())
    {
        return true;
    }

    auto deviceContext      = pimpl->getDeviceContext();
    auto directWriteFactory = pimpl->factories->getDirectWriteFactory();
    auto fontCollection     = pimpl->factories->getSystemFonts();

    if (deviceContext && directWriteFactory && fontCollection)
    {
        updateDeviceContextTransform();

        auto translatedArea = area;
        auto textLayout =
            DirectWriteTypeLayout::createDirectWriteTextLayout (text, translatedArea, *directWriteFactory, *fontCollection, *deviceContext);
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

bool Direct2DLowLevelGraphicsImageContext::drawRoundedRectangle (Rectangle<float> area, float cornerSize, float lineThickness)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawRoundedRectangle);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        updateDeviceContextTransform();
        D2D1_ROUNDED_RECT roundedRect { direct2d::rectangleToRectF (area), cornerSize, cornerSize };
        deviceContext->DrawRoundedRectangle (roundedRect, currentState->currentBrush, lineThickness);
    }

    return true;
}

bool Direct2DLowLevelGraphicsImageContext::fillRoundedRectangle (Rectangle<float> area, float cornerSize)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::fillRoundedRectangle);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        updateDeviceContextTransform();
        D2D1_ROUNDED_RECT roundedRect { direct2d::rectangleToRectF (area), cornerSize, cornerSize };
        deviceContext->FillRoundedRectangle (roundedRect, currentState->currentBrush);
    }

    return true;
}

bool Direct2DLowLevelGraphicsImageContext::drawEllipse (Rectangle<float> area, float lineThickness)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawEllipse);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        updateDeviceContextTransform();

        auto         centre = area.getCentre();
        D2D1_ELLIPSE ellipse { { centre.x, centre.y }, area.proportionOfWidth (0.5f), area.proportionOfHeight (0.5f) };
        deviceContext->DrawEllipse (ellipse, currentState->currentBrush, lineThickness, nullptr);
    }
    return true;
}

bool Direct2DLowLevelGraphicsImageContext::fillEllipse (Rectangle<float> area)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::fillEllipse);

    if (auto deviceContext = pimpl->getDeviceContext())
    {
        if (currentState->fillType.isInvisible())
        {
            return true;
        }

        updateDeviceContextTransform();

        auto         centre = area.getCentre();
        D2D1_ELLIPSE ellipse { { centre.x, centre.y }, area.proportionOfWidth (0.5f), area.proportionOfHeight (0.5f) };
        deviceContext->FillEllipse (ellipse, currentState->currentBrush);
    }
    return true;
}

void Direct2DLowLevelGraphicsImageContext::drawGlyphRun (Array<PositionedGlyph> const& glyphs,
                                                         int                           startIndex,
                                                         int                           numGlyphs,
                                                         const AffineTransform&        transform,
                                                         Rectangle<float>              underlineArea)
{
    TRACE_LOG_D2D_PAINT_CALL (etw::drawGlyphRun);

    if (numGlyphs > 0 && (startIndex + numGlyphs) <= glyphs.size())
    {
        if (currentState->fillType.isInvisible())
        {
            return;
        }

        //
        // Fill the array of glyph indices and offsets
        //
        // All the fonts should be the same for the glyph run
        //
        pimpl->glyphRun.ensureStorageAllocated (numGlyphs);

        auto const& font                = glyphs[startIndex].getFont();
        auto        fontHorizontalScale = font.getHorizontalScale();
        auto        inverseHScale       = fontHorizontalScale > 0.0f ? 1.0f / fontHorizontalScale : 1.0f;

        auto indices = pimpl->glyphRun.glyphIndices.getData();
        auto offsets = pimpl->glyphRun.glyphOffsets.getData();

        int numGlyphsToDraw = 0;
        for (int sourceIndex = 0; sourceIndex < numGlyphs; ++sourceIndex)
        {
            auto const& glyph = glyphs[sourceIndex + startIndex];
            if (! glyph.isWhitespace())
            {
                indices[numGlyphsToDraw] = (UINT16) glyph.getGlyphNumber();
                offsets[numGlyphsToDraw] = {
                    glyph.getLeft() * inverseHScale,
                    -glyph.getBaselineY()
                }; // note the essential minus sign before the baselineY value; negative offset goes down, positive goes up (opposite from JUCE)
                jassert (pimpl->glyphRun.glyphAdvances[numGlyphsToDraw] == 0.0f);
                jassert (glyph.getFont() == font);
                ++numGlyphsToDraw;
            }
        }

        drawGlyphCommon (numGlyphsToDraw, font, transform, underlineArea);
    }
}

void Direct2DLowLevelGraphicsImageContext::drawGlyphCommon (int                    numGlyphs,
                                                            Font const&            font,
                                                            const AffineTransform& transform,
                                                            Rectangle<float>       underlineArea)
{
    auto deviceContext = pimpl->getDeviceContext();
    if (! deviceContext)
    {
        return;
    }

    auto dwriteFontFace = direct2d::DirectWriteFontFace::fromFont (font);
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
    auto scaledTransform   = AffineTransform::scale (dwriteFontFace.fontHorizontalScale, 1.0f).followedBy (transform);
    auto glyphRunTransform = scaledTransform.followedBy (currentState->currentTransform.getTransform());
    pimpl->setDeviceContextTransform (glyphRunTransform);

    DWRITE_GLYPH_RUN directWriteGlyphRun;
    directWriteGlyphRun.fontFace      = dwriteFontFace.fontFace;
    directWriteGlyphRun.fontEmSize    = dwriteFontFace.getEmSize();
    directWriteGlyphRun.glyphCount    = numGlyphs;
    directWriteGlyphRun.glyphIndices  = pimpl->glyphRun.glyphIndices.getData();
    directWriteGlyphRun.glyphAdvances = pimpl->glyphRun.glyphAdvances.getData();
    directWriteGlyphRun.glyphOffsets  = pimpl->glyphRun.glyphOffsets.getData();
    directWriteGlyphRun.isSideways    = FALSE;
    directWriteGlyphRun.bidiLevel     = 0;

    //
    // The gradient brushes are position-dependent, so need to undo the device context transform
    //
    SavedState::ScopedBrushTransformInverter brushTransformInverter { currentState, scaledTransform };

    deviceContext->DrawGlyphRun ({}, &directWriteGlyphRun, currentState->currentBrush);

    //
    // Draw the underline
    //
    if (! underlineArea.isEmpty())
    {
        fillRect (underlineArea);
    }
}

void Direct2DLowLevelGraphicsImageContext::updateDeviceContextTransform()
{
    pimpl->setDeviceContextTransform (currentState->currentTransform.getTransform());
}

void Direct2DLowLevelGraphicsImageContext::updateDeviceContextTransform (AffineTransform chainedTransform)
{
    pimpl->setDeviceContextTransform (currentState->currentTransform.getTransformWith (chainedTransform));
}

} // namespace juce
