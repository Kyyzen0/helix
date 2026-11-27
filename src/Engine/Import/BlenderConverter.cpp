#include "Engine/Import/BlenderConverter.h"
#include "Engine/Import/ImportFile.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <mutex>
#include <poll.h>
#include <regex>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace helix::import {

namespace fs = std::filesystem;

namespace {

std::atomic<std::size_t> blenderRuns{0};

// The script Blender runs. It is a constant: nothing about the file is written into it, the output
// path reaches it as an argument after "--" and the file to convert is opened by Blender itself.
// Exporter options the running Blender does not know are left out, so one script serves the
// versions whose glTF exporter differs in what it offers.
constexpr const char* kExportScript = R"PY(
import bpy, sys
out = sys.argv[sys.argv.index("--") + 1]
wanted = dict(
    export_format="GLB",
    export_apply=True,
    export_normals=True,
    export_texcoords=True,
    export_image_format="NONE",
    # Skins and animations are exported so a rigged .blend keeps its armature and its actions.
    # export_apply applies every modifier except the armature one (the exporter says so), so the
    # mesh stays bound to its skeleton.
    export_animations=True,
    export_skins=True,
    export_morph=False,
    export_cameras=False,
    export_lights=False,
    export_yup=True,
    export_draco_mesh_compression_enable=False,
    use_visible=True,
)
known = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
options = {k: v for k, v in wanted.items() if k in known}
result = bpy.ops.export_scene.gltf(filepath=out, **options)
if "FINISHED" not in result:
    raise RuntimeError("the glTF exporter did not finish: " + str(result))
)PY";

constexpr int kConversionSeconds = 600;
constexpr int kProbeSeconds = 30;

// Hashes a string to a short, stable name, so a cache entry can be found again by the same key.
std::string hashToName(const std::string& text)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : text)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream name;
    name << std::hex << hash;
    return name.str();
}

bool isExecutableFile(const fs::path& path)
{
    std::error_code ignored;
    return fs::is_regular_file(path, ignored) && ::access(path.c_str(), X_OK) == 0;
}

fs::path executableInApp(const fs::path& app)
{
    return app / "Contents" / "MacOS" / "Blender";
}

// The version a Blender app declares in its Info.plist, when that file is readable text.
std::string versionFromPlist(const fs::path& executable)
{
    const fs::path plist = executable.parent_path().parent_path() / "Info.plist";
    std::ifstream stream(plist);
    if (!stream.is_open()) return {};
    std::ostringstream text;
    text << stream.rdbuf();
    static const std::regex pattern(
        R"(<key>CFBundleShortVersionString</key>\s*<string>([^<]+)</string>)"
    );
    std::smatch match;
    const std::string content = text.str();
    if (std::regex_search(content, match, pattern)) return match[1];
    return {};
}

} // namespace

ProcessResult runProcess(
    const fs::path& program,
    const std::vector<std::string>& arguments,
    int seconds,
    std::size_t maxOutputBytes
)
{
    ProcessResult result;

    int pipeFds[2];
    if (::pipe(pipeFds) != 0) return result;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // The program writes its output and its errors to the same pipe and reads nothing.
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    posix_spawn_file_actions_addclose(&actions, pipeFds[1]);

    // Its own process group, so a program that is stopped takes its children with it.
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);

    std::vector<std::string> storage;
    storage.push_back(program.string());
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (std::string& argument : storage)
        argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawnStatus =
        ::posix_spawn(&pid, program.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    ::close(pipeFds[1]);
    if (spawnStatus != 0)
    {
        ::close(pipeFds[0]);
        return result;
    }
    result.started = true;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    bool exited = false;
    int waitStatus = 0;
    char buffer[4096];
    while (!exited)
    {
        pollfd descriptor{pipeFds[0], POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 100);
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)))
        {
            const ssize_t count = ::read(pipeFds[0], buffer, sizeof(buffer));
            if (count > 0)
            {
                result.output.append(buffer, static_cast<std::size_t>(count));
                // Only the end of what it says is kept, which is where it reports why it failed.
                if (result.output.size() > 2 * maxOutputBytes)
                    result.output.erase(0, result.output.size() - maxOutputBytes);
            }
        }
        if (::waitpid(pid, &waitStatus, WNOHANG) == pid) exited = true;
        else if (std::chrono::steady_clock::now() > deadline)
        {
            result.timedOut = true;
            ::kill(-pid, SIGTERM);
            for (int attempt = 0; attempt < 30 && ::waitpid(pid, &waitStatus, WNOHANG) != pid; ++attempt)
                ::usleep(100000);
            if (::waitpid(pid, &waitStatus, WNOHANG) != pid)
            {
                ::kill(-pid, SIGKILL);
                ::waitpid(pid, &waitStatus, 0);
            }
            exited = true;
        }
    }

    // What the program wrote just before it ended.
    ::fcntl(pipeFds[0], F_SETFL, O_NONBLOCK);
    ssize_t count;
    while ((count = ::read(pipeFds[0], buffer, sizeof(buffer))) > 0)
        result.output.append(buffer, static_cast<std::size_t>(count));
    ::close(pipeFds[0]);
    if (result.output.size() > maxOutputBytes)
        result.output.erase(0, result.output.size() - maxOutputBytes);

    if (WIFEXITED(waitStatus)) result.exitCode = WEXITSTATUS(waitStatus);
    return result;
}

fs::path findBlender(const std::string& configuredPath)
{
    if (!configuredPath.empty())
    {
        fs::path configured(configuredPath);
        std::error_code ignored;
        if (fs::is_directory(configured, ignored)) configured = executableInApp(configured);
        return isExecutableFile(configured) ? configured : fs::path();
    }

    const fs::path standard = executableInApp("/Applications/Blender.app");
    if (isExecutableFile(standard)) return standard;
    if (const char* home = std::getenv("HOME"))
    {
        const fs::path user = executableInApp(fs::path(home) / "Applications" / "Blender.app");
        if (isExecutableFile(user)) return user;
    }

    // Spotlight knows an installation the person put somewhere else.
    const ProcessResult found = runProcess(
        "/usr/bin/mdfind",
        {"kMDItemCFBundleIdentifier == 'org.blenderfoundation.blender'"},
        10
    );
    if (found.started && found.exitCode == 0)
    {
        std::istringstream lines(found.output);
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.empty() || line.front() != '/') continue;
            const fs::path candidate = executableInApp(line);
            if (isExecutableFile(candidate)) return candidate;
        }
    }
    return {};
}

std::string blenderVersion(const fs::path& blender)
{
    static std::mutex mutex;
    static std::map<std::string, std::string> versions;
    const std::lock_guard<std::mutex> lock(mutex);

    const auto known = versions.find(blender.string());
    if (known != versions.end()) return known->second;

    std::string version = versionFromPlist(blender);
    if (version.empty())
    {
        const ProcessResult probe = runProcess(blender, {"--version"}, kProbeSeconds);
        static const std::regex pattern(R"(Blender\s+(\d+\.\d+(\.\d+)?))");
        std::smatch match;
        if (std::regex_search(probe.output, match, pattern)) version = match[1];
    }
    versions[blender.string()] = version;
    return version;
}

std::size_t blenderRunCount() { return blenderRuns.load(); }

fs::path convertBlendToGlb(const fs::path& blendFile, const std::string& configuredBlender, bool* fromCache)
{
    if (fromCache) *fromCache = false;

    const fs::path blender = findBlender(configuredBlender);
    if (blender.empty())
    {
        if (!configuredBlender.empty())
            throw ImportError("Blender was not found at " + configuredBlender + ".");
        throw ImportError("Opening .blend files needs Blender installed (blender.org).");
    }

    std::error_code error;
    const fs::path absolute = fs::absolute(blendFile, error);
    const std::uintmax_t size = fs::file_size(absolute, error);
    const auto modified = fs::last_write_time(absolute, error).time_since_epoch().count();
    if (error) throw ImportError("Couldn't read " + blendFile.string() + ".");

    const std::string version = blenderVersion(blender);
    const std::string key = absolute.string() + "|" + std::to_string(size) + "|" +
                            std::to_string(static_cast<long long>(modified)) + "|" + version +
                            // Bumped when the export script changes what the glb holds, so an
                            // older conversion (made without skins) is not served again.
                            "|skins1";
    const fs::path directory = blendCacheDirectory();
    const fs::path output = directory / (hashToName(key) + ".glb");

    if (fs::is_regular_file(output, error) && fs::file_size(output, error) > 0)
    {
        if (fromCache) *fromCache = true;
        return output;
    }

    fs::create_directories(directory, error);
    if (error)
        throw ImportError("Couldn't create the conversion folder " + directory.string() + ".");

    // Blender writes next to the final name and the result is renamed into place, so a
    // conversion that is cut short never leaves a file that looks complete.
    const fs::path partial = directory / (hashToName(key) + ".partial-" + std::to_string(::getpid()) + ".glb");
    fs::remove(partial, error);

    ++blenderRuns;
    const ProcessResult run = runProcess(
        blender,
        {"--background",
         "--factory-startup",
         "--disable-autoexec",
         absolute.string(),
         "--python-exit-code",
         "1",
         "--python-expr",
         kExportScript,
         "--",
         partial.string()},
        kConversionSeconds
    );

    if (!run.started) throw ImportError("Blender could not be started (" + blender.string() + ").");
    if (run.timedOut)
    {
        fs::remove(partial, error);
        throw ImportError(
            "Blender took longer than " + std::to_string(kConversionSeconds / 60) +
            " minutes to convert the file and was stopped."
        );
    }
    if (run.exitCode != 0 || !fs::is_regular_file(partial, error) || fs::file_size(partial, error) == 0)
    {
        fs::remove(partial, error);
        // Blender prints a good deal of noise. The lines that say what went wrong are the ones
        // that mention an error, and the last of them is the cause.
        std::vector<std::string> interesting;
        std::istringstream lines(run.output);
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos ||
                line.find("Traceback") != std::string::npos || line.find("RuntimeError") != std::string::npos)
                interesting.push_back(line);
        }
        std::string reason;
        for (std::size_t i = interesting.size() > 3 ? interesting.size() - 3 : 0; i < interesting.size(); ++i)
            reason += (reason.empty() ? "" : " ") + interesting[i];
        if (reason.empty()) reason = "exit code " + std::to_string(run.exitCode);
        if (reason.size() > 500) reason.resize(500);
        throw ImportError("Blender could not convert the file: " + reason);
    }

    fs::rename(partial, output, error);
    if (error)
    {
        fs::remove(partial, error);
        throw ImportError("Couldn't store the converted file in " + directory.string() + ".");
    }
    return output;
}

} // namespace helix::import
