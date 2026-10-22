// Counts the allocations a process makes.
//
// The file is compiled into a small library that is loaded next to the
// benchmark, so the counts cover the engine as well, which is where a cook
// spends its memory:
//
//   clang -dynamiclib -O2 -o libhelix_alloccount.dylib tests/AllocCounter.c
//   DYLD_INSERT_LIBRARIES=./libhelix_alloccount.dylib ./bench "Perf: ..."
//
// The benchmark reads the counters through dlsym, so it runs unchanged when
// the library is not loaded.
//
// The C allocator is interposed, so every allocation of the process is seen,
// including the ones the C++ runtime makes.

#include <dlfcn.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The counters the benchmark reads.
static _Atomic unsigned long long g_allocations = 0;
static _Atomic unsigned long long g_frees = 0;
static _Atomic unsigned long long g_bytes = 0;

static void* (*real_malloc)(size_t);
static void* (*real_calloc)(size_t, size_t);
static void* (*real_realloc)(void*, size_t);
static void (*real_free)(void*);
static int (*real_posix_memalign)(void**, size_t, size_t);

// The first allocations happen before the real allocator is known, because
// finding it needs memory of its own. They are served from a block that is
// already there.
#define BOOTSTRAP_SIZE 65536
static char g_bootstrap[BOOTSTRAP_SIZE];
static size_t g_bootstrapUsed = 0;
static _Thread_local int g_resolving = 0;

// Returns whether the pointer came from the bootstrap block.
static int isBootstrapPointer(const void* pointer)
{
    const char* start = g_bootstrap;
    const char* end = g_bootstrap + BOOTSTRAP_SIZE;
    return pointer >= start && pointer < end;
}

// Returns the size of a bootstrap allocation.
static size_t bootstrapSize(const void* pointer)
{
    if (!isBootstrapPointer(pointer)) return 0;
    size_t size = 0;
    memcpy(&size, static_cast<const char*>(pointer) - sizeof(size_t), sizeof(size_t));
    return size;
}

// Serves an allocation from the bootstrap block.
static void* bootstrapAllocate(size_t size)
{
    const size_t aligned = (size + 15) & ~((size_t)15);
    if (g_bootstrapUsed + aligned + sizeof(size_t) > BOOTSTRAP_SIZE) return NULL;
    char* block = g_bootstrap + g_bootstrapUsed;
    g_bootstrapUsed += aligned + sizeof(size_t);
    memcpy(block, &size, sizeof(size_t));
    return block + sizeof(size_t);
}

// Finds the real allocator. The guard keeps the search from running again
// when finding it allocates.
static void resolveAllocator(void)
{
    if (real_malloc) return;
    if (g_resolving) return;
    g_resolving = 1;
    real_malloc = (void* (*)(size_t))dlsym(RTLD_NEXT, "malloc");
    real_calloc = (void* (*)(size_t, size_t))dlsym(RTLD_NEXT, "calloc");
    real_realloc = (void* (*)(void*, size_t))dlsym(RTLD_NEXT, "realloc");
    real_free = (void (*)(void*))dlsym(RTLD_NEXT, "free");
    real_posix_memalign = (int (*)(void*, size_t, size_t))dlsym(RTLD_NEXT, "posix_memalign");
    g_resolving = 0;
}

static void* countedMalloc(size_t size)
{
    if (!real_malloc) {
        if (g_resolving) return bootstrapAllocate(size);
        resolveAllocator();
        if (!real_malloc) return bootstrapAllocate(size);
    }
    atomic_fetch_add_explicit(&g_allocations, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_bytes, (unsigned long long)size, memory_order_relaxed);
    return real_malloc(size);
}

static void* countedCalloc(size_t count, size_t size)
{
    if (!real_calloc) {
        if (g_resolving) {
            void* block = bootstrapAllocate(count * size);
            if (block) memset(block, 0, count * size);
            return block;
        }
        resolveAllocator();
        if (!real_calloc) {
            void* block = bootstrapAllocate(count * size);
            if (block) memset(block, 0, count * size);
            return block;
        }
    }
    atomic_fetch_add_explicit(&g_allocations, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(
        &g_bytes,
        (unsigned long long)count * (unsigned long long)size,
        memory_order_relaxed
    );
    return real_calloc(count, size);
}

static void countedFree(void* pointer)
{
    if (!pointer) return;
    if (isBootstrapPointer(pointer)) return;
    atomic_fetch_add_explicit(&g_frees, 1, memory_order_relaxed);
    if (!real_free) {
        resolveAllocator();
        if (!real_free) return;
    }
    real_free(pointer);
}

static void* countedRealloc(void* pointer, size_t size)
{
    if (!pointer) return countedMalloc(size);
    if (isBootstrapPointer(pointer)) {
        const size_t oldSize = bootstrapSize(pointer);
        void* block = countedMalloc(size);
        if (block && oldSize) memcpy(block, pointer, oldSize < size ? oldSize : size);
        return block;
    }
    if (!real_realloc) {
        if (g_resolving) return NULL;
        resolveAllocator();
        if (!real_realloc) return NULL;
    }
    atomic_fetch_add_explicit(&g_allocations, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_bytes, (unsigned long long)size, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_frees, 1, memory_order_relaxed);
    return real_realloc(pointer, size);
}

static int countedPosixMemalign(void** pointer, size_t alignment, size_t size)
{
    if (!real_posix_memalign) {
        if (g_resolving) return -1;
        resolveAllocator();
        if (!real_posix_memalign) return -1;
    }
    atomic_fetch_add_explicit(&g_allocations, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_bytes, (unsigned long long)size, memory_order_relaxed);
    return real_posix_memalign(pointer, alignment, size);
}

/// @brief Returns the number of allocations the process has made.
unsigned long long helix_allocation_count(void)
{
    return atomic_load_explicit(&g_allocations, memory_order_relaxed);
}

/// @brief Returns the number of frees the process has made.
unsigned long long helix_free_count(void)
{
    return atomic_load_explicit(&g_frees, memory_order_relaxed);
}

/// @brief Returns the number of bytes the process has asked for.
unsigned long long helix_allocated_bytes(void)
{
    return atomic_load_explicit(&g_bytes, memory_order_relaxed);
}

// The interpose section tells the loader to send every call to the real
// functions to the counted ones.
typedef struct
{
    const void* replacement;
    const void* replacee;
} InterposeEntry;

__attribute__((used)) static const InterposeEntry interposeMalloc
    __attribute__((section("__DATA,__interpose"))) = {(const void*)countedMalloc,
                                                       (const void*)malloc};
__attribute__((used)) static const InterposeEntry interposeCalloc
    __attribute__((section("__DATA,__interpose"))) = {(const void*)countedCalloc,
                                                       (const void*)calloc};
__attribute__((used)) static const InterposeEntry interposeFree
    __attribute__((section("__DATA,__interpose"))) = {(const void*)countedFree,
                                                       (const void*)free};
__attribute__((used)) static const InterposeEntry interposeRealloc
    __attribute__((section("__DATA,__interpose"))) = {(const void*)countedRealloc,
                                                       (const void*)realloc};
__attribute__((used)) static const InterposeEntry interposePosixMemalign
    __attribute__((section("__DATA,__interpose"))) = {(const void*)countedPosixMemalign,
                                                       (const void*)posix_memalign};
