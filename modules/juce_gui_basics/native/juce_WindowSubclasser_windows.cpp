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

#if JUCE_WINDOWS

namespace juce::detail
{
    class HWNDAncestorSubclasser::Pimpl
    {
    public:
        Pimpl(HWND ancestorHwnd_, std::function<void()> onWindowPosChanged_) :
            ancestorHwnd(ancestorHwnd_),
            onWindowPosChanged(onWindowPosChanged_)
        {
            [[maybe_unused]] auto ok = SetWindowSubclass(ancestorHwnd_, subclassProc, windowSubclassID, (DWORD_PTR)this);
            jassert(ok);
        }

        ~Pimpl()
        {
            [[maybe_unused]] auto ok = RemoveWindowSubclass(ancestorHwnd, subclassProc, windowSubclassID);
            jassert(ok);
        }

    private:
        static LRESULT subclassProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
        {
            auto that = reinterpret_cast<HWNDAncestorSubclasser::Pimpl*>(static_cast<LONG_PTR>(dwRefData));

            switch (umsg)
            {
            case WM_WINDOWPOSCHANGED:
                if (that->onWindowPosChanged)
                {
                    that->onWindowPosChanged();
                }
                break;
            }

            return DefSubclassProc(hwnd, umsg, wParam, lParam);
        }

        HWND ancestorHwnd;
        std::function<void()> onWindowPosChanged;
        uint64 const windowSubclassID = Time::getHighResolutionTicks();
    };

    HWNDAncestorSubclasser::HWNDAncestorSubclasser(void* ancestorHwnd, std::function<void()> onWindowPosChanged_) :
        pimpl(new Pimpl{ (HWND)ancestorHwnd, onWindowPosChanged_ })
    {
    }

    HWNDAncestorSubclasser::~HWNDAncestorSubclasser() {}

    void* HWNDAncestorSubclasser::findAncestorHWND(void* hwnd) noexcept
    {
        if (auto ancestor = GetAncestor((HWND)hwnd, GA_ROOTOWNER))
        {
            return ancestor;
        }

        if (auto ancestor = GetAncestor((HWND)hwnd, GA_ROOT))
        {
            return ancestor;
        }

        return hwnd;
    }

} // namespace juce::detail

#endif
