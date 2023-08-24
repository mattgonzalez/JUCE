
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

        void close(HWND childHwnd)
        {
        }

        void setSize (HWND childHwnd, Rectangle<int> size)
        {
            TRACE_LOG_D2D(etw::childWindowSetSize);

            SetWindowPos(childHwnd, nullptr, 0, 0,
                size.getWidth(), size.getHeight(), SWP_DEFERERASE | SWP_NOREDRAW);
        }

        void postMessage(uint32 messageID, WPARAM wParam, LPARAM lParam)
        {
            PostThreadMessage((DWORD)(pointer_sized_uint)messageThread.getThreadId(), messageID, wParam, lParam);
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

                case WM_CLOSE:
                    DestroyWindow (hwnd);
                    return 0;

                case WM_DESTROY:
                    PostMessage (hwnd, WM_QUIT, 0, 0);
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

                SetEvent(threadQuitEvent.getHandle());
                stopThread(1000);
            }

            void run() override
            {
                SetThreadDescription (GetCurrentThread(), getThreadName().toUTF16());

                runMessageLoop();

#if 0

                HMODULE moduleHandle = (HMODULE) Process::getCurrentModuleInstanceHandle();

                RECT parentRect{};
                GetClientRect(owner->parentHwnd, &parentRect);

                owner->childHwnd = CreateWindowEx (WS_EX_NOREDIRECTIONBITMAP,
                                                   owner->className.toWideCharPointer(),
                                                   nullptr,
                                                   WS_VISIBLE | WS_CHILD | WS_DISABLED, // Specify WS_DISABLED to pass input events to parent window
                                                   0,
                                                   0,
                                                   parentRect.right - parentRect.left,
                                                   parentRect.bottom - parentRect.top,
                                                   owner->parentHwnd,
                                                   nullptr,
                                                   moduleHandle,
                                                   owner);
                if (owner->childHwnd)
                {
                    PostMessage (GetParent (owner->childHwnd), Direct2DLowLevelGraphicsContext::customMessageID, 1, 0);

                    runMessageLoop();
                    owner->childHwnd = nullptr;
                }
#endif
            }

            void createChildWindow(HWND parentHwnd)
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
                    SendMessage (parentHwnd, Direct2DLowLevelGraphicsContext::customMessageID, 1, (LPARAM)childHwnd);
                }
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
            }

            void runMessageLoop()
            {
                MSG message;
                DWORD waitResult;
                HANDLE objects[] = { threadQuitEvent.getHandle() };

                while (! threadShouldExit())
                {
                    waitResult = MsgWaitForMultipleObjects (numElementsInArray (objects),
                                                            objects, // Event handle
                                                            FALSE, // fWaitAll == FALSE
                                                            INFINITE, // No timeout
                                                            QS_ALLINPUT); // All messages

                    if (waitResult == WAIT_OBJECT_0)
                    {
                        return;
                    }

                    while (PeekMessage (&message, nullptr, (UINT) 0, (UINT) 0, PM_REMOVE))
                    {
                        if (message.message == WM_QUIT)
                        {
                            return;
                        }

                        Logger::outputDebugString("child window msg " + String::toHexString((int)message.message));
                        TRACE_LOG_CHILD_WINDOW_MESSAGE(message.message);

                        switch (message.message)
                        {
                        case Direct2DLowLevelGraphicsContext::createWindowMessageID:
                        {
                            createChildWindow((HWND)message.lParam);
                            break;
                        }

                        }

                        TranslateMessage (&message);
                        DispatchMessage (&message);
                    }
                }
            }

            ChildWindowThread* const owner;
            direct2d::ScopedEvent threadQuitEvent;
        } messageThread;
    };

} // namespace direct2d

} // namespace juce
