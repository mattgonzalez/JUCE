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

namespace juce
{

    class HWNDComponent::Pimpl
    {
    public:
        Pimpl(HWND h, HWNDComponent& comp)
            : hwnd(h),
            hwndComponent(comp),
            hwndComponentWatcher(*this)
        {
            if (hwndComponent.isShowing())
                hwndComponentWatcher.componentPeerChanged();
        }

        ~Pimpl()
        {
            removeFromParent();
            DestroyWindow(hwnd);
        }

        Rectangle<int> getHWNDBounds() const
        {
            if (auto* peer = hwndComponent.getPeer())
            {
                ScopedThreadDPIAwarenessSetter threadDpiAwarenessSetter{ hwnd };

                RECT r;
                GetWindowRect(hwnd, &r);
                Rectangle<int> windowRectangle(r.right - r.left, r.bottom - r.top);

                return (windowRectangle.toFloat() / peer->getPlatformScaleFactor()).toNearestInt();
            }

            return {};
        }

        void updateHWNDBounds()
        {
            hwndComponentWatcher.componentMovedOrResized(true, true);
        }

        HWND hwnd;

    private:
        void addToParent()
        {
            if (ancestorPeer != nullptr)
            {
                auto windowFlags = GetWindowLongPtr(hwnd, -16);

                using FlagType = decltype (windowFlags);

                windowFlags &= ~(FlagType)WS_POPUP;

                if (ownedWindowFlag)
                {
                    windowFlags &= ~(FlagType)WS_CHILD;
                    SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, (LONG_PTR)ancestorPeer->getNativeHandle());

                    ancestorSubclasser = std::make_unique<AncestorSubclasser>(*this, (HWND)ancestorPeer->getNativeHandle());
                }
                else
                {
                    windowFlags |= (FlagType)WS_CHILD;
                    SetWindowLongPtr(hwnd, -16, windowFlags);
                    SetParent(hwnd, (HWND)ancestorPeer->getNativeHandle());
                }

                hwndComponentWatcher.componentMovedOrResized(true, true);
            }
        }

        void removeFromParent()
        {
            ancestorSubclasser = nullptr;

            ShowWindow(hwnd, SW_HIDE);
            SetParent(hwnd, nullptr);
        }

        ComponentPeer* findPeerForHWND() const noexcept
        {
            int numPeers = ComponentPeer::getNumPeers();
            for (int index = 0; index < numPeers; ++index)
            {
                if (auto peer = ComponentPeer::getPeer(index); peer->getNativeHandle() == (void*)hwnd)
                {
                    return peer;
                }
            }

            return nullptr;
        }

        HWNDComponent& hwndComponent;
        ComponentPeer* ancestorPeer = nullptr;

        //==============================================================================

        class AncestorSubclasser
        {
        public:
            AncestorSubclasser(Pimpl& pimpl_, HWND ancestorHwnd_) :
                pimpl(pimpl_),
                ancestorHwnd(ancestorHwnd_)
            {
                [[maybe_unused]] auto ok = SetWindowSubclass(ancestorHwnd_, subclassProc, windowSubclassID, (DWORD_PTR)this);
                jassert(ok);
            }

            ~AncestorSubclasser()
            {
                [[maybe_unused]] auto ok = RemoveWindowSubclass(ancestorHwnd, subclassProc, windowSubclassID);
                jassert(ok);
            }

        private:
            static LRESULT subclassProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
            {
                auto that = reinterpret_cast<AncestorSubclasser*>(static_cast<LONG_PTR>(dwRefData));

                switch (umsg)
                {
                case WM_WINDOWPOSCHANGED:
                    that->pimpl.hwndComponentWatcher.componentMovedOrResized(true, true);
                    break;
                }

                return DefSubclassProc(hwnd, umsg, wParam, lParam);
            }

            Pimpl& pimpl;
            HWND const ancestorHwnd;
            uint64 const windowSubclassID = Time::getHighResolutionTicks();
        };
        std::unique_ptr<AncestorSubclasser> ancestorSubclasser;

        //==============================================================================

        struct HWNDComponentWatcher : public ComponentMovementWatcher
        {
            HWNDComponentWatcher(Pimpl& pimpl_) :
                ComponentMovementWatcher(&pimpl_.hwndComponent),
                pimpl(pimpl_)
            {
            }
            ~HWNDComponentWatcher() override = default;

            void componentMovedOrResized(bool wasMoved, bool wasResized) override
            {
                if (auto* peer = pimpl.hwndComponent.getTopLevelComponent()->getPeer())
                {
                    auto area = peer->getAreaCoveredBy(pimpl.hwndComponent);

                    if (pimpl.ownedWindowFlag)
                    {
                        auto pos = area.getPosition();
                        pos = peer->localToGlobal(pos);
                        area.setPosition(pos);
                    }
                    area = (area.toFloat() * peer->getPlatformScaleFactor()).getSmallestIntegerContainer();

                    UINT flagsToSend = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER;

                    if (!wasMoved)   flagsToSend |= SWP_NOMOVE;
                    if (!wasResized) flagsToSend |= SWP_NOSIZE;

                    ScopedThreadDPIAwarenessSetter threadDpiAwarenessSetter{ pimpl.hwnd };

                    SetWindowPos(pimpl.hwnd, nullptr, area.getX(), area.getY(), area.getWidth(), area.getHeight(), flagsToSend);
                }
            }

            void componentPeerChanged() override
            {
                auto* newAncestorPeer = pimpl.hwndComponent.getPeer();

                if (pimpl.ancestorPeer != newAncestorPeer)
                {
                    pimpl.removeFromParent();
                    pimpl.ancestorPeer = newAncestorPeer;

                    if (auto hwndPeer = pimpl.findPeerForHWND())
                    {
                        pimpl.ownedWindowFlag = (hwndPeer->getStyleFlags() & ComponentPeer::windowIsOwned) != 0;
                    }

                    pimpl.addToParent();
                }

                auto isShowing = pimpl.hwndComponent.isShowing();
                ShowWindow(pimpl.hwnd, isShowing ? SW_SHOWNA : SW_HIDE);

                if (isShowing)
                    InvalidateRect(pimpl.hwnd, nullptr, 0);
            }

            void componentVisibilityChanged() override
            {
                componentPeerChanged();
            }

            Pimpl& pimpl;
        } hwndComponentWatcher;

        bool ownedWindowFlag = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pimpl)
    };

    //==============================================================================
    HWNDComponent::HWNDComponent() {}
    HWNDComponent::~HWNDComponent() {}

    void HWNDComponent::paint(Graphics&) {}

    void HWNDComponent::setHWND(void* hwnd)
    {
        if (hwnd != getHWND())
        {
            pimpl.reset();

            if (hwnd != nullptr)
                pimpl.reset(new Pimpl((HWND)hwnd, *this));
        }
    }

    void* HWNDComponent::getHWND() const
    {
        return pimpl == nullptr ? nullptr : (void*)pimpl->hwnd;
    }

    void HWNDComponent::resizeToFit()
    {
        if (pimpl != nullptr)
            setBounds(pimpl->getHWNDBounds());
    }

    void HWNDComponent::updateHWNDBounds()
    {
        if (pimpl != nullptr)
            pimpl->updateHWNDBounds();
    }

} // namespace juce
