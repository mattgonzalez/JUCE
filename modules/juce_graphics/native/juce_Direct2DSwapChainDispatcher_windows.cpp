
#ifdef __INTELLISENSE__

#define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
#define JUCE_WINDOWS 1

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

    class SwapChainDispatcher : protected Thread
    {
    private:
        HANDLE shutdownEvent = nullptr;
        CriticalSection lock;
        Array<HANDLE> swapChainEvents;
        std::atomic<int64> atomicReadyFlags;

    public:
        SwapChainDispatcher()
            : Thread ("SwapChainDispatcher")
        {
            shutdownEvent = CreateEvent (nullptr, FALSE, FALSE, nullptr);
        }

        ~SwapChainDispatcher() override
        {
            stop();
            CloseHandle(shutdownEvent);
        }

        void start ()
        {
            startThread (Thread::Priority::highest);
        }

        void stop()
        {
            jassert (MessageManager::getInstance()->isThisTheMessageThread());

            SetEvent(shutdownEvent);
            stopThread (1000);
        }

        void run() override
        {
            while (!threadShouldExit())
            {
                //
                // Copy event handles to local array
                //
                HANDLE waitableObjects[MAXIMUM_WAIT_OBJECTS] = { shutdownEvent };
                DWORD numWaitableObjects = 1;
                {
                    ScopedLock locker{ lock };

                    numWaitableObjects = jmin(MAXIMUM_WAIT_OBJECTS - 1, swapChainEvents.size());
                    memcpy(waitableObjects + 1, swapChainEvents.getRawDataPointer(), numWaitableObjects - 1);
                }

                //
                // Wait for an event to fire
                //
                auto waitResult = WaitForMultipleObjects(numWaitableObjects, waitableObjects, FALSE, INFINITE);

                //
                // Swap chain event fired?
                //
                if (WAIT_OBJECT_0 < waitResult && waitResult < WAIT_OBJECT_0 + numWaitableObjects)
                {
                    int bitNumber = waitResult - WAIT_OBJECT_0;
                    atomicReadyFlags.fetch_or(bitNumber);
                    continue;
                }

                switch (waitResult)
                {
                case WAIT_OBJECT_0: // shutdown event fired
                {
                    return;
                }

                default:
                {
                    jassertfalse;
                    break;
                }
                }
            }
        }
    };

} // namespace direct2d

} // namespace juce
