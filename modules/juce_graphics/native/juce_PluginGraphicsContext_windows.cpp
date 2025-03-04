/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

#include "../../../extras/Direct2DPlugin/Source/Direct2DPluginAPI.h"

//==============================================================================
struct PluginGraphicsContext::Pimpl
{
protected:
    PluginGraphicsContext& owner;

public:
    explicit Pimpl (PluginGraphicsContext& ownerIn, void* windowHandle)
        : owner(ownerIn), hwnd((HWND)windowHandle)
    {
        if (library.open(R"(C:\JUCE-fork\extras\Direct2DPlugin\Builds\VisualStudio2022\x64\Debug\Dynamic Library\\Direct2DPlugin.dll)"))
        {
            D2DPlugin_openHwnd = (decltype(D2DPlugin_openHwnd)) library.getFunction("D2DPlugin_openHwnd");
            D2DPlugin_close = (decltype(D2DPlugin_close))library.getFunction("D2DPlugin_close");
            D2DPlugin_execute = (decltype(D2DPlugin_execute))library.getFunction("D2DPlugin_execute");

            if (D2DPlugin_openHwnd)
            {
                pluginHandle = D2DPlugin_openHwnd(windowHandle);
            }
        }
    }

    ~Pimpl()
    {
        library.close();
    }

    DynamicLibrary library;
    int (*D2DPlugin_openHwnd)(void* hwnd) = nullptr;
    void (*D2DPlugin_close)() = nullptr;
    void (*D2DPlugin_execute)(int, Direct2DPluginOp*) = nullptr;
    HWND const hwnd = nullptr;
    int pluginHandle = -1;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Pimpl)
};

//==============================================================================
PluginGraphicsContext::PluginGraphicsContext(void* windowHandle) :
    pimpl(std::make_unique<Pimpl>(*this, windowHandle))
{
}

PluginGraphicsContext::~PluginGraphicsContext() = default;

void PluginGraphicsContext::setOrigin (Point<int> o)
{
    Direct2DPluginOp op;
    op.op = Direct2DPluginOp_setOrigin;
    op.u.point = { o.x, o.y };
    pimpl->D2DPlugin_execute(pimpl->pluginHandle, &op);
}

void PluginGraphicsContext::addTransform (const AffineTransform& transform)
{
    Direct2DPluginOp op;
    op.op = Direct2DPluginOp_addTransform;
    op.u.transform = { transform.mat00, transform.mat01, transform.mat02, transform.mat10, transform.mat11, transform.mat12 };
    pimpl->D2DPlugin_execute(pimpl->pluginHandle, &op);
}

bool PluginGraphicsContext::clipToRectangle (const Rectangle<int>& r)
{
    return ! isClipEmpty();
}

bool PluginGraphicsContext::clipToRectangleList (const RectangleList<int>& newClipList)
{

    return true;
}

void PluginGraphicsContext::excludeClipRectangle (const Rectangle<int>& userSpaceExcludedRectangle)
{
    Direct2DPluginOp op;
    op.op = Direct2DPluginOp_excludeClipRectangle;
    op.u.intRect = { userSpaceExcludedRectangle.getX(), userSpaceExcludedRectangle.getY(), userSpaceExcludedRectangle.getWidth(), userSpaceExcludedRectangle.getHeight() };
    pimpl->D2DPlugin_execute(pimpl->pluginHandle, &op);
}

void PluginGraphicsContext::clipToPath (const Path& path, const AffineTransform& transform)
{
}

void PluginGraphicsContext::clipToImageAlpha (const Image& sourceImage, const AffineTransform& transform)
{
}

bool PluginGraphicsContext::clipRegionIntersects (const Rectangle<int>& r)
{
   return false;
}

Rectangle<int> PluginGraphicsContext::getClipBounds() const
{
    return {};
}

bool PluginGraphicsContext::isClipEmpty() const
{
    return false;
}

void PluginGraphicsContext::saveState()
{
}

void PluginGraphicsContext::restoreState()
{
}

void PluginGraphicsContext::beginTransparencyLayer (float opacity)
{
}

void PluginGraphicsContext::endTransparencyLayer()
{
}

void PluginGraphicsContext::setFill (const FillType& fillType)
{
}

void PluginGraphicsContext::setOpacity (float newOpacity)
{
}

void PluginGraphicsContext::setInterpolationQuality (Graphics::ResamplingQuality quality)
{
}

void PluginGraphicsContext::fillRect (const Rectangle<int>& r, bool replaceExistingContents)
{
}

void PluginGraphicsContext::fillRect (const Rectangle<float>& r)
{
}

void PluginGraphicsContext::fillRectList (const RectangleList<float>& list)
{
}

void PluginGraphicsContext::drawRect (const Rectangle<float>& r, float lineThickness)
{
}

void PluginGraphicsContext::fillPath (const Path& p, const AffineTransform& transform)
{
}

void PluginGraphicsContext::strokePath (const Path& p, const PathStrokeType& strokeType, const AffineTransform& transform)
{
}

void PluginGraphicsContext::drawImage (const Image& imageIn, const AffineTransform& transform)
{
}

void PluginGraphicsContext::drawLine (const Line<float>& line)
{
}

void PluginGraphicsContext::drawLineWithThickness (const Line<float>& line, float lineThickness)
{
}

void PluginGraphicsContext::setFont (const Font& newFont)
{
}

const Font& PluginGraphicsContext::getFont()
{
    return tempFont;
}

float PluginGraphicsContext::getPhysicalPixelScaleFactor() const
{
    return 1.0f;
}

void PluginGraphicsContext::drawRoundedRectangle (const Rectangle<float>& area, float cornerSize, float lineThickness)
{
}

void PluginGraphicsContext::fillRoundedRectangle (const Rectangle<float>& area, float cornerSize)
{
}

void PluginGraphicsContext::drawEllipse (const Rectangle<float>& area, float lineThickness)
{
}

void PluginGraphicsContext::fillEllipse (const Rectangle<float>& area)
{
}

void PluginGraphicsContext::drawGlyphs (Span<const uint16_t> glyphNumbers,
                                          Span<const Point<float>> positions,
                                          const AffineTransform& transform)
{
}

std::unique_ptr<ImageType> PluginGraphicsContext::getPreferredImageTypeForTemporaryImages() const noexcept
{
    return std::make_unique<SoftwareImageType>();
}

bool PluginGraphicsContext::startFrame(float dpiScale, bool sizing)
{
    return D2DPlugin_startFrame(pimpl->pluginHandle, dpiScale, (int)sizing);
}

void PluginGraphicsContext::endFrame()
{
    D2DPlugin_endFrame();
}

void PluginGraphicsContext::createResources()
{

}

} // namespace juce
