
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
        direct2d::ScopedEvent wakeEvent;
        CriticalSection lock;
        Array<HANDLE> handles;
        std::atomic<int64> atomicReadyFlags;

    public:
        SwapChainDispatcher()
            : Thread ("SwapChainDispatcher")
        {
        }

        ~SwapChainDispatcher() override
        {
            stop();
        }

        int addSwapChain(HANDLE swapChainEvent)
        {
            {
                ScopedLock locker{ lock };

                int index = handles.indexOf(swapChainEvent);
                if (index >= 0)
                {
                    return index + 1;
                }

                handles.add({ swapChainEvent });
            }

            if (!isThreadRunning())
            {
                startThread(Thread::Priority::highest);
            }
            else
            {
                SetEvent(wakeEvent.getHandle());
            }
            
            return handles.size();
        }

        void removeSwapChain(HANDLE swapChainEvent)
        {
            {
                ScopedLock locker{ lock };

                handles.removeAllInstancesOf(swapChainEvent);
            }

            if (handles.size() > 0)
            {
                SetEvent(wakeEvent.getHandle());
            }
            else
            {
                stop();
            }
        }

        void stop()
        {
            jassert(MessageManager::getInstance()->isThisTheMessageThread());

            signalThreadShouldExit();
            SetEvent(wakeEvent.getHandle());
            stopThread(1000);
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
                HANDLE waitableObjects[MAXIMUM_WAIT_OBJECTS] = { wakeEvent.getHandle() };
                DWORD numWaitableObjects = 1;
                {
                    ScopedLock locker{ lock };

                    numWaitableObjects += jmin(MAXIMUM_WAIT_OBJECTS - 1, handles.size());
                    for (DWORD index = 0; index < numWaitableObjects - 1; ++index)
                    {
                        waitableObjects[index + 1] = handles[index];
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
