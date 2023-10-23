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
            float flatteningTolerance = 1.0f;
            ComSmartPtr<ID2D1GeometryRealization> geometryRealisation;
        };

        ComSmartPtr<ID2D1Geometry> geometry;
        GeometryRealisation filled;

    private:
        // keep a reference to the DirectXFactories to retain the DLLs & factories
        SharedResourcePointer<DirectXFactories> factories;

        JUCE_LEAK_DETECTOR(Direct2DPathData)
    };

    PathData::Ptr Direct2DPathType::createData() const
    {
        return new Direct2DPathData{};
    }

    PathData::Ptr NativePathType::createData() const
    {
        return new Direct2DPathData{};
    }

    int NativePathType::getTypeID() const
    {
        return 0xd2d;
    }

} // namespace juce
