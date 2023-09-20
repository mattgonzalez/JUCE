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

namespace juce
{

class Direct2DPixelData;

class Direct2DLowLevelGraphicsImageContext : public LowLevelGraphicsContext
{
public:
    /** Creates a context to render into an image. */
    Direct2DLowLevelGraphicsImageContext(ReferenceCountedObjectPtr<Direct2DPixelData> direct2DPixelData_);

    /** Creates a context to render into a clipped subsection of an image. */
    Direct2DLowLevelGraphicsImageContext(ReferenceCountedObjectPtr<Direct2DPixelData> direct2DPixelData_, Point<int> origin, const RectangleList<int>& initialClip, bool clearImage_ = true);

    ~Direct2DLowLevelGraphicsImageContext() override;

    //==============================================================================
    bool isVectorDevice() const override
    {
        return false;
    }

    void           setOrigin (Point<int>) override;
    void           addTransform (const AffineTransform&) override;
    float          getPhysicalPixelScaleFactor() override;
    bool           clipToRectangle (const Rectangle<int>&) override;
    bool           clipToRectangleList (const RectangleList<int>&) override;
    void           excludeClipRectangle (const Rectangle<int>&) override;
    void           clipToPath (const Path&, const AffineTransform&) override;
    void           clipToImageAlpha (const Image&, const AffineTransform&) override;
    bool           clipRegionIntersects (const Rectangle<int>&) override;
    Rectangle<int> getClipBounds() const override;
    bool           isClipEmpty() const override;

    //==============================================================================
    void saveState() override;
    void restoreState() override;
    void beginTransparencyLayer (float opacity) override;
    void endTransparencyLayer() override;

    //==============================================================================
    void setFill (const FillType&) override;
    void setOpacity (float) override;
    void setInterpolationQuality (Graphics::ResamplingQuality) override;

    //==============================================================================
    void fillRect (const Rectangle<int>&, bool replaceExistingContents) override;
    void fillRect (const Rectangle<float>&) override;
    void fillRectList (const RectangleList<float>&) override;
    void fillPath (const Path&, const AffineTransform&) override;
    void drawImage (const Image& sourceImage, const AffineTransform&) override;

    //==============================================================================
    void        drawLine (const Line<float>&) override;
    void        setFont (const Font&) override;
    const Font& getFont() override;
    void        drawGlyph (int glyphNumber, const AffineTransform&) override;
    bool drawTextLayout (const AttributedString&, const Rectangle<float>&) override;

    void startResizing();
    void resize();
    void resize (int width, int height);
    void finishResizing();
    void restoreWindow();

    bool startFrame();
    void endFrame();


    //==============================================================================
    //
    // These methods are not part of the standard LowLevelGraphicsContext; they
    // were added because Direct2D supports these drawing primitives
    // 
    // Standard LLGC only supports drawing one glyph at a time; it's much more
    // efficient to pass an entire run of glyphs to the device context
    //
    bool drawLine (const Line<float>&, float) override;

    bool drawEllipse (Rectangle<float> area, float lineThickness) override;
    bool fillEllipse (Rectangle<float> area) override;

    bool drawRect (const Rectangle<float>&, float) override;
    bool drawPath (const Path&, const PathStrokeType& strokeType, const AffineTransform&) override;

    bool drawRoundedRectangle (Rectangle<float> area, float cornerSize, float lineThickness) override;
    bool fillRoundedRectangle (Rectangle<float> area, float cornerSize) override;

    bool supportsGlyphRun() override
    {
        return true;
    }
    void drawGlyphRun (Array<PositionedGlyph> const& glyphs,
                       int                           startIndex,
                       int                           numGlyphs,
                       const AffineTransform&        transform,
                       Rectangle<float>              underlineArea) override;

    enum
    {
        createChildWindowMessageID = 0x400 + 0xd2d, // WM_USER + 0xd2d
        removeChildWindowMessageID,
        childWindowCreatedMessageID
    };

    //==============================================================================
    //
    // Min & max windows sizes; same as Direct3D texture size limits
    // 
    static int constexpr minFrameSize = 1;
    static int constexpr maxFrameSize = 16384;

#if JUCE_DIRECT2D_METRICS
    direct2d::PaintStats::Ptr stats;
#endif

    //==============================================================================
private:
    friend class Direct2DPixelData;

    bool clearImage = true;

    struct SavedState;
    SavedState* currentState = nullptr;

    struct Pimpl;
    std::unique_ptr<Pimpl> pimpl;

    void drawGlyphCommon (int numGlyphs, Font const& font, const AffineTransform& transform, Rectangle<float> underlineArea);
    void updateDeviceContextTransform();
    void updateDeviceContextTransform (AffineTransform chainedTransform);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Direct2DLowLevelGraphicsImageContext)
};

} // namespace juce
