// Compiles the implementation of metal-cpp. This is the only file of the program that
// defines these macros, since each one emits the symbols of the headers it names and a
// second definition would clash at link time.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
