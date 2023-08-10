#pragma once

namespace juce
{
    namespace etw
    {
        enum
        {
            paintKeyword = 1,
            sizeKeyword = 2,
            graphicsKeyword = 4,
            crucialKeyword = 8,
            threadPaintKeyword = 16,
            messageKeyword = 32,
            direct2dKeyword = 64,
            softwareRendererKeyword = 128
        };

        enum
        {
            direct2dPaintStart = 0xd2d0000,
            direct2dPaintEnd,
            present1SwapChainStart,
            present1SwapChainEnd,
            presentDoNotSequenceStart,
            presentDoNotSequenceEnd,
            swapChainThreadEvent,
            waitForVBlankDone,
            resize,
            swapChainMessage,
            parentWindowMessage,
            childWindowMessage,
            direct2dStartFrame,
            childWindowSetSize,
            createResource,
            presentIdleFrame
        };
    }
}

#if JUCE_ETW_TRACELOGGING

#define JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE ETWGlobalTraceLoggingProvider

TRACELOGGING_DECLARE_PROVIDER (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE);

#define TraceLoggingWriteWrapper(hProvider, eventName, ...) TraceLoggingWrite(hProvider, eventName, __VA_ARGS__)

#else

#define TraceLoggingWriteWrapper(hProvider, eventName, ...)

#endif

#define TRACE_LOG_D2D(code) \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                   # code, \
                   TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                   TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                   TraceLoggingInt32 (code, "code"))

#define TRACE_LOG_D2D_CREATE_RESOURCE(name) \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                   "Create " # name, \
                   TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                   TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                   TraceLoggingString(name, "resource"), \
                   TraceLoggingInt32 (etw::createResource, "code"))

#define TRACE_LOG_D2D_START_FRAME \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                   "D2D start frame ", \
                   TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                   TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                   TraceLoggingInt32 (etw::direct2dStartFrame, "code"))

#define TRACE_LOG_D2D_PAINT_START(frameNumber) \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                   "D2D paint start", \
                   TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                   TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                   TraceLoggingInt32 (frameNumber, "frame"), \
                   TraceLoggingInt32 (etw::direct2dPaintStart, "code"))

#define TRACE_LOG_D2D_PAINT_END(frameNumber) \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                       "D2D paint end", \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                       TraceLoggingInt32 (frameNumber, "frame"),                       \
                       TraceLoggingInt32 (etw::direct2dPaintEnd, "code"))

#define TRACE_LOG_D2D_PRESENT1_START(frameNumber)                                         \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,                          \
                       "D2D present1 start",                                           \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION),                    \
                       TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                       TraceLoggingInt32 (frameNumber, "frame"),                       \
                       TraceLoggingInt32 (etw::present1SwapChainStart, "code"))

#define TRACE_LOG_D2D_PRESENT1_END(frameNumber)                                         \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,                          \
                       "D2D present1 end",                                           \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION),                    \
                       TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                       TraceLoggingInt32 (frameNumber, "frame"),                       \
                       TraceLoggingInt32 (etw::present1SwapChainEnd, "code"))

#define TRACE_LOG_PRESENT_DO_NOT_SEQUENCE_START(frameNumber)                           \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,                          \
                       "D2D present do-not-sequence start",                            \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION),                    \
                       TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                       TraceLoggingInt32 (frameNumber, "frame"),                       \
                       TraceLoggingInt32 (etw::presentDoNotSequenceStart, "code"))

#define TRACE_LOG_PRESENT_DO_NOT_SEQUENCE_END(frameNumber)                             \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,                          \
                       "D2D present do-not-sequence end",                              \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION),                    \
                       TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                       TraceLoggingInt32 (frameNumber, "frame"),                       \
                       TraceLoggingInt32 (etw::presentDoNotSequenceEnd, "code"))

#define TRACE_LOG_SWAP_CHAIN_EVENT                                  \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,       \
                       "Swap chain thread event",                   \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                       TraceLoggingInt32 (etw::swapChainThreadEvent, "code"))

#define TRACE_LOG_SWAP_CHAIN_MESSAGE                                  \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,       \
                       "Swap chain ready message",                   \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                       TraceLoggingInt32 (etw::swapChainMessage, "code"))

#define TRACE_LOG_JUCE_VBLANK_THREAD_EVENT                          \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,       \
                       "VBlankThread WaitForVBlank done",           \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::softwareRendererKeyword), \
                        TraceLoggingInt32(etw::waitForVBlankDone, "code"))

#define TRACE_LOG_D2D_RESIZE(message)                                                \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,                          \
                              "D2D resize",                                              \
                              TraceLoggingLevel (TRACE_LEVEL_INFORMATION),                    \
                              TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                               TraceLoggingInt32 (message, "message"),\
                              TraceLoggingInt32 (etw::resize, "code"))

#define TRACE_LOG_PARENT_WINDOW_MESSAGE(message)                               \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,       \
                       "Parent window message",                   \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::messageKeyword), \
                       TraceLoggingInt32 (message, "message"),\
                       TraceLoggingInt32 (etw::parentWindowMessage, "code"))


#define TRACE_LOG_CHILD_WINDOW_MESSAGE(message)                            \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,       \
                       "Child window message",                   \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::messageKeyword), \
                       TraceLoggingInt32(message, "message"), \
                       TraceLoggingInt32 (etw::childWindowMessage, "code"))
