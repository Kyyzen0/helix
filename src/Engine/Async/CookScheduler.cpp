#include "Engine/Async/CookScheduler.h"

#include <algorithm>

namespace enzo::async {

namespace {

/// @brief Picks a sensible number of workers for this machine.
unsigned int defaultThreadCount()
{
    const unsigned int cores = std::thread::hardware_concurrency();
    if (cores <= 1) return 1;
    // Leave a core for the interface, which stays responsive while a cook runs.
    return std::min(cores - 1, 8u);
}

} // namespace

CookScheduler::CookScheduler(unsigned int threadCount)
{
    if (threadCount == 0) threadCount = defaultThreadCount();

    threads_.reserve(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i)
        threads_.emplace_back([this] { workerLoop_(); });
}

CookScheduler::~CookScheduler()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        // Tell the running cook to stop so the workers leave it promptly.
        if (job_) job_->token.cancel();
    }
    cv_.notify_all();

    for (std::thread& thread : threads_)
        if (thread.joinable()) thread.join();
}

CookGeneration CookScheduler::requestCook(
    std::vector<std::vector<nt::NodeId>> levels,
    CookNodeFn cookNode
)
{
    auto job = std::make_shared<JobState>();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // The cook that is running stops, rather than finishing before the new
        // one starts. The new cook takes its place in the same request.
        if (job_) job_->token.cancel();

        generation_ += 1;
        job->generation = generation_;
        job->levels = std::move(levels);
        job->levelStates.resize(job->levels.size());
        job->cookNode = std::move(cookNode);

        job_ = job;
        hasJob_ = true;
    }

    cv_.notify_all();
    return job->generation;
}

void CookScheduler::cancel()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation_ += 1;
        if (job_)
        {
            job_->token.cancel();
            job_.reset();
        }
        hasJob_ = false;
    }
    cv_.notify_all();
}

bool CookScheduler::isBusy() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return hasJob_;
}

bool CookScheduler::isCooking() const
{
    return activeWorkers_.load(std::memory_order_acquire) > 0;
}

CookGeneration CookScheduler::generation() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
}

void CookScheduler::setCookFinishedFn(CookFinishedFn cookFinishedFn)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cookFinishedFn_ = std::move(cookFinishedFn);
}

void CookScheduler::workerLoop_()
{
    // The generation this worker last took part in, so it only joins a cook
    // once and does not sit in the same one twice.
    CookGeneration joined = kNoGeneration;

    for (;;)
    {
        std::shared_ptr<JobState> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this, &joined] {
                return stop_ || (hasJob_ && job_ && job_->generation != joined);
            });
            if (stop_) return;
            job = job_;
            joined = job_->generation;
        }

        runJob_(job);
    }
}

void CookScheduler::runJob_(const std::shared_ptr<JobState>& job)
{
    activeWorkers_.fetch_add(1, std::memory_order_acq_rel);

    for (size_t levelIndex = 0; levelIndex < job->levels.size(); ++levelIndex)
    {
        const std::vector<nt::NodeId>& level = job->levels[levelIndex];
        LevelState& state = job->levelStates[levelIndex];

        // Take nodes off the level until it runs out. Every worker takes the
        // next one, so the work spreads over the pool.
        for (;;)
        {
            if (job->token.isCancelled()) break;

            const size_t index = state.nextIndex.fetch_add(1, std::memory_order_relaxed);
            if (index >= level.size()) break;

            // The cook may have been abandoned between the check and the take.
            if (job->token.isCancelled()) break;

            job->cookNode(level[index], job->token);

            state.completed.fetch_add(1, std::memory_order_release);
            // Wake the workers waiting on this level, so they look again.
            { std::lock_guard<std::mutex> lock(state.mutex); }
            state.cv.notify_all();
        }

        // Wait for the level to finish before the next one starts. The nodes of
        // the next level read the geometry this one produced, so the wait also
        // carries that work across to them.
        //
        // A cook that was abandoned does not wait, since the nodes it is
        // waiting for are never going to arrive.
        std::unique_lock<std::mutex> lock(state.mutex);
        state.cv.wait(lock, [&] {
            return state.completed.load(std::memory_order_acquire) >= level.size()
                || job->token.isCancelled();
        });
    }

    activeWorkers_.fetch_sub(1, std::memory_order_acq_rel);

    // The cook is over once every worker has left it.
    const size_t workersDone = job->workersDone.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (workersDone != threads_.size()) return;

    const CookGeneration generation = job->generation;
    const CookStatus status =
        job->token.isCancelled() ? CookStatus::Cancelled : CookStatus::Success;

    CookFinishedFn cookFinishedFn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Only the cook that is still wanted clears the slot. A cook that was
        // replaced has already been given up on.
        if (job_ && job_->generation == generation) hasJob_ = false;
        cookFinishedFn = cookFinishedFn_;
    }

    if (cookFinishedFn) cookFinishedFn(generation, status);
}

} // namespace enzo::async
