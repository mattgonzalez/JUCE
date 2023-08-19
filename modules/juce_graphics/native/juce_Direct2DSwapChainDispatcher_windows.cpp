
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
        HANDLE wakeEvent = nullptr;
        CriticalSection lock;
        struct SwapChain
        {
            HANDLE swapChainEvent;
        };
        Array<SwapChain> swapChains;
        std::atomic<int64> atomicReadyFlags;

    public:
        SwapChainDispatcher()
            : Thread ("SwapChainDispatcher")
        {
            wakeEvent = CreateEvent (nullptr, FALSE, FALSE, nullptr);
        }

        ~SwapChainDispatcher() override
        {
            stop();
        }

        int addSwapChain(HANDLE swapChainEvent)
        {
            {
                ScopedLock locker{ lock };

                for (int index = 0; index < swapChains.size(); ++index)
                {
                    if (swapChains[index].swapChainEvent == swapChainEvent)
                    {
                        return index + 1;
                    }
                }

                swapChains.add({ swapChainEvent });
            }

            startThread(Thread::Priority::highest);

            SetEvent(wakeEvent);

            return swapChains.size();
        }

        void removeSwapChain(int bitNumber)
        {
            if (1 <= bitNumber && bitNumber < swapChains.size())
            {
                ScopedLock locker{ lock };

                swapChains.remove(bitNumber - 1);
            }

            SetEvent(wakeEvent);

            if (swapChains.size() == 0)
            {
                stop();
            }
        }

        void stop()
        {
            jassert(MessageManager::getInstance()->isThisTheMessageThread());

            SetEvent(wakeEvent);
            stopThread(1000);
            CloseHandle(wakeEvent);
        }

        bool isSwapChainReady(int bitNumber)
        {
            int64 mask = 1LL << bitNumber;
            auto readyFlags = atomicReadyFlags.fetch_and(~mask);
            return (readyFlags & mask) != 0;
        }

        void run() override
        {
            while (!threadShouldExit())
            {
                //
                // Copy event handles to local array
                //
                HANDLE waitableObjects[MAXIMUM_WAIT_OBJECTS] = { wakeEvent };
                DWORD numWaitableObjects;
                {
                    ScopedLock locker{ lock };

                    numWaitableObjects = 1 + jmin(MAXIMUM_WAIT_OBJECTS - 1, swapChains.size());
                    for (DWORD index = 0; index < numWaitableObjects - 1; ++index)
                    {
                        waitableObjects[index + 1] = swapChains[index].swapChainEvent;
                    }
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
                    TRACE_LOG_SWAP_CHAIN_EVENT(bitNumber);
                    atomicReadyFlags.fetch_or(1LL << bitNumber);
                    continue;
                }

                //
                // Maybe the wake event or an error?
                //
                switch (waitResult)
                {
                case WAIT_OBJECT_0: // wake event fired
                case WAIT_FAILED:
                {
                    break;
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
