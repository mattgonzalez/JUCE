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

class DXGIAdapters
{
public:
    void updateAdapters()
    {
        if (factory == nullptr)
            return;

        adapters.clear();

        UINT i = 0;
        ComSmartPtr<IDXGIAdapter> adapter;

        while (factory->EnumAdapters (i++, adapter.resetAndGetPointerAddress()) != DXGI_ERROR_NOT_FOUND)
            adapters.push_back (adapter);
    }

    std::vector<ComSmartPtr<IDXGIAdapter>> getAdapters() const
    {
        return adapters;
    }

    IDXGIFactory2* getFactory() const { return factory; }

    static DXGIAdapters& getInstance()
    {
        static DXGIAdapters adapters;
        return adapters;
    }

private:
    DXGIAdapters() = default;

    DynamicLibrary dxgiDll;
    ComSmartPtr<IDXGIFactory2> factory = [&]() -> ComSmartPtr<IDXGIFactory2>
    {
        if (! dxgiDll.open ("DXGI.dll"))
            return nullptr;

        JUCE_LOAD_WINAPI_FUNCTION (dxgiDll,
                                   CreateDXGIFactory,
                                   createDXGIFactory,
                                   HRESULT,
                                   (REFIID, void**))

        ComSmartPtr<IDXGIFactory2> result;
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wlanguage-extension-token")
        createDXGIFactory (__uuidof (IDXGIFactory2), (void**) result.resetAndGetPointerAddress());
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE

        return result;
    }();
    std::vector<ComSmartPtr<IDXGIAdapter>> adapters;
};

} // namespace juce
