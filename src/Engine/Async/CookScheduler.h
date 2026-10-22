#pragma once

#include "Engine/Async/CookTypes.h"
#include "Engine/Core/Types.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace enzo::async {

/**
 * @class CookScheduler
 * @brief Runs cooks on a pool of worker threads.
 *
 * The scheduler takes a cook as a set of levels. Every node in a level reads
 * only nodes in earlier levels, so a whole level cooks at the same time and
 * the next level waits for it. The nodes of one level are handed out one at a
 * time, so a thread that finishes early picks up the next node rather than
 * sitting idle.
 *
 * Only one cook runs at a time. Asking for a new cook does not put it behind
 * the running one, it takes its place: the running cook is told to stop and
 * the new one starts as soon as a thread is free. Asking again before that
 * replaces the request once more, so a burst of edits leaves one cook behind
 * rather than a queue of them.
 *
 * @note The scheduler knows nothing about nodes. It is handed the levels to
 * cook and a function that cooks one node, so it holds no engine state of its
 * own and can be reasoned about on its own.
 */
class CookScheduler
{
  public:
    /// @brief Cooks one node. Runs on a worker thread.
    using CookNodeFn = std::function<void(nt::NodeId, const CookCancelToken&)>;

    /// @brief Called once when a cook finishes. Runs on the worker that finished it.
    using CookFinishedFn = std::function<void(CookGeneration, CookStatus)>;

    /**
     * @brief Builds a scheduler and starts its workers.
     *
     * @param threadCount How many workers to run. Zero picks a number from the
     * machine, leaving one core for the interface.
     */
    explicit CookScheduler(unsigned int threadCount = 0);

    /// @brief Stops the workers and waits for them.
    ~CookScheduler();

    CookScheduler(const CookScheduler&) = delete;
    CookScheduler& operator=(const CookScheduler&) = delete;

    /**
     * @brief Starts a cook, abandoning any cook already running.
     *
     * @param levels The nodes to cook, grouped so a whole level can run at once.
     * @param cookNode Cooks one node. Called on a worker thread.
     *
     * @return The generation of the new cook.
     */
    CookGeneration requestCook(std::vector<std::vector<nt::NodeId>> levels, CookNodeFn cookNode);

    /// @brief Abandons the running cook and drops the pending one.
    void cancel();

    /// @brief Returns whether a cook is wanted, running or waiting to start.
    bool isBusy() const;

    /// @brief Returns whether a worker is inside a cook right now.
    bool isCooking() const;

    /// @brief Returns the generation of the newest request.
    CookGeneration generation() const;

    /// @brief Sets the function called once when a cook finishes.
    void setCookFinishedFn(CookFinishedFn cookFinishedFn);

    /// @brief Returns how many workers the pool holds.
    unsigned int threadCount() const { return static_cast<unsigned int>(threads_.size()); }

  private:
    /// @brief The state of one level, shared by the workers cooking it.
    struct LevelState
    {
        // The next node of the level to hand out.
        std::atomic<size_t> nextIndex{0};
        // How many of the level's nodes have finished cooking.
        std::atomic<size_t> completed{0};
        // Guards the wait, and carries the release of the finished work.
        std::mutex mutex;
        std::condition_variable cv;
    };

    /// @brief One cook, shared by the workers taking part in it.
    struct JobState
    {
        CookGeneration generation = kNoGeneration;
        std::vector<std::vector<nt::NodeId>> levels;
        // A deque, because a level holds a lock and cannot be moved.
        std::deque<LevelState> levelStates;
        CookNodeFn cookNode;
        CookCancelToken token;
        // How many workers have left this cook.
        std::atomic<size_t> workersDone{0};
    };

    /// @brief The body of a worker thread.
    void workerLoop_();

    /// @brief Takes part in one cook, from its first level to its last.
    void runJob_(const std::shared_ptr<JobState>& job);

    // Guards the members below.
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    // Whether the workers should stop.
    bool stop_ = false;

    // Whether a cook is wanted, and the cook that is wanted.
    bool hasJob_ = false;
    std::shared_ptr<JobState> job_;

    // The generation of the newest request.
    CookGeneration generation_ = kNoGeneration;

    // The workers.
    std::vector<std::thread> threads_;

    // How many workers are inside a cook right now.
    std::atomic<size_t> activeWorkers_{0};

    // Called once when a cook finishes.
    CookFinishedFn cookFinishedFn_;
};

} // namespace enzo::async
