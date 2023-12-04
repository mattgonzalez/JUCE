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

    class Direct2ImageContext : public Direct2DGraphicsContext
    {
    public:
        /** Creates a context to render into an image. */
        Direct2ImageContext(bool clearImage_);

        ~Direct2ImageContext() override;

        void startFrame(ID2D1Bitmap1* bitmap, float dpiScaleFactor);

    private:
        bool clearImage = true;

        struct ImagePimpl;
        std::unique_ptr<ImagePimpl> pimpl;

        Pimpl* getPimpl() const noexcept override;
        void clearTargetBuffer() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Direct2ImageContext)
    };

} // namespace juce
