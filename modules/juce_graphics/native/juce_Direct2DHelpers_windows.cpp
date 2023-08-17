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
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <d2d1_2.h>
#include <d3d11_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include <evntrace.h>
#include <TraceLoggingProvider.h>

#endif

namespace juce
{

namespace direct2d
{
    template <typename Type>
    D2D1_RECT_F rectangleToRectF (const Rectangle<Type>& r)
    {
        return { (float) r.getX(), (float) r.getY(), (float) r.getRight(), (float) r.getBottom() };
    }

    template <typename Type>
    RECT rectangleToRECT (const Rectangle<Type>& r)
    {
        return { r.getX(), r.getY(), r.getRight(), r.getBottom() };
    }

    template <typename Type>
    Rectangle<int> RECTToRectangle (RECT const& r)
    {
        return Rectangle<int>::leftTopRightBottom (r.left, r.top, r.right, r.bottom);
    }

    static D2D1_COLOR_F colourToD2D (Colour c)
    {
        return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), c.getFloatAlpha() };
    }

    static void pathToGeometrySink (const Path& path, ID2D1GeometrySink* sink, const AffineTransform& transform)
    {
        //
        // Every call to BeginFigure must have a matching call to EndFigure. But - the Path does not necessarily
        // have matching startNewSubPath and closePath markers. The figureStarted flag indicates if an extra call
        // to BeginFigure or EndFigure is needed during the iteration loop or when exiting this function.
        //
        Path::Iterator it (path);
        bool figureStarted = false;

        while (it.next())
        {
            switch (it.elementType)
            {
                case Path::Iterator::cubicTo:
                {
                    jassert (figureStarted);

                    transform.transformPoint (it.x1, it.y1);
                    transform.transformPoint (it.x2, it.y2);
                    transform.transformPoint (it.x3, it.y3);

                    sink->AddBezier ({ { it.x1, it.y1 }, { it.x2, it.y2 }, { it.x3, it.y3 } });
                    break;
                }

                case Path::Iterator::lineTo:
                {
                    jassert (figureStarted);

                    transform.transformPoint (it.x1, it.y1);
                    sink->AddLine ({ it.x1, it.y1 });
                    break;
                }

                case Path::Iterator::quadraticTo:
                {
                    jassert (figureStarted);

                    transform.transformPoint (it.x1, it.y1);
                    transform.transformPoint (it.x2, it.y2);
                    sink->AddQuadraticBezier ({ { it.x1, it.y1 }, { it.x2, it.y2 } });
                    break;
                }

                case Path::Iterator::closePath:
                {
                    if (figureStarted)
                    {
                        sink->EndFigure (D2D1_FIGURE_END_CLOSED);
                        figureStarted = false;
                    }
                    break;
                }

                case Path::Iterator::startNewSubPath:
                {
                    if (figureStarted)
                    {
                        sink->EndFigure (D2D1_FIGURE_END_CLOSED);
                    }

                    transform.transformPoint (it.x1, it.y1);
                    sink->BeginFigure ({ it.x1, it.y1 }, D2D1_FIGURE_BEGIN_FILLED);
                    figureStarted = true;
                    break;
                }
            }
        }

        if (figureStarted)
        {
            sink->EndFigure (D2D1_FIGURE_END_OPEN);
        }
    }

    static D2D1::Matrix3x2F transformToMatrix (const AffineTransform& transform)
    {
        return { transform.mat00, transform.mat10, transform.mat01, transform.mat11, transform.mat02, transform.mat12 };
    }

    static D2D1_POINT_2F pointTransformed (int x, int y, const AffineTransform& transform)
    {
        transform.transformPoint (x, y);
        return { (FLOAT) x, (FLOAT) y };
    }

    static void rectToGeometrySink (const Rectangle<int>& rect, ID2D1GeometrySink* sink, const AffineTransform& transform)
    {
        sink->BeginFigure (pointTransformed (rect.getX(), rect.getY(), transform), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine (pointTransformed (rect.getRight(), rect.getY(), transform));
        sink->AddLine (pointTransformed (rect.getRight(), rect.getBottom(), transform));
        sink->AddLine (pointTransformed (rect.getX(), rect.getBottom(), transform));
        sink->EndFigure (D2D1_FIGURE_END_CLOSED);
    }

    //
    // ScopedGeometryWithSink creates an ID2D1PathGeometry object with an open sink.
    // D.R.Y. for rectToPathGeometry, rectListToPathGeometry, and pathToPathGeometry
    //
    struct ScopedGeometryWithSink
    {
        ScopedGeometryWithSink (ID2D1Factory* factory, D2D1_FILL_MODE fillMode)
        {
            auto hr = factory->CreatePathGeometry (geometry.resetAndGetPointerAddress());
            if (SUCCEEDED (hr))
            {
                hr = geometry->Open (sink.resetAndGetPointerAddress());
                if (SUCCEEDED (hr))
                {
                    sink->SetFillMode (fillMode);
                }
            }
        }

        ~ScopedGeometryWithSink()
        {
            if (sink != nullptr)
            {
                auto hr = sink->Close();
                jassertquiet (SUCCEEDED (hr));
            }
        }

        ComSmartPtr<ID2D1PathGeometry> geometry;
        ComSmartPtr<ID2D1GeometrySink> sink;
    };

    ComSmartPtr<ID2D1Geometry> rectToPathGeometry (ID2D1Factory* factory, const Rectangle<int>& rect, const AffineTransform& transform, D2D1_FILL_MODE fillMode)
    {
        ScopedGeometryWithSink objects { factory, fillMode };

        if (objects.sink != nullptr)
        {
            direct2d::rectToGeometrySink (rect, objects.sink, transform);
            return { (ID2D1Geometry*) objects.geometry };
        }

        return nullptr;
    }

    ComSmartPtr<ID2D1Geometry> rectListToPathGeometry (ID2D1Factory* factory, const RectangleList<int>& clipRegion, const AffineTransform& transform, D2D1_FILL_MODE fillMode)
    {
        ScopedGeometryWithSink objects { factory, fillMode };

        if (objects.sink != nullptr)
        {
            for (int i = clipRegion.getNumRectangles(); --i >= 0;)
                direct2d::rectToGeometrySink (clipRegion.getRectangle (i), objects.sink, transform);

            return { (ID2D1Geometry*) objects.geometry };
        }

        return nullptr;
    }

    ComSmartPtr<ID2D1Geometry> pathToPathGeometry (ID2D1Factory* factory, const Path& path, const AffineTransform& transform)
    {
        ScopedGeometryWithSink objects { factory, path.isUsingNonZeroWinding() ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE };

        if (objects.sink != nullptr)
        {
            direct2d::pathToGeometrySink (path, objects.sink, transform);

            return { (ID2D1Geometry*) objects.geometry };
        }

        return nullptr;
    }

    class UpdateRegion
    {
    public:
        ~UpdateRegion()
        {
            clear();
        }

        void getRECTAndValidate (HWND windowHandle)
        {
            numRect = 0;

            auto regionHandle = CreateRectRgn (0, 0, 0, 0);
            if (regionHandle)
            {
                auto regionType = GetUpdateRgn (windowHandle, regionHandle, false);
                if (regionType == SIMPLEREGION || regionType == COMPLEXREGION)
                {
                    auto regionDataBytes = GetRegionData (regionHandle, (DWORD) block.getSize(), (RGNDATA*) block.getData());
                    if (regionDataBytes > block.getSize())
                    {
                        block.ensureSize (regionDataBytes);
                        regionDataBytes = GetRegionData (regionHandle, (DWORD) block.getSize(), (RGNDATA*) block.getData());
                    }

                    if (regionDataBytes > 0)
                    {
                        auto header = (RGNDATAHEADER const* const) block.getData();
                        if (header->iType == RDH_RECTANGLES)
                        {
                            numRect = header->nCount;
                        }
                    }
                }

                if (numRect > 0)
                {
                    ValidateRgn (windowHandle, regionHandle);
                }
                else
                {
                    ValidateRect (windowHandle, nullptr);
                }

                DeleteObject (regionHandle);
                regionHandle = nullptr;

                return;
            }

            ValidateRect (windowHandle, nullptr);
        }

        void clear()
        {
            numRect = 0;
        }

        uint32 getNumRECT() const
        {
            return numRect;
        }

        RECT* getRECTArray()
        {
            auto header = (RGNDATAHEADER const* const) block.getData();
            return (RECT*) (header + 1);
        }

        void addToRectangleList (RectangleList<int>& rectangleList)
        {
            rectangleList.ensureStorageAllocated (rectangleList.getNumRectangles() + getNumRECT());
            for (uint32 i = 0; i < getNumRECT(); ++i)
            {
                auto r = RECTToRectangle<int> (getRECTArray()[i]);
                rectangleList.add (r);
            }
        }

        static void forwardInvalidRegionToParent (HWND childHwnd)
        {
            auto regionHandle = CreateRectRgn (0, 0, 0, 0);
            if (regionHandle)
            {
                GetUpdateRgn (childHwnd, regionHandle, false);
                ValidateRgn (childHwnd, regionHandle);
                InvalidateRgn (GetParent (childHwnd), regionHandle, FALSE);
                DeleteObject (regionHandle);
            }
        }

    private:
        MemoryBlock block { 1024 };
        uint32 numRect = 0;
    };

    struct DirectWriteFontFace
    {
        ComSmartPtr<IDWriteFontFace> fontFace;
        float fontHeight = 0.0f;
        float fontHeightToEmSizeFactor = 0.0f;
        float fontHorizontalScale = 0.0f;

        float getEmSize() const noexcept
        {
            return fontHeight * fontHeightToEmSizeFactor;
        }

        void clear()
        {
            fontFace = nullptr;
        }
    };

    class DirectWriteGlyphRun
    {
    public:
        DirectWriteGlyphRun()
        {
            ensureStorageAllocated(16);
        }

        void ensureStorageAllocated(int capacityNeeded)
        {
            if (capacityNeeded > glyphCapacity)
            {
                glyphCapacity = capacityNeeded;
                glyphIndices.realloc(capacityNeeded);
                glyphAdvances.realloc(capacityNeeded);
                glyphOffsets.realloc(capacityNeeded);

                glyphAdvances.clear(capacityNeeded);
            }
        }

        int glyphCapacity = 0;
        HeapBlock<UINT16> glyphIndices;
        HeapBlock<float> glyphAdvances;
        HeapBlock<DWRITE_GLYPH_OFFSET> glyphOffsets;
    };

} // namespace direct2d

#if JUCE_ETW_TRACELOGGING

TRACELOGGING_DEFINE_PROVIDER (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,
                              "JuceEtwTraceLogging",
                              // {6A612E78-284D-4DDB-877A-5F521EB33132}
                              (0x6a612e78, 0x284d, 0x4ddb, 0x87, 0x7a, 0x5f, 0x52, 0x1e, 0xb3, 0x31, 0x32));

ETWEventProvider::ETWEventProvider()
{
    auto hr = TraceLoggingRegister (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE);
    juce::ignoreUnused (hr);
    jassert (SUCCEEDED (hr));
}

ETWEventProvider::~ETWEventProvider()
{
    TraceLoggingUnregister (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE);
}

#endif

} // namespace juce