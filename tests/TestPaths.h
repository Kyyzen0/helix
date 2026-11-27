#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace helix::test {

/// @brief Returns a path for a scratch file in the temporary directory, which follows $TMPDIR.
inline std::string tempPath(std::string_view fileName)
{
    return (std::filesystem::temp_directory_path() / fileName).string();
}

} // namespace helix::test
