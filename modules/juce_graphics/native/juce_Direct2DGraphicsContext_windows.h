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

#if ! defined(_WINDEF_) && ! defined(__INTELLISENSE__)
class HWND__; // Forward or never
typedef HWND__* HWND;
#endif

#if JUCE_DIRECT2D_METRICS

namespace direct2d
{
    struct PaintStats : public ReferenceCountedObject
    {
        enum
        {
            messageThreadPaintDuration,
            frameInterval,
            presentDuration,
            present1Duration,
            swapChainEventInterval,
            swapChainMessageTransitTime,
            swapChainMessageInterval,
            vblankToBeginDraw,

            numStats
        };

        StringArray const accumulatorNames {
            "messageThreadPaintDuration",
            "frameInterval",
            "presentDuration",
            "present1Duration",
            "swapChainEventInterval",
            "swapChainMessageTransitTime",
            "swapChainMessageInterval",
            "VBlank to BeginDraw"
        };

        int64 const creationTime = Time::getMillisecondCounter();
        double const millisecondsPerTick = 1000.0 / (double) Time::getHighResolutionTicksPerSecond();
        int paintCount = 0;
        int presentCount = 0;
        int present1Count = 0;
        int64 lastPaintStartTicks = 0;
        uint64 lockAcquireMaxTicks = 0;

        ~PaintStats()
        {
        }

        void reset()
        {
            for (auto& accumulator : accumulators)
            {
                accumulator.reset();
            }
            lastPaintStartTicks = 0;
            paintCount = 0;
            present1Count = 0;
            lockAcquireMaxTicks = 0;
        }

        using Ptr = ReferenceCountedObjectPtr<PaintStats>;

        StatisticsAccumulator<double> const& getAccumulator (int index) const
        {
            return accumulators[index];
        }

        void addValueTicks (int index, int64 ticks)
        {
            addValueMsec (index, Time::highResolutionTicksToSeconds (ticks) * 1000.0);
        }

        void addValueMsec (int index, double value)
        {
            accumulators[index].addValue (value);
        }

    private:
        StatisticsAccumulator<double> accumulators[numStats];
    };

    struct ScopedElapsedTime
    {
        ScopedElapsedTime (PaintStats::Ptr stats_, int accumulatorIndex_) : stats (stats_),
                                                                            accumulatorIndex (accumulatorIndex_)
        {
        }

        ~ScopedElapsedTime()
        {
            auto finishTicks = Time::getHighResolutionTicks();
            stats->addValueTicks (accumulatorIndex, finishTicks - startTicks);
        }

        int64 startTicks = Time::getHighResolutionTicks();
        PaintStats::Ptr stats;
        int accumulatorIndex;
    };
} // namespace direct2d

#endif

namespace direct2d
{
    struct SwapChainListener
    {
        virtual ~SwapChainListener() = default;

        virtual void swapChainSignaledReady() = 0;
        virtual void swapChainTimedOut() {}

        JUCE_DECLARE_WEAK_REFERENCEABLE(SwapChainListener)
    };
};

#if JUCE_ETW_TRACELOGGING

struct ETWEventProvider
{
    ETWEventProvider();
    ~ETWEventProvider();
};

#endif

class Direct2DLowLevelGraphicsContext : public LowLevelGraphicsContext
{
public:
    Direct2DLowLevelGraphicsContext(HWND, direct2d::SwapChainListener* const, double dpiScalingFactor, bool opaque, bool temporaryWindow);
    ~Direct2DLowLevelGraphicsContext() override;

    void handleChildWindowChange (bool visible);
    void setWindowAlpha(float alpha);

    //==============================================================================
    bool isVectorDevice() const override { return false; }

    void setOrigin (Point<int>) override;
    void addTransform (const AffineTransform&) override;
    float getPhysicalPixelScaleFactor() override;
    bool clipToRectangle (const Rectangle<int>&) override;
    bool clipToRectangleList (const RectangleList<int>&) override;
    void excludeClipRectangle (const Rectangle<int>&) override;
    void clipToPath (const Path&, const AffineTransform&) override;
    void clipToImageAlpha (const Image&, const AffineTransform&) override;
    bool clipRegionIntersects (const Rectangle<int>&) override;
    Rectangle<int> getClipBounds() const override;
    bool isClipEmpty() const override;

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
    bool drawRect (const Rectangle<float>&, float) override;
    void fillPath (const Path&, const AffineTransform&) override;
    bool drawPath (const Path&, const PathStrokeType& strokeType, const AffineTransform&) override;
    void drawImage (const Image& sourceImage, const AffineTransform&) override;

    //==============================================================================
    void drawLine (const Line<float>&) override;
    bool drawLine (const Line<float>&, float) override;
    void setFont (const Font&) override;
    const Font& getFont() override;
    void drawGlyph (int glyphNumber, const AffineTransform&) override;
    bool supportsGlyphRun() override { return true; }
    void drawGlyphRun (Array<PositionedGlyph> const& glyphs, 
        int startIndex, 
        int numGlyphs, 
        const AffineTransform& transform,
                       Rectangle<float> underlineArea) override;
    bool drawTextLayout (const AttributedString&, const Rectangle<float>&) override;

    void startResizing();
    void resize();
    void resize (int width, int height);
    void finishResizing();
    void restoreWindow();

    void addDeferredRepaint (Rectangle<int> deferredRepaint);
    void addInvalidWindowRegionToDeferredRepaints();
    bool startFrame();
    void endFrame();

    void setScaleFactor (double scale_);
    double getScaleFactor() const;

    bool drawRoundedRectangle (Rectangle<float> area, float cornerSize, float lineThickness) override;
    bool fillRoundedRectangle (Rectangle<float> area, float cornerSize) override;

    bool drawEllipse (Rectangle<float> area, float lineThickness) override;
    bool fillEllipse (Rectangle<float> area) override;

    static uint32 constexpr customMessageID = 0x400 + 0xd2d; // WM_USER + 0xd2d
    static int constexpr minWindowSize = 1;
    static int constexpr maxWindowSize = 16384;

#if JUCE_DIRECT2D_METRICS
    direct2d::PaintStats::Ptr stats;
#endif

    //==============================================================================
private:
    struct ClientSavedState;
    ClientSavedState* currentState = nullptr;

    struct Pimpl;
    std::unique_ptr<Pimpl> pimpl;

    void drawGlyphCommon(int numGlyphs, const AffineTransform& transform, Rectangle<float> underlineArea);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Direct2DLowLevelGraphicsContext)
};

} // namespace juce
