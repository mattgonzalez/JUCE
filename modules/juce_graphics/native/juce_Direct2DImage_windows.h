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

    namespace direct2d
    {

        //==============================================================================
        //
        // DPIScalableArea keeps track of an area for a window or a bitmap both in
        // terms of device-independent pixels and physical pixels, as well as the DPI
        // scaling factor.
        //
        template <class valueType>
        class DPIScalableArea
        {
        public:
            static DPIScalableArea fromDeviceIndependentArea(Rectangle<valueType> dipArea, float dpiScalingFactor)
            {
                DPIScalableArea scalableArea;
                scalableArea.deviceIndependentArea = dipArea;
                scalableArea.dpiScalingFactor = dpiScalingFactor;

                //
                // These need to round to the nearest integer, so use roundToInt instead of the standard Rectangle methods
                //
                Rectangle<float> physicalArea = dipArea.toFloat() * dpiScalingFactor;
                scalableArea.physicalArea =
                {
                    roundToInt(physicalArea.getX()),
                    roundToInt(physicalArea.getY()),
                    roundToInt(physicalArea.getWidth()),
                    roundToInt(physicalArea.getHeight())
                };

                return scalableArea;
            }

            static DPIScalableArea fromPhysicalArea(Rectangle<valueType> physicalArea, float dpiScalingFactor)
            {
                DPIScalableArea scalableArea;
                scalableArea.dpiScalingFactor = dpiScalingFactor;
                scalableArea.physicalArea = physicalArea;

                //
                // These need to round to the nearest integer, so use roundToInt instead of the standard Rectangle methods
                //
                Rectangle<float> dipArea = physicalArea.toFloat() / dpiScalingFactor;
                scalableArea.deviceIndependentArea =
                {
                    roundToInt(dipArea.getX()),
                    roundToInt(dipArea.getY()),
                    roundToInt(dipArea.getWidth()),
                    roundToInt(dipArea.getHeight())
                };

                return scalableArea;
            }

            bool isEmpty() const noexcept
            {
                return deviceIndependentArea.isEmpty();
            }

            float getDPIScalingFactor() const noexcept
            {
                return dpiScalingFactor;
            }

            auto getDeviceIndependentArea() const noexcept
            {
                return deviceIndependentArea;
            }

            auto getPhysicalArea() const noexcept
            {
                return physicalArea;
            }

            valueType getDeviceIndependentWidth() const noexcept { return deviceIndependentArea.getWidth(); };

            valueType getDeviceIndependentHeight() const noexcept { return deviceIndependentArea.getHeight(); };

            void clipToPhysicalArea(Rectangle<valueType> clipArea)
            {
                *this = fromPhysicalArea(physicalArea.getIntersection(clipArea), dpiScalingFactor);
            }

            DPIScalableArea withZeroOrigin() const noexcept
            {
                return DPIScalableArea
                {
                    deviceIndependentArea.withZeroOrigin(),
                    physicalArea.withZeroOrigin(),
                    dpiScalingFactor
                };
            }

        private:
            DPIScalableArea() = default;

            DPIScalableArea(Rectangle<valueType> deviceIndependentArea_,
                Rectangle<valueType> physicalArea_,
                float dpiScalingFactor_) :
                deviceIndependentArea(deviceIndependentArea_),
                physicalArea(physicalArea_),
                dpiScalingFactor(dpiScalingFactor_)
            {
            }

            Rectangle<valueType> deviceIndependentArea;
            Rectangle<valueType> physicalArea;
            float dpiScalingFactor = 1.0f;
        };

    }

    using Direct2DImageType = NativeImageType;

} // namespace juce