#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace helix::import {

/// @brief What a process that was run left behind.
struct ProcessResult
{
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    /// @brief What the process wrote on its output and error streams, cut to its last bytes.
    std::string output;
};

/**
 * @brief Runs a program with an argument list, without a shell.
 *
 * The arguments reach the program as they are, so a path with a space, a quote or a dollar sign
 * in it is only ever a path.
 *
 * @param seconds How long the program may run before it is stopped.
 * @param maxOutputBytes How much of the end of its output is kept.
 */
ProcessResult runProcess(
    const std::filesystem::path& program,
    const std::vector<std::string>& arguments,
    int seconds,
    std::size_t maxOutputBytes = 16384
);

/// @brief Finds the Blender to use: the one named, the one in /Applications, then one that Spotlight
/// knows by its bundle identifier.
/// @return The executable inside the Blender app, or an empty path when there is none.
std::filesystem::path findBlender(const std::string& configuredPath);

/// @brief Asks a Blender for its version, such as "5.2.0", or returns an empty string.
std::string blenderVersion(const std::filesystem::path& blender);

/**
 * @brief Converts a .blend file to GLB with Blender running in the background.
 *
 * The GLB has the modifiers applied, with normals and UVs, and leaves out the animations, the
 * skins and the pictures. The result is kept in the blend cache, under a name made from the path,
 * the size and the date of the file and the version of Blender, and a file that was converted
 * before is reused.
 *
 * @param[out] fromCache Set to whether Blender was not run.
 * @return The path of the GLB.
 * @throws ImportError when Blender is missing, fails or takes too long.
 */
std::filesystem::path convertBlendToGlb(
    const std::filesystem::path& blendFile,
    const std::string& configuredBlender,
    bool* fromCache = nullptr
);

/// @brief The start of the names of the cache entries of a .blend file: the hash of its absolute path.
std::string blendCachePrefix(const std::filesystem::path& blendFile);

/**
 * @brief Deletes the converted GLB files the blend cache holds for a .blend file.
 *
 * The cache is keyed on the size and the date of the file, so a file edited in place with both
 * unchanged would be served from it for ever; Reload calls this to force a new conversion.
 * Entries of other files stay.
 *
 * @return The number of files removed.
 */
std::size_t forgetBlendConversion(const std::filesystem::path& blendFile);

/// @brief The number of times Blender was run for a conversion since the start.
std::size_t blenderRunCount();

} // namespace helix::import
