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
    Pimpl (HWND h, HWNDComponent& comp)
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
        DestroyWindow (hwnd);
    }

    Rectangle<int> getHWNDBounds() const
    {
        if (auto* peer = hwndComponent.getPeer())
        {
            ScopedThreadDPIAwarenessSetter threadDpiAwarenessSetter { hwnd };

            RECT r;
            GetWindowRect (hwnd, &r);
            Rectangle<int> windowRectangle (r.right - r.left, r.bottom - r.top);

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
        if (hwndComponentPeer != nullptr)
        {
            auto windowFlags = GetWindowLongPtr (hwnd, -16);

            using FlagType = decltype (windowFlags);

            windowFlags &= ~(FlagType) WS_POPUP;

            if (ancestorWatcher)
            {
                windowFlags &= ~(FlagType)WS_CHILD;
                SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, (LONG_PTR)hwndComponentPeer->getNativeHandle());
            }
            else 
            {
                windowFlags |= (FlagType)WS_CHILD;
                SetWindowLongPtr(hwnd, -16, windowFlags);
                SetParent(hwnd, (HWND)hwndComponentPeer->getNativeHandle());
            }

            hwndComponentWatcher.componentMovedOrResized (true, true);
        }
    }

    void removeFromParent()
    {
        ShowWindow (hwnd, SW_HIDE);
        SetParent (hwnd, nullptr);
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
    ComponentPeer* hwndComponentPeer = nullptr;

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
                if (pimpl.ancestorWatcher)
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
            auto* newHwndComponentPeer = pimpl.hwndComponent.getPeer();

            if (pimpl.hwndComponentPeer != newHwndComponentPeer)
            {
                pimpl.removeFromParent();
                pimpl.hwndComponentPeer = newHwndComponentPeer;

                if (auto hwndPeer = pimpl.findPeerForHWND())
                {
                    if (hwndPeer->getStyleFlags() & ComponentPeer::windowIsOwned)
                    {
                        pimpl.ancestorWatcher = std::make_unique<AncestorWatcher>(pimpl, newHwndComponentPeer->getComponent());
                    }
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

    struct AncestorWatcher : public ComponentMovementWatcher
    {
        AncestorWatcher(Pimpl& pimpl_, Component& ancestor_) : 
            ComponentMovementWatcher(&ancestor_),
            pimpl(pimpl_)
        {
        }
        ~AncestorWatcher() override = default;

        void componentMovedOrResized(bool wasMoved, bool wasResized) override
        {
            pimpl.hwndComponentWatcher.componentMovedOrResized(wasMoved, wasResized);
        }

        void componentPeerChanged() override {}
        void componentVisibilityChanged() override {}

        Pimpl& pimpl;
    };
    std::unique_ptr<AncestorWatcher> ancestorWatcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pimpl)
};

//==============================================================================
HWNDComponent::HWNDComponent()  {}
HWNDComponent::~HWNDComponent() {}

void HWNDComponent::paint (Graphics&) {}

void HWNDComponent::setHWND (void* hwnd)
{
    if (hwnd != getHWND())
    {
        pimpl.reset();

        if (hwnd != nullptr)
            pimpl.reset (new Pimpl ((HWND) hwnd, *this));
    }
}

void* HWNDComponent::getHWND() const
{
    return pimpl == nullptr ? nullptr : (void*) pimpl->hwnd;
}

void HWNDComponent::resizeToFit()
{
    if (pimpl != nullptr)
        setBounds (pimpl->getHWNDBounds());
}

void HWNDComponent::updateHWNDBounds()
{
    if (pimpl != nullptr)
        pimpl->updateHWNDBounds();
}

} // namespace juce
