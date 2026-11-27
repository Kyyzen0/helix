#include "Engine/Import/ImportFile.h"
#include "Engine/Network/Network.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Serializer/Serializer.h"
#include <catch2/catch_test_macros.hpp>
#include <dlfcn.h>
#include <exception>
#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// An exception thrown inside libhelixEngine has to be caught by its standard base class in
// another image (the tests, a node library, the facade). If every image carried its own copy of
// the typeinfo of std::exception, `catch (const std::exception&)` would silently miss it.

namespace {

// Catches by std::exception only, the way every handler in the code base does.
template <typename Thrower>
std::string messageCaughtAsStdException(Thrower&& thrower)
{
    try
    {
        thrower();
    }
    catch (const std::exception& error)
    {
        return error.what();
    }
    catch (...)
    {
        return "<not caught as std::exception>";
    }
    return "<nothing thrown>";
}

} // namespace

TEST_CASE("an engine std::out_of_range is caught as std::exception", "[exceptions]")
{
    helix::nt::Network network;
    const std::string message = messageCaughtAsStdException([&] { network.getNode(4242); });
    CHECK(message.find("4242") != std::string::npos);
}

TEST_CASE("an engine std::runtime_error is caught as std::exception", "[exceptions]")
{
    helix::nt::Serializer serializer;
    const std::string message = messageCaughtAsStdException([&] {
        serializer.save(helix::nt::nm(), "/nonexistent/helix/folder/scene.kut");
    });
    CHECK(message.find("Couldn't open") != std::string::npos);
}

TEST_CASE("an engine ImportError is caught as std::exception and by its own type", "[exceptions]")
{
    const std::filesystem::path missing = "/nonexistent/helix/file.glb";
    CHECK(
        messageCaughtAsStdException([&] { helix::import::importFile(missing, {}); }) !=
        "<not caught as std::exception>"
    );
    CHECK_THROWS_AS(helix::import::importFile(missing, {}), helix::import::ImportError);
    CHECK_THROWS_AS(helix::import::importFile(missing, {}), std::runtime_error);
}

TEST_CASE("an exception of the engine crosses into the tests whatever its standard base", "[exceptions]")
{
    // The dependency cycle of the network graph, a std::runtime_error raised by the engine.
    helix::nt::Serializer serializer;
    CHECK_THROWS_AS(
        serializer.save(helix::nt::nm(), "/nonexistent/helix/folder/scene.kut"), std::runtime_error
    );
    helix::nt::Network network;
    CHECK_THROWS_AS(network.getNode(7), std::out_of_range);
    CHECK_THROWS_AS(network.getNode(7), std::logic_error);
}

namespace {

// Returns what `nm -m` prints for the image, or nothing when the tool cannot run.
std::string symbolsOf(const std::filesystem::path& image)
{
    std::string output;
    const std::string command = "/usr/bin/nm -m '" + image.string() + "' 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return output;
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
    pclose(pipe);
    return output;
}

std::filesystem::path imageOf(const void* address)
{
    Dl_info info{};
    if (!dladdr(address, &info) || !info.dli_fname) return {};
    return info.dli_fname;
}

// True when the image defines a copy of the typeinfo of a standard exception. Such a copy is
// private to the image, so another image cannot match it, and a catch of the base class misses
// what the first throws. The names are the mangled ones nm prints: std::exception,
// std::runtime_error, std::logic_error and std::out_of_range.
bool definesStandardTypeinfo(const std::string& symbols)
{
    static const std::array<const char*, 4> names = {
        "_ZTISt9exception", "_ZTISt13runtime_error", "_ZTISt11logic_error", "_ZTISt12out_of_range"
    };
    std::size_t start = 0;
    while (start < symbols.size())
    {
        std::size_t end = symbols.find('\n', start);
        if (end == std::string::npos) end = symbols.size();
        const std::string line = symbols.substr(start, end - start);
        start = end + 1;
        if (line.find("(undefined)") != std::string::npos) continue;
        for (const char* name : names)
            if (line.find(name) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST_CASE("no image defines its own typeinfo for the standard exceptions", "[exceptions]")
{
    const std::filesystem::path engine = imageOf(reinterpret_cast<const void*>(&helix::nt::nm));
    const std::filesystem::path tests = imageOf(reinterpret_cast<const void*>(&symbolsOf));
    REQUIRE_FALSE(engine.empty());
    std::vector<std::filesystem::path> images = {engine, tests};
    const std::filesystem::path nodes = engine.parent_path() / "nodes" / "libhelixOps.dylib";
    if (std::filesystem::exists(nodes)) images.push_back(nodes);

    for (const auto& image : images)
    {
        const std::string symbols = symbolsOf(image);
        if (symbols.empty()) SKIP("nm is not available");
        INFO(image.string());
        CHECK_FALSE(definesStandardTypeinfo(symbols));
    }
}
