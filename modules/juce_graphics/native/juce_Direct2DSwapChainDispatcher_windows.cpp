
#ifdef __INTELLISENSE__

    #define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
    #define JUCE_WINDOWS                    1

    #include <windows.h>
    #include <juce_core/juce_core.h>
    #include <juce_graphics/juce_graphics.h>
    #include <d2d1_2.h>
    #include <d3d11_1.h>
    #include <dwrite.h>
    #include <dcomp.h>
    #include "juce_win32_Direct2DHelpers.cpp"
    #include "juce_win32_Direct2DCommandQueue.cpp"

#endif

#define JUCE_DIRECT2D_DIRECT_COMPOSITION 1

namespace juce
{
namespace direct2d
{

//==============================================================================
//
// SwapChainDispatcher
//
// Every D2D window has a waitable swap chain. The waitable swap chain provides
// an event that signals when the swap chain is ready.
// 
// This thread waits for the events from all the swap chains and sets an atomic
// flag bit when the swap chain event fires.
// 
// Direct2DComponentPeer will attempt to paint on the next onVBlank if the
// atomic flag bit is set for that specific swap chain.
// 
// Note that WaitForMultipleObjects can wait for a maximum of 64 objects. 
// The thread needs one dedicated event for shutdown notification, so that
// leaves a maximum of 63 swap chains that can be serviced.
//

class SwapChainDispatcher
{
public:
    explicit SwapChainDispatcher (ScopedEvent h)
        : events { { std::move (h), {} } } {}

    ~SwapChainDispatcher()
    {
        states |= (1 << quitting);
        SetEvent (events[quitting].getHandle());
        thread.join();
    }

    bool isSwapChainReady()
    {
        return states.fetch_and (~(1 << ready));
    }

private:
    enum States
    {
        ready    = 0,
        quitting = 1,
    };

    std::array<ScopedEvent, 2> events;
    std::atomic<int> states { 0 };
    std::thread thread
    {
        [&]
        {
            while ((states & (1 << quitting)) == 0)
            {
                std::array<HANDLE, 2> handles;
                std::transform (events.begin(), events.end(), handles.begin(), [] (const auto& s) { return s.getHandle(); });

                const auto waitResult = WaitForMultipleObjects ((DWORD) handles.size(), handles.data(), FALSE, INFINITE);

                switch (waitResult)
                {
                    case WAIT_OBJECT_0 + ready:
                        states |= (1 << ready);
                        break;

                    case WAIT_OBJECT_0 + quitting:
                    case WAIT_FAILED:
                        break;

                    default:
                        jassertfalse;
                        break;
                }
            }
        }
    };
};

} // namespace direct2d

} // namespace juce
