
namespace juce
{
    class DirectWriteCustomFontCollectionLoader : public ComBaseClassHelper<IDWriteFontCollectionLoader>
    {
    private:
        struct FontRawData
        {
            const void* data;
            size_t numBytes;
        };

        struct FontFileStream : public ComBaseClassHelper<IDWriteFontFileStream>
        {
            FontFileStream(FontRawData const& rawData_) :
                rawData(rawData_)
            {
            }
            ~FontFileStream() override = default;

            HRESULT GetFileSize(UINT64* fileSize) override
            {
                *fileSize = rawData.numBytes;
                return S_OK;
            }

            HRESULT GetLastWriteTime(UINT64* lastWriteTime) override
            {
                *lastWriteTime = 0;
                return S_OK;
            }

            HRESULT ReadFileFragment(void const** fragmentStart, UINT64 fileOffset, UINT64 fragmentSize, void** fragmentContext) override
            {
                if (fileOffset + fragmentSize >= rawData.numBytes)
                {
                    *fragmentStart = nullptr;
                    *fragmentContext = nullptr;
                    return E_INVALIDARG;
                }

                *fragmentStart = addBytesToPointer(rawData.data, fileOffset);
                *fragmentContext = this;
                return S_OK;
            }

            void ReleaseFileFragment(void* /*fragmentContext*/) override
            {
            }

            FontRawData rawData;
        };

        struct FontFileLoader : public ComBaseClassHelper<IDWriteFontFileLoader>
        {
            Array<FontRawData> rawDataArray;

            FontFileLoader() = default;
            ~FontFileLoader() override = default;

            HRESULT CreateStreamFromKey(void const* fontFileReferenceKey, UINT32 fontFileReferenceKeySize, IDWriteFontFileStream** fontFileStream) override
            {
                jassert(sizeof(char*) == fontFileReferenceKeySize);
                if (sizeof(char*) == fontFileReferenceKeySize)
                {
                    const char* referenceKey = *(const char**)fontFileReferenceKey;
                    for (auto const& rawData : rawDataArray)
                    {
                        if (referenceKey == rawData.data)
                        {
                            *fontFileStream = new FontFileStream{ rawData };
                            return S_OK;
                        }
                    }
                }

                *fontFileStream = nullptr;
                return E_INVALIDARG;
            }
        } fontFileLoader;

        struct FontFileEnumerator : public ComBaseClassHelper<IDWriteFontFileEnumerator>
        {
            FontFileEnumerator(IDWriteFactory* factory_, FontFileLoader& fontFileLoader_) :
                factory(factory_),
                fontFileLoader(fontFileLoader_)
            {
            }

            ~FontFileEnumerator() override = default;

            HRESULT GetCurrentFontFile(IDWriteFontFile** fontFile) override
            {
                if (rawDataIndex < 0 || rawDataIndex >= fontFileLoader.rawDataArray.size())
                {
                    *fontFile = nullptr;
                    return E_FAIL;
                }

                auto referenceKey = fontFileLoader.rawDataArray[rawDataIndex].data;
                return factory->CreateCustomFontFileReference(&referenceKey,
                    sizeof(referenceKey),
                    &fontFileLoader,
                    fontFile);
            }

            HRESULT MoveNext(BOOL* hasCurrentFile) override
            {
                ++rawDataIndex;
                *hasCurrentFile = rawDataIndex < fontFileLoader.rawDataArray.size() ? TRUE : FALSE;
                return S_OK;
            }

            IDWriteFactory* factory;
            FontFileLoader& fontFileLoader;
            int rawDataIndex = -1;
        };

    public:
        DirectWriteCustomFontCollectionLoader(const void* data, size_t dataSize)
        {
            fontFileLoader.rawDataArray.add({ data, dataSize });
        }
        ~DirectWriteCustomFontCollectionLoader() override = default;

        HRESULT CreateEnumeratorFromKey(
            IDWriteFactory* factory,
            void const* collectionKey,
            UINT32 collectionKeySize,
            IDWriteFontFileEnumerator** fontFileEnumerator
        ) override
        {
            jassertquiet(collectionKeySize == sizeof(key));
            jassertquiet(0 == std::memcmp(collectionKey, &key, collectionKeySize));

            *fontFileEnumerator = new FontFileEnumerator{ factory, fontFileLoader };
            return S_OK;
        }

        IDWriteFontFileLoader* getFontFileLoader() const
        {
            return (IDWriteFontFileLoader*)&fontFileLoader;
        }

        bool hasRawData(const void* data, size_t /*dataSize*/)
        {
            return fontFileLoader.rawDataArray.getFirst().data == data;
        }

        ComSmartPtr<IDWriteFontCollection> customFontCollection;
        int64 const key = Time::getHighResolutionTicks();
    };
}
