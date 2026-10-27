#pragma once
// The one include of Apple's metal-cpp for the renderer. The header only interface needs
// its implementation compiled once, which `MetalImplementation.cpp` does.
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

namespace helix::gfx {

/// @brief An autorelease pool that lives for a scope.
///
/// metal-cpp hands out many objects autoreleased, such as descriptors and encoders. A
/// thread that has no pool of its own, such as a test or a render thread, would leak
/// them, so every piece of work that creates Metal objects runs inside one.
class ScopedAutoreleasePool
{
  public:
    ScopedAutoreleasePool() : pool_(NS::AutoreleasePool::alloc()->init()) {}
    ~ScopedAutoreleasePool() { pool_->release(); }

    ScopedAutoreleasePool(const ScopedAutoreleasePool&) = delete;
    ScopedAutoreleasePool& operator=(const ScopedAutoreleasePool&) = delete;

  private:
    NS::AutoreleasePool* pool_;
};

/// @brief Returns an NS::String for the text, autoreleased.
inline NS::String* toNsString(const char* text) { return NS::String::string(text, NS::UTF8StringEncoding); }

} // namespace helix::gfx
