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
    // Direct2D native Path type
    //

    class Direct2DPathData : public PathData
    {
    public:
        Direct2DPathData()
        {
        }

        std::unique_ptr<PathType> createType() const override
        {
            return std::make_unique<Direct2DPathType>();
        }

        using Ptr = ReferenceCountedObjectPtr<Direct2DPathData>;

        struct GeometryRealisation
        {
            virtual ~GeometryRealisation() = default;

            virtual void create(ID2D1Geometry* geometry, juce::Rectangle<float>, ID2D1DeviceContext1* const deviceContext)
            {
                if (geometry)
                {
                    //
                    // Create a new realisation?
                    //
                    if (geometryRealisation == nullptr)
                    {
                        deviceContext->CreateFilledGeometryRealization(geometry,
                            flatteningTolerance,
                            geometryRealisation.resetAndGetPointerAddress());
                    }
                }
            }

            float flatteningTolerance = 1.0f;
            ComSmartPtr<ID2D1GeometryRealization> geometryRealisation;
        };

        struct StrokedGeometryRealisation : public GeometryRealisation
        {
            virtual ~StrokedGeometryRealisation() = default;

            virtual void create(ID2D1Geometry* geometry, juce::Rectangle<float> pathBounds, ID2D1DeviceContext1* const deviceContext)
            {
                if (geometry)
                {
                    //
                    // Create a new realisation?
                    //
                    if (geometryRealisation == nullptr && strokeStyle != nullptr)
                    {
                        //
                        // Transforming the stroked geometry realization will also affect the line weight. 
                        // Determine how much the specified transform will affect the path bounds
                        // and scale the link thickness accordingly.
                        //
                        auto widthRatio = pathBounds.getWidth() / size.getWidth();
                        auto heightRatio = pathBounds.getHeight() / size.getHeight();
                        auto strokeThicknessScale = juce::jmin(widthRatio, heightRatio);

                        deviceContext->CreateStrokedGeometryRealization(geometry,
                            flatteningTolerance,
                            strokeType.getStrokeThickness() * strokeThicknessScale,
                            strokeStyle,
                            geometryRealisation.resetAndGetPointerAddress());

                        strokeStyle = nullptr;
                    }
                }

            }

            Rectangle<float> size;
            PathStrokeType strokeType{ 1.0f };
            ComSmartPtr<ID2D1StrokeStyle> strokeStyle;
        };

        static float findGeometryFlatteningTolerance(float dpiScaleFactor, const AffineTransform& transform, float maxZoomFactor = 1.0f)
        {
            jassert(maxZoomFactor > 0.0f);

            //
            // Could use D2D1::ComputeFlatteningTolerance here, but that requires defining NTDDI_VERSION and it doesn't do anything special.
            // 
            // Direct2D default flattening tolerance is 0.25
            //
            auto transformScaleFactor = std::sqrt(std::abs(transform.getDeterminant()));
            return 0.25f / (transformScaleFactor * dpiScaleFactor * maxZoomFactor);
        }

        ID2D1GeometryRealization* const getOrCreateFilledGeometryRealisation(Path const& path, 
            ID2D1Factory2* const factory, 
            ID2D1DeviceContext1* const deviceContext, 
            float dpiScaleFactor, 
            AffineTransform const& transform)
        {
            return getOrCreateCommon(path, factory, deviceContext, dpiScaleFactor, transform, filled);
        }

        ID2D1GeometryRealization* const getOrCreateStrokedGeometryRealisation(Path const& path,
            const PathStrokeType& strokeType,
            ID2D1Factory2* const factory,
            ID2D1DeviceContext1* const deviceContext,
            float dpiScaleFactor,
            AffineTransform const& transform)
        {
            auto transformedSize = path.getBoundsTransformed(transform).withZeroOrigin();
            if (transformedSize.isEmpty())
            {
                return nullptr;
            }

            if (stroked.size != transformedSize)
            {
                stroked.geometryRealisation = nullptr;
                stroked.size = transformedSize;
            }

            if (strokeType != stroked.strokeType)
            {
                stroked.geometryRealisation = nullptr;
                stroked.strokeType = strokeType;
                stroked.strokeStyle = direct2d::pathStrokeTypeToStrokeStyle(factory, strokeType);
            }

            auto gr = getOrCreateCommon(path, factory, deviceContext, dpiScaleFactor, transform, stroked);
            return gr;
        }

        ComSmartPtr<ID2D1Geometry> geometry;
        GeometryRealisation filled;
        StrokedGeometryRealisation stroked;

    private:
        // keep a reference to the DirectXFactories to retain the DLLs & factories
        SharedResourcePointer<DirectXFactories> factories;

        ID2D1GeometryRealization* getOrCreateCommon(Path const& path, 
            ID2D1Factory2* const factory, 
            ID2D1DeviceContext1* const deviceContext, 
            float dpiScaleFactor, 
            AffineTransform const& transform, 
            GeometryRealisation& realisation)
        {
            //
            // Has the geometry been created or has the path changed?
            //
            if (geometry == nullptr || hasChanged())
            {
                geometry = nullptr;
                filled.geometryRealisation = nullptr;
                stroked.geometryRealisation = nullptr;

                geometry = direct2d::pathToPathGeometry(factory, path, {});
            }

            if (geometry)
            {
                //
                // Is there an existing geometry realisation; if so, was the flattening tolerance for that
                // realisation within range? 
                //
                auto flatteningTolerance = findGeometryFlatteningTolerance(dpiScaleFactor, transform);
                Range<float> flatteningToleranceRange{ flatteningTolerance * 0.5f, flatteningTolerance * 2.0f };
                if (!flatteningToleranceRange.contains(flatteningTolerance))
                {
                    realisation.geometryRealisation = nullptr;
                }

                //
                // Create a new realisation if necessary
                //
                if (realisation.geometryRealisation == nullptr)
                {
                    realisation.flatteningTolerance = flatteningTolerance;
                    realisation.create(geometry, path.getBounds(), deviceContext);
                }
            }

            return realisation.geometryRealisation;
        }

        JUCE_LEAK_DETECTOR(Direct2DPathData)
    };

    PathData::Ptr Direct2DPathType::createData() const
    {
        return new Direct2DPathData{};
    }

} // namespace juce
