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

class Direct2DHwndContext : public Direct2DGraphicsContext
{
public:
    Direct2DHwndContext (HWND, float dpiScalingFactor, bool opaque);
    ~Direct2DHwndContext() override;

    void handleShowWindow();
    void setWindowAlpha (float alpha);

    void setSize (int width, int height);
    void updateSize();

    void addDeferredRepaint (Rectangle<int> deferredRepaint);
    void addInvalidWindowRegionToDeferredRepaints();

    Image createSnapshot(Rectangle<int> deviceIndependentArea) override;
    Image createSnapshot();

    bool isReady() const noexcept;

    void drawImageSection (Image const& image, Rectangle<int> sourceArea, Point<int> destination);

private:
    struct HwndPimpl;
    std::unique_ptr<HwndPimpl> pimpl;

    Pimpl* const getPimpl() const noexcept override;
    void clearTargetBuffer() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Direct2DHwndContext)
};

} // namespace juce
