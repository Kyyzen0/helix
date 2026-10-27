#include "Engine/Expression/ExpressionEngine.h"
#include "Engine/Daslang/Runtime.h"

namespace helix::expr {

namespace {
constexpr const char* evalFunctionName = "helixEvalExpression";

// Wraps a bare expression in an exported function that returns the given type,
// so daslang has a function to compile and call.
String wrapExpression(const String& expression, const String& returnType)
{
    return "options gen2\n"
           "require math\n"
           "require helix_expression\n"
           "[export]\n"
           "def " +
           String(evalFunctionName) + " : " + returnType +
           " {\n"
           "    return " +
           returnType + "(" + expression +
           ")\n"
           "}\n";
}
} // namespace

ExpressionEngine& ExpressionEngine::instance()
{
    static ExpressionEngine engine;
    return engine;
}

ExpressionEngine::ThreadCache& ExpressionEngine::threadCache_()
{
    // The engine is a singleton, so one pointer per thread is enough.
    thread_local ThreadCache* cache = nullptr;
    if (cache) return *cache;

    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = threadCaches_[std::this_thread::get_id()];
    if (!slot) slot = std::make_unique<ThreadCache>();
    cache = slot.get();
    return *cache;
}

daslang::CompiledProgram*
ExpressionEngine::program_(ReturnType type, const String& expression, String& error)
{
    ProgramMap& local = threadCache_().programs[size_t(type)];
    auto cached = local.find(expression);
    if (cached != local.end()) return cached->second.get();

    // A miss compiles once for every thread, then clones for this one. Compiling
    // and cloning stay under the lock because daslang is not thread safe there.
    std::shared_ptr<daslang::CompiledProgram> clone;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ProgramMap& masters = masters_[size_t(type)];
        auto master = masters.find(expression);
        if (master == masters.end())
        {
            static constexpr const char* returnTypes[] = {"float", "int64", "string"};
            auto compiled = daslang::Runtime::instance()
                                .compile("expression", wrapExpression(expression, returnTypes[size_t(type)]), error);
            if (!compiled) return nullptr;
            master = masters.emplace(expression, std::move(compiled)).first;
        }
        clone = master->second->clone();
    }
    if (!clone)
    {
        error = "Failed to initialise the expression context";
        return nullptr;
    }
    return local.emplace(expression, std::move(clone)).first->second.get();
}

bool ExpressionEngine::evalFloat(
    const String& expression,
    const ExpressionContext* context,
    floatT& result,
    String& error
)
{
    auto* script = program_(ReturnType::Float, expression, error);
    if (!script) return false;
    return script->evalFloat(evalFunctionName, context, result, error);
}

bool ExpressionEngine::evalInt(
    const String& expression,
    const ExpressionContext* context,
    intT& result,
    String& error
)
{
    auto* script = program_(ReturnType::Int, expression, error);
    if (!script) return false;
    return script->evalInt(evalFunctionName, context, result, error);
}

bool ExpressionEngine::evalString(
    const String& expression,
    const ExpressionContext* context,
    String& result,
    String& error
)
{
    auto* script = program_(ReturnType::String, expression, error);
    if (!script) return false;
    return script->evalString(evalFunctionName, context, result, error);
}

} // namespace helix::expr
