/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

struct BitmapDataDetail
{
    BitmapDataDetail() = delete;

    static void convert (const Image::BitmapData& src, Image::BitmapData& dest)
    {
        jassert (src.width == dest.width);
        jassert (src.height == dest.height);

        if (src.pixelStride == dest.pixelStride && src.pixelFormat == dest.pixelFormat)
        {
            for (int y = 0; y < dest.height; ++y)
                memcpy (dest.getLinePointer (y), src.getLinePointer (y), (size_t) dest.pixelStride * (size_t) dest.width);
        }
        else
        {
            for (int y = 0; y < dest.height; ++y)
                for (int x = 0; x < dest.width; ++x)
                    dest.setPixelColour (x, y, src.getPixelColour (x, y));
        }
    }

    static Image convert (const Image::BitmapData& src, const ImageType& type)
    {
        Image result (type.create (src.pixelFormat, src.width, src.height, false));

        {
            Image::BitmapData dest (result, Image::BitmapData::writeOnly);
            BitmapDataDetail::convert (src, dest);
        }

        return result;
    }
};

class SubsectionPixelData : public ImagePixelData
{
public:
    SubsectionPixelData (ImagePixelData::Ptr source, Rectangle<int> r)
        : ImagePixelData (source->pixelFormat, r.getWidth(), r.getHeight()),
          sourceImage (std::move (source)),
          area (r)
    {
    }

    Rectangle<int>      getSubsection()      const { return area; }
    ImagePixelData::Ptr getSourcePixelData() const { return sourceImage; }

    std::unique_ptr<LowLevelGraphicsContext> createLowLevelContext() override
    {
        auto g = sourceImage->createLowLevelContext();
        g->clipToRectangle (area);
        g->setOrigin (area.getPosition());
        return g;
    }

    void initialiseBitmapData (Image::BitmapData& bitmap, int x, int y, Image::BitmapData::ReadWriteMode mode) override
    {
        sourceImage->initialiseBitmapData (bitmap, x + area.getX(), y + area.getY(), mode);

        if (mode != Image::BitmapData::readOnly)
            sendDataChangeMessage();
    }

    ImagePixelData::Ptr clone() override
    {
        jassert (getReferenceCount() > 0); // (This method can't be used on an unowned pointer, as it will end up self-deleting)
        auto type = createType();

        Image newImage (type->create (pixelFormat, area.getWidth(), area.getHeight(), pixelFormat != Image::RGB));

        {
            Graphics g (newImage);
            g.drawImageAt (Image (*this), 0, 0);
        }

        return *newImage.getPixelData();
    }

    std::unique_ptr<ImageType> createType() const override { return sourceImage->createType(); }

    /* as we always hold a reference to image, don't double count */
    int getSharedCount() const noexcept override { return getReferenceCount() + sourceImage->getSharedCount() - 1; }

private:
    friend class Image;
    const ImagePixelData::Ptr sourceImage;
    const Rectangle<int> area;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubsectionPixelData)
};

//==============================================================================
ImagePixelData::ImagePixelData (Image::PixelFormat format, int w, int h, Image::Permanence permanenceIn)
    : pixelFormat (format), width (w), height (h), permanence(permanenceIn)
{
    jassert (format == Image::RGB || format == Image::ARGB || format == Image::SingleChannel);
    jassert (w > 0 && h > 0); // It's illegal to create a zero-sized image!
}

ImagePixelData::~ImagePixelData()
{
    listeners.call ([this] (Listener& l) { l.imageDataBeingDeleted (this); });
}

void ImagePixelData::sendDataChangeMessage()
{
    listeners.call ([this] (Listener& l) { l.imageDataChanged (this); });
}

int ImagePixelData::getSharedCount() const noexcept
{
    return getReferenceCount();
}

void ImagePixelData::moveImageSection(int dx, int dy,
    int sx, int sy,
    int w, int h)
{
    if (dx < 0)
    {
        w += dx;
        sx -= dx;
        dx = 0;
    }

    if (dy < 0)
    {
        h += dy;
        sy -= dy;
        dy = 0;
    }

    if (sx < 0)
    {
        w += sx;
        dx -= sx;
        sx = 0;
    }

    if (sy < 0)
    {
        h += sy;
        dy -= sy;
        sy = 0;
    }

    const int minX = jmin(dx, sx);
    const int minY = jmin(dy, sy);

    w = jmin(w, width - jmax(sx, dx));
    h = jmin(h, height - jmax(sy, dy));

    if (w > 0 && h > 0)
    {
        auto maxX = jmax(dx, sx) + w;
        auto maxY = jmax(dy, sy) + h;

        Image image{ this };
        const Image::BitmapData destData(image, minX, minY, maxX - minX, maxY - minY, Image::BitmapData::readWrite);

        auto dst = destData.getPixelPointer(dx - minX, dy - minY);
        auto src = destData.getPixelPointer(sx - minX, sy - minY);

        auto lineSize = (size_t)destData.pixelStride * (size_t)w;

        if (dy > sy)
        {
            while (--h >= 0)
            {
                const int offset = h * destData.lineStride;
                memmove(dst + offset, src + offset, lineSize);
            }
        }
        else if (dst != src)
        {
            while (--h >= 0)
            {
                memmove(dst, src, lineSize);
                dst += destData.lineStride;
                src += destData.lineStride;
            }
        }
    }
}

template <class PixelType>
struct PixelIterator
{
    template <class PixelOperation>
    static void iterate(const Image::BitmapData& data, const PixelOperation& pixelOp)
    {
        for (int y = 0; y < data.height; ++y)
        {
            auto p = data.getLinePointer(y);

            for (int x = 0; x < data.width; ++x)
            {
                pixelOp(*reinterpret_cast<PixelType*> (p));
                p += data.pixelStride;
            }
        }
    }
};

template <class PixelOperation>
static void performPixelOp(const Image::BitmapData& data, const PixelOperation& pixelOp)
{
    switch (data.pixelFormat)
    {
    case Image::ARGB:           PixelIterator<PixelARGB> ::iterate(data, pixelOp); break;
    case Image::RGB:            PixelIterator<PixelRGB>  ::iterate(data, pixelOp); break;
    case Image::SingleChannel:  PixelIterator<PixelAlpha>::iterate(data, pixelOp); break;
    case Image::UnknownFormat:
    default:                    jassertfalse; break;
    }
}

struct DesaturateOp
{
    template <class PixelType>
    void operator() (PixelType& pixel) const
    {
        pixel.desaturate();
    }
};

void ImagePixelData::desaturate()
{
    if (pixelFormat == Image::RGB || pixelFormat == Image::ARGB)
    {
        Image image{ this };
        const Image::BitmapData destData(image, 0, 0, width, height, Image::BitmapData::readWrite);
        performPixelOp(destData, DesaturateOp());
    }
}

void ImagePixelData::applyGaussianBlurEffect ([[maybe_unused]] float radius, Image& result)
{
    result = {};
}

void ImagePixelData::applySingleChannelBoxBlurEffect ([[maybe_unused]] int radius, juce::Image &result)
{
    result = {};
}

//==============================================================================
ImageType::ImageType() = default;
ImageType::~ImageType() = default;

Image ImageType::convert (const Image& source) const
{
    if (source.isNull() || getTypeID() == source.getPixelData()->createType()->getTypeID())
        return source;

    const Image::BitmapData src (source, Image::BitmapData::readOnly);

    if (src.data == nullptr)
        return {};

    return BitmapDataDetail::convert (src, *this);
}

//==============================================================================
class SoftwarePixelData : public ImagePixelData
{
public:
    SoftwarePixelData (Image::PixelFormat formatToUse, int w, int h, bool clearImage)
        : ImagePixelData (formatToUse, w, h),
          pixelStride (formatToUse == Image::RGB ? 3 : ((formatToUse == Image::ARGB) ? 4 : 1)),
          lineStride ((pixelStride * jmax (1, w) + 3) & ~3)
    {
        imageData.allocate ((size_t) lineStride * (size_t) jmax (1, h), clearImage);
    }

    std::unique_ptr<LowLevelGraphicsContext> createLowLevelContext() override
    {
        sendDataChangeMessage();
        return std::make_unique<LowLevelGraphicsSoftwareRenderer> (Image (*this));
    }

    void initialiseBitmapData (Image::BitmapData& bitmap, int x, int y, Image::BitmapData::ReadWriteMode mode) override
    {
        const auto offset = (size_t) x * (size_t) pixelStride + (size_t) y * (size_t) lineStride;
        bitmap.data = imageData + offset;
        bitmap.size = (size_t) (height * lineStride) - offset;
        bitmap.pixelFormat = pixelFormat;
        bitmap.lineStride = lineStride;
        bitmap.pixelStride = pixelStride;

        if (mode != Image::BitmapData::readOnly)
            sendDataChangeMessage();
    }

    ImagePixelData::Ptr clone() override
    {
        auto s = new SoftwarePixelData (pixelFormat, width, height, false);
        memcpy (s->imageData, imageData, (size_t) lineStride * (size_t) height);
        return *s;
    }

    std::unique_ptr<ImageType> createType() const override    { return std::make_unique<SoftwareImageType>(); }

private:
    HeapBlock<uint8> imageData;
    const int pixelStride, lineStride;

    JUCE_LEAK_DETECTOR (SoftwarePixelData)
};

SoftwareImageType::SoftwareImageType() = default;
SoftwareImageType::~SoftwareImageType() = default;

ImagePixelData::Ptr SoftwareImageType::create (Image::PixelFormat format, int width, int height, bool clearImage, Image::Permanence) const
{
    // The Permanence parameter is ignored here, as software images are always permanent
    return *new SoftwarePixelData (format, width, height, clearImage);
}

int SoftwareImageType::getTypeID() const
{
    return 2;
}

//==============================================================================
NativeImageType::NativeImageType() = default;
NativeImageType::~NativeImageType() = default;

int NativeImageType::getTypeID() const
{
    return 1;
}

#if JUCE_LINUX || JUCE_BSD
ImagePixelData::Ptr NativeImageType::create (Image::PixelFormat format, int width, int height, bool clearImage) const
{
    return new SoftwarePixelData (format, width, height, clearImage);
}
#endif

//==============================================================================

Image Image::getClippedImage (const Rectangle<int>& area) const
{
    if (area.contains (getBounds()))
        return *this;

    auto validArea = area.getIntersection (getBounds());

    if (validArea.isEmpty())
        return {};

    return Image { ImagePixelData::Ptr { new SubsectionPixelData { image, validArea } } };
}

//==============================================================================
Image::Image() noexcept = default;

Image::Image (ReferenceCountedObjectPtr<ImagePixelData> instance) noexcept
    : image (std::move (instance))
{
}

Image::Image (PixelFormat format, int width, int height, bool clearImage, Permanence requestedPermanence)
    : image (NativeImageType().create (format, width, height, clearImage, requestedPermanence))
{
}

Image::Image (PixelFormat format, int width, int height, bool clearImage, const ImageType& type, Permanence requestedPermanence)
    : image (type.create (format, width, height, clearImage, requestedPermanence))
{
}

Image::Image (const Image& other) noexcept
    : image (other.image)
{
}

Image& Image::operator= (const Image& other)
{
    image = other.image;
    return *this;
}

Image::Image (Image&& other) noexcept
    : image (std::move (other.image))
{
}

Image& Image::operator= (Image&& other) noexcept
{
    image = std::move (other.image);
    return *this;
}

Image::~Image() = default;

int Image::getReferenceCount() const noexcept           { return image == nullptr ? 0 : image->getSharedCount(); }

bool Image::isValid() const noexcept
{
    return image != nullptr;
}

int Image::getWidth() const noexcept                    { return image == nullptr ? 0 : image->width; }
int Image::getHeight() const noexcept                   { return image == nullptr ? 0 : image->height; }
Rectangle<int> Image::getBounds() const noexcept        { return image == nullptr ? Rectangle<int>() : Rectangle<int> (image->width, image->height); }
Image::PixelFormat Image::getFormat() const noexcept    { return image == nullptr ? UnknownFormat : image->pixelFormat; }
bool Image::isARGB() const noexcept                     { return getFormat() == ARGB; }
bool Image::isRGB() const noexcept                      { return getFormat() == RGB; }
bool Image::isSingleChannel() const noexcept            { return getFormat() == SingleChannel; }
bool Image::hasAlphaChannel() const noexcept            { return getFormat() != RGB; }
bool Image::isPermanent() const noexcept                { return image != nullptr && image->permanence == Permanence::permanent; }
bool Image::isDisposable() const noexcept               { return ! isPermanent(); }

std::unique_ptr<LowLevelGraphicsContext> Image::createLowLevelContext() const
{
    if (image != nullptr)
        return image->createLowLevelContext();

    return {};
}

void Image::duplicateIfShared()
{
    if (getReferenceCount() > 1)
        image = image->clone();
}

Image Image::createCopy() const
{
    if (image != nullptr)
        return Image (image->clone());

    return {};
}

Image Image::rescaled (int newWidth, int newHeight, Graphics::ResamplingQuality quality) const
{
    if (image == nullptr || (image->width == newWidth && image->height == newHeight))
        return *this;

    auto type = image->createType();
    Image newImage (type->create (image->pixelFormat, newWidth, newHeight, hasAlphaChannel(), image->permanence));

    Graphics g (newImage);
    g.setImageResamplingQuality (quality);
    g.drawImageTransformed (*this, AffineTransform::scale ((float) newWidth  / (float) image->width,
                                                           (float) newHeight / (float) image->height), false);
    return newImage;
}

Image Image::convertedToFormat (PixelFormat newFormat) const
{
    if (image == nullptr || newFormat == image->pixelFormat)
        return *this;

    auto w = image->width, h = image->height;

    auto type = image->createType();
    Image newImage (type->create (newFormat, w, h, false));

    if (newFormat == SingleChannel)
    {
        if (! hasAlphaChannel())
        {
            newImage.clear (getBounds(), Colours::black);
        }
        else
        {
            const BitmapData destData (newImage, 0, 0, w, h, BitmapData::writeOnly);
            const BitmapData srcData (*this, 0, 0, w, h);

            for (int y = 0; y < h; ++y)
            {
                auto src = reinterpret_cast<const PixelARGB*> (srcData.getLinePointer (y));
                auto dst = destData.getLinePointer (y);

                for (int x = 0; x < w; ++x)
                    dst[x] = src[x].getAlpha();
            }
        }
    }
    else if (image->pixelFormat == SingleChannel && newFormat == Image::ARGB)
    {
        const BitmapData destData (newImage, 0, 0, w, h, BitmapData::writeOnly);
        const BitmapData srcData (*this, 0, 0, w, h);

        for (int y = 0; y < h; ++y)
        {
            auto src = reinterpret_cast<const PixelAlpha*> (srcData.getLinePointer (y));
            auto dst = reinterpret_cast<PixelARGB*> (destData.getLinePointer (y));

            for (int x = 0; x < w; ++x)
                dst[x].set (src[x]);
        }
    }
    else
    {
        if (hasAlphaChannel())
            newImage.clear (getBounds());

        Graphics g (newImage);
        g.drawImageAt (*this, 0, 0);
    }

    return newImage;
}

NamedValueSet* Image::getProperties() const
{
    return image == nullptr ? nullptr : &(image->userData);
}

//==============================================================================
Image::BitmapData::BitmapData (Image& im, int x, int y, int w, int h, BitmapData::ReadWriteMode mode)
    : width (w), height (h)
{
    // The BitmapData class must be given a valid image, and a valid rectangle within it!
    jassert (im.image != nullptr);
    jassert (x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= im.getWidth() && y + h <= im.getHeight());

    im.image->initialiseBitmapData (*this, x, y, mode);
    jassert (data != nullptr && pixelStride > 0 && lineStride != 0);
}

Image::BitmapData::BitmapData (const Image& im, int x, int y, int w, int h)
    : width (w), height (h)
{
    // The BitmapData class must be given a valid image, and a valid rectangle within it!
    jassert (im.image != nullptr);
    jassert (x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= im.getWidth() && y + h <= im.getHeight());

    im.image->initialiseBitmapData (*this, x, y, readOnly);
    jassert (data != nullptr && pixelStride > 0 && lineStride != 0);
}

Image::BitmapData::BitmapData (const Image& im, BitmapData::ReadWriteMode mode)
    : width (im.getWidth()),
      height (im.getHeight())
{
    // The BitmapData class must be given a valid image!
    jassert (im.image != nullptr);

    im.image->initialiseBitmapData (*this, 0, 0, mode);
    jassert (data != nullptr && pixelStride > 0 && lineStride != 0);
}

Image::BitmapData::~BitmapData()
{
}

Colour Image::BitmapData::getPixelColour (int x, int y) const noexcept
{
    jassert (isPositiveAndBelow (x, width) && isPositiveAndBelow (y, height));

    auto pixel = getPixelPointer (x, y);

    switch (pixelFormat)
    {
        case Image::ARGB:           return Colour ( ((const PixelARGB*)  pixel)->getUnpremultiplied());
        case Image::RGB:            return Colour (*((const PixelRGB*)   pixel));
        case Image::SingleChannel:  return Colour (*((const PixelAlpha*) pixel));
        case Image::UnknownFormat:
        default:                    jassertfalse; break;
    }

    return {};
}

void Image::BitmapData::setPixelColour (int x, int y, Colour colour) const noexcept
{
    jassert (isPositiveAndBelow (x, width) && isPositiveAndBelow (y, height));

    auto pixel = getPixelPointer (x, y);
    auto col = colour.getPixelARGB();

    switch (pixelFormat)
    {
        case Image::ARGB:           ((PixelARGB*)  pixel)->set (col); break;
        case Image::RGB:            ((PixelRGB*)   pixel)->set (col); break;
        case Image::SingleChannel:  ((PixelAlpha*) pixel)->set (col); break;
        case Image::UnknownFormat:
        default:                    jassertfalse; break;
    }
}

//==============================================================================
void Image::clear (const Rectangle<int>& area, Colour colourToClearTo)
{
    if (image == nullptr)
        return;

    auto g = image->createLowLevelContext();
    g->setFill (colourToClearTo);
    g->fillRect (area, true);
}

//==============================================================================
Colour Image::getPixelAt (int x, int y) const
{
    if (isPositiveAndBelow (x, getWidth()) && isPositiveAndBelow (y, getHeight()))
    {
        const BitmapData srcData (*this, x, y, 1, 1);
        return srcData.getPixelColour (0, 0);
    }

    return {};
}

void Image::setPixelAt (int x, int y, Colour colour)
{
    if (isPositiveAndBelow (x, getWidth()) && isPositiveAndBelow (y, getHeight()))
    {
        const BitmapData destData (*this, x, y, 1, 1, BitmapData::writeOnly);
        destData.setPixelColour (0, 0, colour);
    }
}

void Image::multiplyAlphaAt (int x, int y, float multiplier)
{
    if (isPositiveAndBelow (x, getWidth()) && isPositiveAndBelow (y, getHeight())
         && hasAlphaChannel())
    {
        const BitmapData destData (*this, x, y, 1, 1, BitmapData::readWrite);

        if (isARGB())
            reinterpret_cast<PixelARGB*> (destData.data)->multiplyAlpha (multiplier);
        else
            *(destData.data) = (uint8) (*(destData.data) * multiplier);
    }
}

struct AlphaMultiplyOp
{
    float alpha;

    template <class PixelType>
    void operator() (PixelType& pixel) const
    {
        pixel.multiplyAlpha (alpha);
    }
};

void Image::multiplyAllAlphas (float amountToMultiplyBy)
{
    jassert (hasAlphaChannel());

    const BitmapData destData (*this, 0, 0, getWidth(), getHeight(), BitmapData::readWrite);
    performPixelOp (destData, AlphaMultiplyOp { amountToMultiplyBy });
}

void Image::desaturate()
{
    if (image)
        image->desaturate();
}

void Image::createSolidAreaMask (RectangleList<int>& result, float alphaThreshold) const
{
    if (hasAlphaChannel())
    {
        auto threshold = (uint8) jlimit (0, 255, roundToInt (alphaThreshold * 255.0f));
        SparseSet<int> pixelsOnRow;

        const BitmapData srcData (*this, 0, 0, getWidth(), getHeight());

        for (int y = 0; y < srcData.height; ++y)
        {
            pixelsOnRow.clear();
            auto lineData = srcData.getLinePointer (y);

            if (isARGB())
            {
                for (int x = 0; x < srcData.width; ++x)
                {
                    if (reinterpret_cast<const PixelARGB*> (lineData)->getAlpha() >= threshold)
                        pixelsOnRow.addRange (Range<int> (x, x + 1));

                    lineData += srcData.pixelStride;
                }
            }
            else
            {
                for (int x = 0; x < srcData.width; ++x)
                {
                    if (*lineData >= threshold)
                        pixelsOnRow.addRange (Range<int> (x, x + 1));

                    lineData += srcData.pixelStride;
                }
            }

            for (int i = 0; i < pixelsOnRow.getNumRanges(); ++i)
            {
                auto range = pixelsOnRow.getRange (i);
                result.add (Rectangle<int> (range.getStart(), y, range.getLength(), 1));
            }

            result.consolidate();
        }
    }
    else
    {
        result.add (0, 0, getWidth(), getHeight());
    }
}

void Image::moveImageSection (int dx, int dy,
                              int sx, int sy,
                              int w, int h)
{
    if (image)
        image->moveImageSection (dx, dy, sx, sy, w, h);
}

void ImageEffects::applyGaussianBlurEffect (float radius, const Image& input, Image& result)
{
    auto* image = input.getPixelData();

    if (image == nullptr)
    {
        result = {};
        return;
    }

    auto copy = result;
    image->applyGaussianBlurEffect (radius, copy);

    if (copy.isValid())
    {
        result = std::move (copy);
        return;
    }

    const auto tie = [] (const auto& x) { return std::tuple (x.getFormat(), x.getWidth(), x.getHeight()); };

    if (tie (input) != tie (result))
        result = Image { input.getFormat(), input.getWidth(), input.getHeight(), false };

    ImageConvolutionKernel blurKernel (roundToInt (radius * 2.0f));

    blurKernel.createGaussianBlur (radius);

    blurKernel.applyToImage (result, input, result.getBounds());
}

static void blurDataTriplets (uint8* d, int num, const int delta) noexcept
{
    uint32 last = d[0];
    d[0] = (uint8) ((d[0] + d[delta] + 1) / 3);
    d += delta;

    num -= 2;

    do
    {
        const uint32 newLast = d[0];
        d[0] = (uint8) ((last + d[0] + d[delta] + 1) / 3);
        d += delta;
        last = newLast;
    }
    while (--num > 0);

    d[0] = (uint8) ((last + d[0] + 1) / 3);
}

static void blurSingleChannelImage (uint8* const data, const int width, const int height,
                                    const int lineStride, const int repetitions) noexcept
{
    jassert (width > 2 && height > 2);

    for (int y = 0; y < height; ++y)
        for (int i = repetitions; --i >= 0;)
            blurDataTriplets (data + lineStride * y, width, 1);

    for (int x = 0; x < width; ++x)
        for (int i = repetitions; --i >= 0;)
            blurDataTriplets (data + x, height, lineStride);
}

static void blurSingleChannelImage (Image& image, int radius)
{
    const Image::BitmapData bm (image, Image::BitmapData::readWrite);
    blurSingleChannelImage (bm.data, bm.width, bm.height, bm.lineStride, 2 * radius);
}

void ImageEffects::applySingleChannelBoxBlurEffect (int radius, const Image& input, Image& result)
{
    auto* image = input.getPixelData();

    if (image == nullptr)
    {
        result = {};
        return;
    }

    auto copy = result;
    image->applySingleChannelBoxBlurEffect (radius, copy);

    if (copy.isValid())
    {
        result = std::move (copy);
        return;
    }

    const auto inputConfig = std::tuple (Image::SingleChannel, input.getWidth(), input.getHeight());
    const auto outputConfig = std::tuple (result.getFormat(), result.getWidth(), result.getHeight());

    if (inputConfig != outputConfig)
        result = Image { Image::SingleChannel, input.getWidth(), input.getHeight(), false };

    {
        Image::BitmapData source { input, Image::BitmapData::readOnly };
        Image::BitmapData dest { result, Image::BitmapData::writeOnly };
        BitmapDataDetail::convert (source, dest);
    }

    blurSingleChannelImage (result, radius);
}

//==============================================================================
#if JUCE_ALLOW_STATIC_NULL_VARIABLES

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")
JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4996)

const Image Image::null;

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
JUCE_END_IGNORE_WARNINGS_MSVC

#endif

} // namespace juce
