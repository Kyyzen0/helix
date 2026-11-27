#include "HelixCore/ImageFile.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <filesystem>
#include <system_error>

namespace helix::core::detail {

namespace {

// Releases a Core Foundation object when the scope ends.
template <typename T> struct CfRef
{
    explicit CfRef(T ref = nullptr) : ref(ref) {}
    ~CfRef()
    {
        if (ref) CFRelease(ref);
    }
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
    T ref;
};

} // namespace

std::string writePng(
    const std::string& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgba
)
{
    namespace fs = std::filesystem;

    if (width == 0 || height == 0) return "The image has no pixels.";
    if (rgba.size() != std::size_t(width) * height * 4)
        return "The pixels do not match the size of the image.";

    CfRef<CGColorSpaceRef> colorSpace(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CfRef<CGDataProviderRef> provider(
        CGDataProviderCreateWithData(nullptr, rgba.data(), rgba.size(), nullptr)
    );
    if (!colorSpace.ref || !provider.ref) return "The image could not be prepared.";

    // The alpha byte is skipped, so the PNG is opaque whatever the background alpha was.
    CfRef<CGImageRef> image(CGImageCreate(
        width,
        height,
        8,
        32,
        std::size_t(width) * 4,
        colorSpace.ref,
        CGBitmapInfo(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrderDefault,
        provider.ref,
        nullptr,
        false,
        kCGRenderingIntentDefault
    ));
    if (!image.ref) return "The image could not be prepared.";

    const fs::path destination(path);
    const fs::path temporary = destination.string() + ".tmp";
    const std::string temporaryText = temporary.string();
    CfRef<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
        nullptr,
        reinterpret_cast<const UInt8*>(temporaryText.c_str()),
        CFIndex(temporaryText.size()),
        false
    ));
    if (!url.ref) return "The path " + path + " cannot be written.";

    CfRef<CGImageDestinationRef> destinationRef(
        CGImageDestinationCreateWithURL(url.ref, CFSTR("public.png"), 1, nullptr)
    );
    if (!destinationRef.ref) return "Couldn't create " + path + ".";
    CGImageDestinationAddImage(destinationRef.ref, image.ref, nullptr);
    if (!CGImageDestinationFinalize(destinationRef.ref))
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return "Couldn't write " + path + ".";
    }

    std::error_code error;
    fs::rename(temporary, destination, error);
    if (error)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return "Couldn't replace " + path + ": " + error.message();
    }
    return {};
}

} // namespace helix::core::detail
