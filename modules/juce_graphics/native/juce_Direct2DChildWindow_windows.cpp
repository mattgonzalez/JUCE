
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

    class ChildWindow
    {
    public:
        ChildWindow (String className_, HWND parentHwnd_) : className (className_),
                                                            parentHwnd (parentHwnd_),
                                                            messageThread (this)
        {
            messageThread.start();
        }

        ~ChildWindow()
        {
            messageThread.stop();
        }

        void close()
        {
            messageThread.stop();
        }

        void setSize()
        {
            RECT r;
            GetClientRect(GetParent(childHwnd), &r);
            setSize({ r.right - r.left, r.bottom - r.top });
        }

        void setSize (Rectangle<int> size)
        {
            SetWindowPos(childHwnd, nullptr, 0, 0,
                size.getWidth(), size.getHeight(), SWP_DEFERERASE | SWP_NOREDRAW);
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
        };

        HWND childHwnd = nullptr;

    private:
        String const className;
        HWND const parentHwnd;

        static LRESULT CALLBACK windowProc (
            HWND hwnd,
            UINT message,
            WPARAM wParam,
            LPARAM lParam)
        {
            //Logger::outputDebugString("child window proc " + String::toHexString((int)message));
            TRACE_LOG_CHILD_WINDOW_MESSAGE;

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

        struct MessageThread : protected Thread
        {
            MessageThread (ChildWindow* const owner_) : Thread ("Direct2DMessageThread"),
                                                        owner (owner_),
                                                        threadQuitEvent (CreateEvent (nullptr, FALSE, FALSE, nullptr))
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

                SendMessage (owner->childHwnd, WM_CLOSE, 0, 0);
                stopThread (100000);

                if (threadQuitEvent)
                {
                    CloseHandle (threadQuitEvent);
                    threadQuitEvent = nullptr;
                }
            }

            void run() override
            {
                SetThreadDescription (GetCurrentThread(), L"Direct2DMessageThread");

                HMODULE moduleHandle = (HMODULE) Process::getCurrentModuleInstanceHandle();

                owner->childHwnd = CreateWindowEx (WS_EX_NOREDIRECTIONBITMAP,
                                                   owner->className.toWideCharPointer(),
                                                   nullptr,
                                                   WS_VISIBLE | WS_CHILD | WS_DISABLED, // Specify WS_DISABLED to pass input events to parent window
                                                   0,
                                                   0,
                                                   0,
                                                   0,
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
            }

            void runMessageLoop()
            {
                MSG message;
                DWORD waitResult;
                HANDLE objects[] = { threadQuitEvent };

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

                    while (PeekMessage (&message, NULL, (UINT) 0, (UINT) 0, PM_REMOVE))
                    {
                        if (message.message == WM_QUIT)
                        {
                            return;
                        }

                        TranslateMessage (&message);
                        DispatchMessage (&message);
                    }
                }
            }

            ChildWindow* const owner;
            HANDLE threadQuitEvent = nullptr;
        } messageThread;
    };

} // namespace direct2d

} // namespace juce
