
#ifdef __INTELLISENSE__

#define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
#define JUCE_WINDOWS 1

#include <windows.h>
#include <d2d1_2.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dwrite.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include "juce_win32_Direct2DHelpers.cpp"
#include "juce_win32_Direct2DPresentationThread.cpp"

#endif

namespace juce
{

namespace direct2d
{

    class ChildWindowThread
    {
    public:
        ChildWindowThread () : messageThread (this)
        {
            messageThread.start();
        }

        ~ChildWindowThread()
        {
            messageThread.stop();
        }

        bool isRunning() const noexcept { return messageThread.isThreadRunning(); }

        void setSize (HWND childHwnd, Rectangle<int> size)
        {
            TRACE_LOG_D2D(etw::childWindowSetSize);

            SetWindowPos(childHwnd, nullptr, 0, 0,
                size.getWidth(), size.getHeight(), SWP_DEFERERASE | SWP_NOREDRAW);
        }

        void createChildForParentWindow(HWND parentHwnd)
        {
            ScopedLock locker{ lock };

            for (auto const& expectantParentHwnd : expectantParentWindows)
            {
                if (expectantParentHwnd == parentHwnd)
                {
                    return;
                }
            }

            expectantParentWindows.add(parentHwnd);
            PostThreadMessage((DWORD)(pointer_sized_int)messageThread.getThreadId(), Direct2DLowLevelGraphicsContext::createChildWindowMessageID, 0, (LPARAM)parentHwnd);
        }

        void removeChildWindow(HWND childHwnd)
        {
            ScopedLock locker{ lock };

            for (int i = 0; i < windowHandlePairs.size(); ++i)
            {
                if (windowHandlePairs[i].childHwnd == childHwnd)
                {
                    windowHandlePairs.remove(i);
                    SendMessage(childHwnd, WM_CLOSE, 0, 0);
                    return;
                }
            }
        }

        struct Class
        {
            Class()
            {
                HMODULE moduleHandle = (HMODULE) Process::getCurrentModuleInstanceHandle();
                WNDCLASSEXW wcex = { sizeof (WNDCLASSEX) };
                wcex.style = CS_HREDRAW | CS_VREDRAW;
                wcex.lpfnWndProc = windowProc;
                wcex.cbClsExtra = 0;
                wcex.cbWndExtra = sizeof (LONG_PTR);
                wcex.hInstance = moduleHandle;
                wcex.hbrBackground = nullptr;
                wcex.lpszMenuName = nullptr;
                wcex.lpszClassName = className.toWideCharPointer();
                RegisterClassExW (&wcex);
            }

            ~Class()
            {
                HMODULE moduleHandle = (HMODULE) Process::getCurrentModuleInstanceHandle();
                UnregisterClassW (className.toWideCharPointer(), moduleHandle);
            }

            String const className { "JUCE_Direct2D_" + String::toHexString (Time::getHighResolutionTicks()) };
        } windowClass;

    private:
        CriticalSection lock;
        struct WindowHandlePair
        {
            HWND parentHwnd = nullptr;
            HWND childHwnd = nullptr;
        };
        Array<HWND> expectantParentWindows;
        Array<WindowHandlePair> windowHandlePairs;

        static LRESULT CALLBACK windowProc (
            HWND hwnd,
            UINT message,
            WPARAM wParam,
            LPARAM lParam)
        {
            //Logger::outputDebugString("child window proc " + String::toHexString((int)message));

            if (WM_CREATE == message)
            {
                //                     LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
                //                     ChildWindow* that = (ChildWindow*)pcs->lpCreateParams;
                //
                //                     SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(that));

                return 1;
            }

            //auto that = (ChildWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

            switch (message)
            {
                case WM_SHOWWINDOW:
                    return 0;

                case WM_ERASEBKGND:
                    return 1;

                case WM_PAINT:
                case WM_NCPAINT:
                    direct2d::UpdateRegion::forwardInvalidRegionToParent (hwnd);
                    return 0;

                case WM_WINDOWPOSCHANGED:
                    return 0;

                case WM_SIZE:
                    //Logger::outputDebugString("WM_SIZE " + juce::String::toHexString(lParam));
                    return 0;

                case WM_MOVE:
                    return 0;

                case WM_CLOSE:
                    DestroyWindow (hwnd);
                    return 0;

                case WM_DESTROY:
                    //PostMessage (hwnd, WM_QUIT, 0, 0);
                    return 0;

                default:
                    break;
            }

            return DefWindowProcW (hwnd, message, wParam, lParam);
        }

        struct MessageThread : public Thread
        {
            MessageThread (ChildWindowThread* const owner_) : Thread ("Direct2DMessageThread"),
                                                        owner (owner_)
            {
            }

            ~MessageThread() override
            {
                stop();
            }

            void start()
            {
                jassert (MessageManager::getInstance()->isThisTheMessageThread());
                startThread (Thread::Priority::normal);
            }

            void stop()
            {
                jassert (MessageManager::getInstance()->isThisTheMessageThread());

                signalThreadShouldExit();
                SetEvent(wakeEvent.getHandle());
                stopThread(1000);
            }

            void run() override
            {
                SetThreadDescription (GetCurrentThread(), getThreadName().toUTF16());

                runMessageLoop();
            }

            void createChildWindowForExpectantParent(HWND parentHwnd)
            {
                {
                    ScopedLock locker{ owner->lock };
	
                    if (!owner->expectantParentWindows.contains(parentHwnd))
                    {
                        return;
                    }
                    owner->expectantParentWindows.removeFirstMatchingValue(parentHwnd);
                }

                if (HWND childHwnd = createChildWindow(parentHwnd))
                {
                    ScopedLock locker { owner->lock };

                    owner->windowHandlePairs.add ({ parentHwnd, childHwnd });
                }
            }

            HWND createChildWindow(HWND parentHwnd)
            {
                RECT parentRect {};
                GetClientRect (parentHwnd, &parentRect);

                HMODULE moduleHandle = (HMODULE) Process::getCurrentModuleInstanceHandle();
                auto childHwnd = CreateWindowEx (WS_EX_NOREDIRECTIONBITMAP,
                                                   owner->windowClass.className.toWideCharPointer(),
                                                   nullptr,
                                                   WS_VISIBLE | WS_CHILD | WS_DISABLED, // Specify WS_DISABLED to pass input events to parent window
                                                   0,
                                                   0,
                                                   parentRect.right - parentRect.left,
                                                   parentRect.bottom - parentRect.top,
                                                   parentHwnd,
                                                   nullptr,
                                                   moduleHandle,
                                                   owner);

                if (childHwnd)
                {
                    PostMessage (parentHwnd, Direct2DLowLevelGraphicsContext::childWindowCreatedMessageID, 1, (LPARAM)childHwnd);
                }
#if JUCE_DEBUG
                else
                {
                    TCHAR messageBuffer[256] = {};

                    FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr,
                                   GetLastError(),
                                   MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   messageBuffer,
                                   (DWORD) numElementsInArray (messageBuffer) - 1,
                                   nullptr);

                    DBG (messageBuffer);
                    jassertfalse;
                }
#endif

                return childHwnd;
            }

            void runMessageLoop()
            {
                MSG message;
                DWORD waitResult;
                HANDLE objects[] = { wakeEvent.getHandle() };

                while (! threadShouldExit())
                {
                    waitResult = MsgWaitForMultipleObjects (numElementsInArray (objects),
                                                            objects, // Event handle
                                                            FALSE, // fWaitAll == FALSE
                                                            INFINITE, // No timeout
                                                            QS_ALLINPUT); // All messages

                    if (waitResult == WAIT_OBJECT_0)
                    {
                        if (threadShouldExit())
                        {
                            return;
                        }

                        continue;
                    }

                    while (PeekMessage (&message, nullptr, (UINT) 0, (UINT) 0, PM_REMOVE))
                    {
                        //Logger::outputDebugString("child window msg " + String::toHexString((int)message.message));
                        TRACE_LOG_CHILD_WINDOW_MESSAGE(message.message);

                        switch (message.message)
                        {
                        case WM_QUIT:
                            return;

                        case Direct2DLowLevelGraphicsContext::createChildWindowMessageID:
                            createChildWindowForExpectantParent((HWND)message.lParam);
                            break;
                        }

                        TranslateMessage (&message);
                        DispatchMessage (&message);
                    }
                }
            }

            ChildWindowThread* const owner;
            direct2d::ScopedEvent wakeEvent;
        } messageThread;
    };

} // namespace direct2d

} // namespace juce
