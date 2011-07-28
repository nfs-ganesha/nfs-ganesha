#if defined(__x86_64) || defined(__amd64)
#define FI64 "%ld"
#define FU64 "%lu"
#define FX64 "%lx"
#define FX64_UP "%lX"
#else
#define FI64 "%lld"
#define FU64 "%llu"
#define FX64 "%llx"
#define FX64_UP "%llX"
#endif

