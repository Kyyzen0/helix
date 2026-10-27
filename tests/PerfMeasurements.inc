// The measurements that docs/PERFORMANCE.md quotes.
//
// The file is included by tests/Benchmarks.cpp, because the bench target
// compiles that file alone and CMakeLists.txt belongs to the build owner.
//
// Every line printed here starts with MEAS, so the numbers can be picked out
// of the noise the engine makes while it loads.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <dlfcn.h>
#include <functional>
#include <libproc.h>
#include <mach/mach.h>
#include <string>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <thread>
#include <vector>

namespace perf {

using Clock = std::chrono::steady_clock;

/// Returns the milliseconds between two points in time.
double msBetween(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

/// The statistics of a set of measurements.
struct Samples
{
    double min = 0.0;
    double median = 0.0;
    double max = 0.0;
    double mean = 0.0;
};

/// Returns the statistics of the given measurements.
Samples summarise(std::vector<double> values)
{
    Samples samples;
    if (values.empty()) return samples;
    std::sort(values.begin(), values.end());
    samples.min = values.front();
    samples.max = values.back();
    samples.median = values[values.size() / 2];
    double total = 0.0;
    for (double value : values) total += value;
    samples.mean = total / static_cast<double>(values.size());
    return samples;
}

/// Prints the statistics of a measurement.
void printSamples(const std::string& name, const Samples& samples, const std::string& unit)
{
    std::cout << "MEAS " << name << " unit=" << unit << " min=" << samples.min
              << " median=" << samples.median << " max=" << samples.max
              << " mean=" << samples.mean << "\n";
}

/// Returns the resident memory of this process right now, in bytes.
size_t residentBytes()
{
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(
            mach_task_self(),
            MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&info),
            &count
        )
        != KERN_SUCCESS)
        return 0;
    return static_cast<size_t>(info.resident_size);
}

/// Returns the peak resident memory of this process, in bytes.
size_t peakResidentBytes()
{
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return static_cast<size_t>(usage.ru_maxrss);
}

/// Returns the milliseconds since the process was launched, or -1 when the
/// start time cannot be read.
double millisecondsSinceLaunch()
{
    struct proc_bsdinfo info;
    const int size = proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof(info));
    if (size <= 0) return -1.0;

    struct timeval now;
    gettimeofday(&now, nullptr);
    const double seconds = static_cast<double>(now.tv_sec)
                           - static_cast<double>(info.pbi_start_tvsec);
    const double micros = static_cast<double>(now.tv_usec)
                           - static_cast<double>(info.pbi_start_tvusec);
    return seconds * 1000.0 + micros / 1000.0;
}

using Counter = unsigned long long (*)();

/// The counters the counting library keeps, when it is loaded.
struct AllocationCounts
{
    bool available = false;
    unsigned long long allocations = 0;
    unsigned long long frees = 0;
    unsigned long long bytes = 0;
};

/// Reads the counters of the counting library.
///
/// The library is tests/AllocCounter.c, loaded next to the benchmark with
/// DYLD_INSERT_LIBRARIES. Without it the counts are not available.
AllocationCounts readAllocationCounts()
{
    static Counter allocations = reinterpret_cast<Counter>(
        dlsym(RTLD_DEFAULT, "helix_allocation_count")
    );
    static Counter frees =
        reinterpret_cast<Counter>(dlsym(RTLD_DEFAULT, "helix_free_count"));
    static Counter bytes =
        reinterpret_cast<Counter>(dlsym(RTLD_DEFAULT, "helix_allocated_bytes"));

    AllocationCounts counts;
    if (!allocations) return counts;
    counts.available = true;
    counts.allocations = allocations();
    counts.frees = frees ? frees() : 0;
    counts.bytes = bytes ? bytes() : 0;
    return counts;
}

/// Returns a grid node with the given number of rows and columns.
helix::nt::NodeId addGrid(helix::nt::NetworkManager& nm, int rows, int columns)
{
    const helix::nt::NodeId grid = nm.createNode("helix::grid");
    nm.getNode(grid).getParameter("rows").lock()->setInt(rows);
    nm.getNode(grid).getParameter("columns").lock()->setInt(columns);
    return grid;
}

/// Returns the number of points in the first mesh a node produces.
unsigned int pointCount(helix::nt::NetworkManager& nm, helix::nt::NodeId node)
{
    const std::shared_ptr<const helix::NodePacket> packet = nm.getNode(node).getOutputPacket(0);
    if (!packet) return 0;
    const auto mesh =
        std::dynamic_pointer_cast<const helix::geo::Mesh>(packet->getPrimitive(0));
    if (!mesh) return 0;
    return static_cast<unsigned int>(mesh->getNumPoints());
}

/// What one measurement of a cook found.
struct CookMeasurement
{
    /// How long the calling thread was held up by the cook.
    Samples blocked;
    /// How long it took until the new geometry was on screen.
    Samples latency;
    /// The allocations one cook made.
    unsigned long long allocations = 0;
    /// The bytes one cook asked for.
    unsigned long long bytes = 0;
    /// Whether the counting library was loaded.
    bool allocationsAvailable = false;
};

/// Measures a cook of a scene.
///
/// The scene is changed by @p changeScene, which dirties it the way a
/// parameter change does. The manager cooks in the background when @p background
/// is set, and on the calling thread when it is not.
CookMeasurement measureCook(
    const std::function<void(int)>& changeScene,
    int iterations,
    bool background
)
{
    using namespace helix;
    auto& nm = nt::nm();

    // The signal fires when the new geometry is put on screen.
    int publishCount = 0;
    Clock::time_point publishTime;
    auto connection = nm.displayGeoChanged.connect(
        [&](std::shared_ptr<const helix::NodePacket>) {
            ++publishCount;
            publishTime = Clock::now();
        }
    );

    std::vector<double> blocked;
    std::vector<double> latency;
    std::vector<double> allocations;
    std::vector<double> bytes;

    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        const int publishesBefore = publishCount;
        publishTime = Clock::now();

        const AllocationCounts before = readAllocationCounts();

        const Clock::time_point changeStart = Clock::now();
        changeScene(iteration);
        const Clock::time_point changeEnd = Clock::now();

        Clock::time_point resultTime = changeEnd;
        if (background)
        {
            // The interface picks the result up once a frame.
            const Clock::time_point deadline = changeEnd + std::chrono::seconds(60);
            while (publishCount == publishesBefore && Clock::now() < deadline)
            {
                nm.processPendingCooks();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            resultTime = publishTime;
        }

        const AllocationCounts after = readAllocationCounts();

        blocked.push_back(msBetween(changeStart, changeEnd));
        latency.push_back(msBetween(changeStart, resultTime));
        if (before.available && after.available)
        {
            allocations.push_back(
                static_cast<double>(after.allocations - before.allocations)
            );
            bytes.push_back(static_cast<double>(after.bytes - before.bytes));
        }
    }

    CookMeasurement measurement;
    measurement.blocked = summarise(blocked);
    measurement.latency = summarise(latency);
    measurement.allocationsAvailable = !allocations.empty();
    if (measurement.allocationsAvailable)
    {
        measurement.allocations = static_cast<unsigned long long>(summarise(allocations).median);
        measurement.bytes = static_cast<unsigned long long>(summarise(bytes).median);
    }
    return measurement;
}

/// Prints what a measurement of a cook found.
void printCookMeasurement(
    const std::string& name,
    const CookMeasurement& measurement,
    const std::string& unit
)
{
    printSamples(name + ".blocked", measurement.blocked, unit);
    printSamples(name + ".latency", measurement.latency, unit);
    if (measurement.allocationsAvailable)
    {
        std::cout << "MEAS " << name << ".allocations unit=count median="
                  << measurement.allocations << "\n";
        std::cout << "MEAS " << name << ".allocated_bytes unit=bytes median="
                  << measurement.bytes << "\n";
    }
    else
    {
        std::cout << "MEAS " << name << ".allocations unit=count value=unavailable\n";
    }
}

} // namespace perf

TEST_CASE_METHOD(NMReset, "Perf: probe of the pieces a cook is made of")
{
    using namespace helix;
    auto& nm = nt::nm();

    std::cout << "MEAS probe.hardware_threads value=" << std::thread::hardware_concurrency()
              << "\n";

    // One grid on its own.
    const nt::NodeId grid = perf::addGrid(nm, 400, 400);
    nm.cook(grid);
    std::cout << "MEAS probe.grid_400x400_points value=" << perf::pointCount(nm, grid) << "\n";

    const auto timeCook = [&](const std::string& name, nt::NodeId node) {
        nm.getNode(node).dirtyNode(false);
        const perf::Clock::time_point start = perf::Clock::now();
        nm.cook(node);
        const double elapsed = perf::msBetween(start, perf::Clock::now());
        std::cout << "MEAS probe." << name << " unit=ms value=" << elapsed << "\n";
    };

    timeCook("grid_400x400_cook", grid);

    // Eight grids under a merge, which is the network the background cook can
    // spread over its workers.
    std::vector<nt::NodeId> branches;
    const nt::NodeId merge = nm.createNode("helix::merge");
    for (int i = 0; i < 8; ++i)
    {
        const nt::NodeId branch = perf::addGrid(nm, 400, 400);
        nm.connectNodes(branch, 0, merge, 0);
        branches.push_back(branch);
    }
    nm.cook(merge);
    std::cout << "MEAS probe.merge_of_eight_points value=" << perf::pointCount(nm, merge)
              << "\n";

    // Every branch on its own.
    const perf::Clock::time_point branchStart = perf::Clock::now();
    for (nt::NodeId branch : branches)
    {
        nm.getNode(branch).dirtyNode(false);
        nm.cook(branch);
    }
    const double branchElapsed = perf::msBetween(branchStart, perf::Clock::now());
    std::cout << "MEAS probe.eight_branch_cooks unit=ms value=" << branchElapsed << "\n";

    // The merge on its own.
    nm.getNode(merge).dirtyNode(false);
    const perf::Clock::time_point mergeStart = perf::Clock::now();
    nm.cook(merge);
    std::cout << "MEAS probe.merge_cook unit=ms value="
              << perf::msBetween(mergeStart, perf::Clock::now()) << "\n";
}

TEST_CASE_METHOD(NMReset, "Perf: dense point grid, cook on the calling thread")
{
    using namespace helix;
    auto& nm = nt::nm();

    const nt::NodeId grid = perf::addGrid(nm, 1000, 1000);
    const nt::NodeId transform = nm.createNode("helix::transform");
    nm.connectNodes(grid, 0, transform, 0);
    nm.setDisplayNode(transform);

    std::cout << "MEAS scene.dense_points value=" << perf::pointCount(nm, transform) << "\n";

    // A change of the size of the grid dirties the grid and the transform above it.
    const auto changeScene = [&](int iteration) {
        nm.getNode(grid)
            .getParameter("size")
            .lock()
            ->setFloat(10.0f + 0.001f * static_cast<float>(iteration), 0);
    };

    const perf::CookMeasurement measurement = perf::measureCook(changeScene, 5, false);
    perf::printCookMeasurement("cook.sync.dense", measurement, "ms");
}

TEST_CASE_METHOD(NMReset, "Perf: dense point grid, cook in the background")
{
    using namespace helix;
    auto& nm = nt::nm();
    nm.setAsyncCookingEnabled(true);

    const nt::NodeId grid = perf::addGrid(nm, 1000, 1000);
    const nt::NodeId transform = nm.createNode("helix::transform");
    nm.connectNodes(grid, 0, transform, 0);
    nm.setDisplayNode(transform);

    std::cout << "MEAS scene.dense_points value=" << perf::pointCount(nm, transform) << "\n";

    const auto changeScene = [&](int iteration) {
        nm.getNode(grid)
            .getParameter("size")
            .lock()
            ->setFloat(10.0f + 0.001f * static_cast<float>(iteration), 0);
    };

    const perf::CookMeasurement measurement = perf::measureCook(changeScene, 5, true);
    perf::printCookMeasurement("cook.background.dense", measurement, "ms");
}

TEST_CASE_METHOD(NMReset, "Perf: network of eight dense branches, cook on the calling thread")
{
    using namespace helix;
    auto& nm = nt::nm();

    std::vector<nt::NodeId> branches;
    const nt::NodeId merge = nm.createNode("helix::merge");
    for (int i = 0; i < 8; ++i)
    {
        const nt::NodeId branch = perf::addGrid(nm, 400, 400);
        nm.connectNodes(branch, 0, merge, 0);
        branches.push_back(branch);
    }
    nm.setDisplayNode(merge);

    std::cout << "MEAS scene.branch_points value=" << perf::pointCount(nm, merge) << "\n";

    // Every branch is changed, so the cook has eight nodes it can spread out.
    const auto changeScene = [&](int iteration) {
        for (size_t i = 0; i < branches.size(); ++i)
            nm.getNode(branches[i])
                .getParameter("size")
                .lock()
                ->setFloat(
                    10.0f + 0.001f * static_cast<float>(iteration)
                        + 0.001f * static_cast<float>(i),
                    0
                );
    };

    const perf::CookMeasurement measurement = perf::measureCook(changeScene, 5, false);
    perf::printCookMeasurement("cook.sync.network", measurement, "ms");
}

TEST_CASE_METHOD(NMReset, "Perf: network of eight dense branches, cook in the background")
{
    using namespace helix;
    auto& nm = nt::nm();
    nm.setAsyncCookingEnabled(true);

    std::vector<nt::NodeId> branches;
    const nt::NodeId merge = nm.createNode("helix::merge");
    for (int i = 0; i < 8; ++i)
    {
        const nt::NodeId branch = perf::addGrid(nm, 400, 400);
        nm.connectNodes(branch, 0, merge, 0);
        branches.push_back(branch);
    }
    nm.setDisplayNode(merge);

    std::cout << "MEAS scene.branch_points value=" << perf::pointCount(nm, merge) << "\n";

    const auto changeScene = [&](int iteration) {
        for (size_t i = 0; i < branches.size(); ++i)
            nm.getNode(branches[i])
                .getParameter("size")
                .lock()
                ->setFloat(
                    10.0f + 0.001f * static_cast<float>(iteration)
                        + 0.001f * static_cast<float>(i),
                    0
                );
    };

    const perf::CookMeasurement measurement = perf::measureCook(changeScene, 5, true);
    perf::printCookMeasurement("cook.background.network", measurement, "ms");
}

TEST_CASE_METHOD(NMReset, "Perf: peak memory of a one million point scene")
{
    using namespace helix;
    auto& nm = nt::nm();

    const size_t residentAtStart = perf::residentBytes();
    std::cout << "MEAS memory.resident_at_start unit=bytes value=" << residentAtStart << "\n";

    const nt::NodeId grid = perf::addGrid(nm, 1000, 1000);
    const nt::NodeId transform = nm.createNode("helix::transform");
    nm.connectNodes(grid, 0, transform, 0);
    nm.setDisplayNode(transform);

    const size_t residentAfterCook = perf::residentBytes();
    const size_t peak = perf::peakResidentBytes();

    std::cout << "MEAS scene.dense_points value=" << perf::pointCount(nm, transform) << "\n";
    std::cout << "MEAS memory.resident_after_cook unit=bytes value=" << residentAfterCook
              << "\n";
    std::cout << "MEAS memory.peak_resident unit=bytes value=" << peak << "\n";
    std::cout << "MEAS memory.growth unit=bytes value="
              << static_cast<double>(residentAfterCook) - static_cast<double>(residentAtStart)
              << "\n";
}

TEST_CASE("Perf: engine startup")
{
    using namespace helix;

    const double launchToRegistry = perf::millisecondsSinceLaunch();
    const perf::Clock::time_point start = perf::Clock::now();
    nt::NodeLoader::loadNodes();
    const double registryLoad = perf::msBetween(start, perf::Clock::now());

    std::cout << "MEAS startup.launch_to_registry_loaded unit=ms value=" << launchToRegistry
              << "\n";
    std::cout << "MEAS startup.node_registry_load unit=ms value=" << registryLoad << "\n";

    // The first scene the interface shows.
    auto& nm = nt::nm();
    const nt::NodeId grid = perf::addGrid(nm, 1000, 1000);
    const nt::NodeId transform = nm.createNode("helix::transform");
    nm.connectNodes(grid, 0, transform, 0);
    nm.setDisplayNode(transform);

    std::cout << "MEAS startup.launch_to_first_cook unit=ms value="
              << perf::millisecondsSinceLaunch() << "\n";
    std::cout << "MEAS startup.first_scene_points value=" << perf::pointCount(nm, transform)
              << "\n";
}
