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

//==============================================================================
struct PluginGraphicsContext::Pimpl 
{
protected:
    PluginGraphicsContext& owner;

public:
    explicit Pimpl (PluginGraphicsContext& ownerIn)
        : owner (ownerIn)
    {
    }

    ~Pimpl()
    {
    }

    JUCE_DECLARE_WEAK_REFERENCEABLE (Pimpl)
};

//==============================================================================
PluginGraphicsContext::PluginGraphicsContext() = default;
PluginGraphicsContext::~PluginGraphicsContext() = default;

void PluginGraphicsContext::setOrigin (Point<int> o)
{
}

void PluginGraphicsContext::addTransform (const AffineTransform& transform)
{
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

} // namespace juce
