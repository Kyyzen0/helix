// Decodes the pictures of materials with ImageIO, the framework of macOS that owns the formats.
#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include "Engine/Import/ImageDecode.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace helix::import {

namespace {

DecodeResult failed(std::string message)
{
    DecodeResult result;
    result.error = std::move(message);
    return result;
}

// Draws an image into an sRGB RGBA bitmap, at its size or scaled down so its longest side fits.
DecodeResult render(CGImageRef cgImage, const std::string& source)
{
    const std::size_t sourceWidth = CGImageGetWidth(cgImage);
    const std::size_t sourceHeight = CGImageGetHeight(cgImage);
    if (sourceWidth == 0 || sourceHeight == 0) return failed("the picture is empty");

    std::size_t width = sourceWidth;
    std::size_t height = sourceHeight;
    const std::size_t longest = std::max(width, height);
    if (longest > kMaxTextureSide)
    {
        const double scale = double(kMaxTextureSide) / double(longest);
        width = std::max<std::size_t>(1, std::size_t(std::llround(double(width) * scale)));
        height = std::max<std::size_t>(1, std::size_t(std::llround(double(height) * scale)));
    }

    auto image = std::make_shared<geo::Image>();
    image->width = static_cast<std::uint32_t>(width);
    image->height = static_cast<std::uint32_t>(height);
    image->source = source;
    if (width != sourceWidth || height != sourceHeight)
        image->source += " (scaled down from " + std::to_string(sourceWidth) + "x" + std::to_string(sourceHeight) + ")";
    try
    {
        image->rgba.assign(width * height * 4, 0);
    }
    catch (const std::bad_alloc&)
    {
        return failed("there is not enough memory for the picture");
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(
        image->rgba.data(), width, height, 8, width * 4, colorSpace, kCGImageAlphaPremultipliedLast
    );
    CGColorSpaceRelease(colorSpace);
    if (!context) return failed("the picture cannot be converted");
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    // The memory of a bitmap context starts at the top row of what is drawn.
    CGContextDrawImage(context, CGRectMake(0, 0, CGFloat(width), CGFloat(height)), cgImage);
    CGContextRelease(context);

    // A bitmap context only makes premultiplied alpha, so it is undone here.
    std::uint8_t* pixels = image->rgba.data();
    for (std::size_t i = 0; i < width * height; ++i, pixels += 4)
    {
        const unsigned alpha = pixels[3];
        if (alpha == 0 || alpha == 255) continue;
        for (int channel = 0; channel < 3; ++channel)
            pixels[channel] = static_cast<std::uint8_t>(std::min(255u, (pixels[channel] * 255u + alpha / 2) / alpha));
    }

    DecodeResult result;
    result.image = std::move(image);
    return result;
}

DecodeResult decodeSource(CGImageSourceRef imageSource, const std::string& source)
{
    if (!imageSource) return failed("the format is not one macOS can read");
    if (CGImageSourceGetCount(imageSource) == 0 || CGImageSourceGetStatus(imageSource) == kCGImageStatusUnknownType)
        return failed("the format is not one macOS can read");

    // A picture bigger than the limit is read already reduced, so it is never held at full size.
    std::size_t longest = 0;
    if (CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, nullptr))
    {
        auto read = [&](CFStringRef key) -> std::size_t {
            CFNumberRef number = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, key));
            long long value = 0;
            if (number) CFNumberGetValue(number, kCFNumberLongLongType, &value);
            return value > 0 ? static_cast<std::size_t>(value) : 0;
        };
        longest = std::max(read(kCGImagePropertyPixelWidth), read(kCGImagePropertyPixelHeight));
        CFRelease(properties);
    }

    CGImageRef cgImage = nullptr;
    if (longest > kMaxTextureSide)
    {
        const int limit = static_cast<int>(kMaxTextureSide);
        CFNumberRef maxSize = CFNumberCreate(nullptr, kCFNumberIntType, &limit);
        const void* keys[] = {kCGImageSourceCreateThumbnailFromImageAlways, kCGImageSourceThumbnailMaxPixelSize,
                              kCGImageSourceCreateThumbnailWithTransform};
        const void* values[] = {kCFBooleanTrue, maxSize, kCFBooleanFalse};
        CFDictionaryRef options = CFDictionaryCreate(
            nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks
        );
        cgImage = CGImageSourceCreateThumbnailAtIndex(imageSource, 0, options);
        CFRelease(options);
        CFRelease(maxSize);
        if (!cgImage) return failed("the picture is too large to read");
        // The scaled-down note of render compares with the reduced image, so it is added here.
        DecodeResult result = render(cgImage, source);
        CGImageRelease(cgImage);
        if (result.image && longest > kMaxTextureSide)
        {
            auto image = std::make_shared<geo::Image>(*result.image);
            image->source = source + " (scaled down from " + std::to_string(longest) + " px)";
            result.image = std::move(image);
        }
        return result;
    }

    cgImage = CGImageSourceCreateImageAtIndex(imageSource, 0, nullptr);
    if (!cgImage) return failed("the picture is damaged");
    DecodeResult result = render(cgImage, source);
    CGImageRelease(cgImage);
    return result;
}

} // namespace

DecodeResult decodeImage(const void* bytes, std::size_t size, const std::string& source)
{
    if (!bytes || size == 0) return failed("the picture is empty");
    @autoreleasepool
    {
        CFDataRef data = CFDataCreate(nullptr, static_cast<const UInt8*>(bytes), static_cast<CFIndex>(size));
        if (!data) return failed("there is not enough memory for the picture");
        CGImageSourceRef imageSource = CGImageSourceCreateWithData(data, nullptr);
        CFRelease(data);
        DecodeResult result = decodeSource(imageSource, source);
        if (imageSource) CFRelease(imageSource);
        return result;
    }
}

DecodeResult decodeImageFile(const std::filesystem::path& file)
{
    @autoreleasepool
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error)) return failed("the file does not exist");
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8*>(file.c_str()), static_cast<CFIndex>(file.native().size()), false
        );
        if (!url) return failed("the path is not valid");
        CGImageSourceRef imageSource = CGImageSourceCreateWithURL(url, nullptr);
        CFRelease(url);
        DecodeResult result = decodeSource(imageSource, file.string());
        if (imageSource) CFRelease(imageSource);
        return result;
    }
}

DecodeResult decodeCGImage(CGImage* image, const std::string& source)
{
    if (!image) return failed("the picture is empty");
    return render(image, source);
}

} // namespace helix::import
