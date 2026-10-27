#pragma once
#include "Engine/Core/Types.h"
#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace helix::daslang {
class CompiledProgram;
}

namespace helix::expr {

class ExpressionContext;

/**
 * @brief Evaluator for parameter expressions written in daslang.
 *
 * A bare expression such as "5 + 5" is wrapped in a function, compiled through
 * daslang::Runtime, and run to produce a parameter value.
 *
 * @note Compiled expressions are cached so repeated evaluation during cooking
 * stays cheap. The engine is safe to call from several threads at once, e.g.
 * cook workers and the interface: a daslang context runs one function at a time,
 * so every thread evaluates on its own clone of the compiled program, and only
 * compiling and cloning (a cache miss) take the engine's lock.
 */
class ExpressionEngine
{
  public:
    static ExpressionEngine& instance();

    /// @brief Evaluates an expression and returns its result as a float.
    /// @param context The world the expression reads, e.g. for prm(), may be null.
    /// @return True on success, false when the expression fails to compile or run.
    bool evalFloat(
        const String& expression,
        const ExpressionContext* context,
        floatT& result,
        String& error
    );

    /// @brief Evaluates an expression and returns its result as an integer.
    /// @param context The world the expression reads, e.g. for prm(), may be null.
    /// @return True on success, false when the expression fails to compile or run.
    bool evalInt(
        const String& expression,
        const ExpressionContext* context,
        intT& result,
        String& error
    );

    /// @brief Evaluates an expression and returns its result as a string.
    /// @param context The world the expression reads, e.g. for prm(), may be null.
    /// @return True on success, false when the expression fails to compile or run.
    bool evalString(
        const String& expression,
        const ExpressionContext* context,
        String& result,
        String& error
    );

    ExpressionEngine(const ExpressionEngine&) = delete;
    ExpressionEngine& operator=(const ExpressionEngine&) = delete;

  private:
    ExpressionEngine() = default;

    enum class ReturnType : size_t
    {
        Float,
        Int,
        String,
        Count
    };

    using ProgramMap = std::unordered_map<String, std::shared_ptr<daslang::CompiledProgram>>;
    using ProgramMaps = std::array<ProgramMap, size_t(ReturnType::Count)>;

    /// @brief Programs one thread evaluates on, only ever touched by that thread.
    struct ThreadCache
    {
        ProgramMaps programs;
    };

    /// @brief Returns the calling thread's own cache, registering it on first use.
    ThreadCache& threadCache_();

    /// @brief Returns the calling thread's clone of the program for an expression.
    /// @return The program, or null with @p error set when it fails to compile.
    /// @note A hit costs one lookup keyed by the raw expression, with no lock and
    /// without rebuilding the wrapped source.
    daslang::CompiledProgram* program_(ReturnType type, const String& expression, String& error);

    // Guards the masters and the registry of thread caches. Evaluation never holds it.
    std::mutex mutex_;
    // One compiled program per expression and return type, cloned for each thread.
    ProgramMaps masters_;
    // Owns the thread caches, so no clone is destroyed while another thread runs daslang.
    std::unordered_map<std::thread::id, std::unique_ptr<ThreadCache>> threadCaches_;
};

} // namespace helix::expr
