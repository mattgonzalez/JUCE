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

#if JUCE_DIRECT2D_METRICS

namespace juce
{

String Direct2DMetricsHub::getProcessString() noexcept
{
    auto processID = GetCurrentProcessId();
    auto string = String{ (pointer_sized_int)processID };

    auto processHandle = GetCurrentProcess();
    wchar_t processNameBuffer[256];
    DWORD numWchar = numElementsInArray(processNameBuffer);
    if (QueryFullProcessImageNameW(processHandle,
        0,
        processNameBuffer,
        &numWchar))
    {
        File file{ juce::String{ processNameBuffer, numWchar } };
        string =  file.getFileNameWithoutExtension() + "_" + string;
    }

    return string;
}

void Direct2DMetricsHub::HubPipeServer::messageReceived (const MemoryBlock& message)
{
    auto request = (Request*) message.getData();
    if (!request || message.getSize() < sizeof (Request))
        return;

    switch (request->requestType)
    {
        case getValuesRequest:
        {
            ScopedLock locker { owner.lock };

            Direct2DMetrics::Ptr metrics = nullptr;
            if (request->windowHandle)
            {
                for (int i = 0; i < owner.metricsArray.size(); ++i)
                {
                    auto arrayEntry = owner.metricsArray[i];
                    if (arrayEntry->windowHandle && arrayEntry->windowHandle == request->windowHandle)
                    {
                        metrics = arrayEntry;
                        break;
                    }
                }
            }

            if (! metrics)
            {
                metrics = owner.imageContextMetrics;
            }

            if (metrics)
            {
                MemoryBlock block { sizeof (GetValuesResponse), true };

                auto* response = (GetValuesResponse*) block.getData();
                response->responseType = getValuesRequest;
                response->windowHandle = metrics->windowHandle;

                for (size_t i = 0; i <= Direct2DMetrics::drawGlyphRunTime; ++i)
                {
                    auto& accumulator = metrics->getAccumulator (i);
                    response->values[i].count = accumulator.getCount();
                    response->values[i].total = metrics->getSum (i);
                    response->values[i].average = accumulator.getAverage();
                    response->values[i].minimum = accumulator.getMinValue();
                    response->values[i].maximum = accumulator.getMaxValue();
                    response->values[i].stdDev = accumulator.getStandardDeviation();
                }

                // Track bitmap operations common to all device contexts
                for (size_t i = Direct2DMetrics::createBitmapTime; i <= Direct2DMetrics::unmapBitmapTime; ++i)
                {
                    auto& accumulator = owner.imageContextMetrics->getAccumulator (i);
                    response->values[i].count = accumulator.getCount();
                    response->values[i].total = metrics->getSum (i);
                    response->values[i].average = accumulator.getAverage();
                    response->values[i].minimum = accumulator.getMinValue();
                    response->values[i].maximum = accumulator.getMaxValue();
                    response->values[i].stdDev = accumulator.getStandardDeviation();
                }

                sendMessage (block);
            }
            break;
        }

        case resetValuesRequest:
        {
            owner.resetAll();
            break;
        }

        case getWindowHandlesRequest:
        {
            MemoryBlock block{ sizeof(GetWindowHandlesResponse), true };
            auto response = (GetWindowHandlesResponse*)block.getData();

            ScopedLock locker{ owner.lock };

            int numWindowHandles = jmin(owner.metricsArray.size(), numElementsInArray(response->windowHandles));
            for (int i = 0; i < numWindowHandles; ++i)
            {
                auto const& metrics = owner.metricsArray[i];
                if (metrics->windowHandle)
                {
                    response->windowHandles[i] = metrics->windowHandle;
                }
            }

            response->responseType = getWindowHandlesRequest;

            sendMessage(block);
            break;
        }
    }
}

void Direct2DMetricsHub::resetAll()
{
    ScopedLock locker { lock };

    imageContextMetrics->reset();
    for (auto metrics : metricsArray)
    {
        metrics->reset();
    }
}

} // namespace juce

#endif
