# Task 017B: FileCache Logging and Exception Stack — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> - **Disposition:** reviewed_executable — implementation_authorized
> - **Task ID:** 017B
> - **Binding design:** `port/design/filecache-task-017b-logging-exception-stack.md`
> - **Prerequisite:** Review 5 accepted, Task 018 four-driver addendum accepted
>   [DONE], Task 018R vcpkg `Arrow` repair accepted [DONE], TPCH
>   `BufferedInput` performance investigation/corrective accepted [PENDING]
> - **Successor:** Task 019 implementation begins after 017B acceptance
> - **Execution order:** 017A → 018 → Review 5 → 018R → TPCH performance
>   investigation → **017B** → 019
> - **Environment:** `root-oss` (`/root/oss/velox`, branch `filecache`, head `0c5b5918eb`)
> - **Commit policy:** Worker never stages, commits, or pushes. Controller commits after independent review.
> - **Implementation status:** AUTHORIZED BUT PAUSED. The plan was reviewed and
>   authorized on 2026-07-24. On 2026-07-25 the user placed the TPCH
>   `BufferedInput` performance investigation before implementation. Resume
>   only after that investigation and any required corrective are accepted.
> - **implementation_authorized:** true

**Goal:** Replace the no-op `logger_useful.h` shim (Task 003) with real lazy
logging through glog, real exception formatting, and real exception logging —
while preserving the existing `FileCacheLogger`/`LoggerPtr`/`getLogger` shape,
zero evaluation for every filtered log record, zero `shared_ptr` ownership copy,
and a `noexcept` exception-logging guarantee with emergency `stderr` fallback.

**Architecture:** Keep `FileCacheLogger` as an immutable name-holder. Use glog as
the only backend. `LOG_TEST`/`TRACE`/`DEBUG` map to `VLOG(3/2/1)` with a
three-gate filter (compile-strip, `FLAGS_minloglevel`, `VLOG_IS_ON`).
`LOG_INFO`/`WARNING`/`ERROR` map to native glog `LOG(INFO/WARNING/ERROR)` with a
two-gate filter (compile-strip, `FLAGS_minloglevel`). No call-site rewrite, no
new backend abstraction. Exception formatting captures `std::current_exception`
once (never bare `throw`). `tryLogCurrentException` emits via an internal
function-pointer emitter (default: `LOG(ERROR)`), with a fixed allocation-free
`stderr` emergency path when everything else fails. The emitter pointer is the
sole test-injection seam — it is not a `google::LogSink` and is not a production
backend.

**Tech Stack:** C++20 (Velox baseline), `glog::glog`, `fmt::fmt`, Velox
`velox_exception` + `velox_process`, GoogleTest, CMake via `velox_add_library` +
ninja.

## Global Constraints

1. Worker never stages, commits, or pushes. Controller commits after independent review.
2. Implementation IS authorized. Controller authorized on 2026-07-24 after independent plan
   re-review (0 Critical, 0 Important). Authorization receipt:
   `port/task/fullreview/root-oss/5/017b-implementation-plan-review.md`.
   Worker no-commit and result-handoff rules in Constraint #1 and §10 remain in full effect.
3. Allman-style braces in all C++ code.
4. No C++ sleeps; no `ninja -j`; no `nproc`.
5. Every build/test command redirects output to a unique `.log` file inside the build directory.
6. A `task` subagent analyzes each log and returns only a concise summary; the Worker never pastes full build/test logs inline.
7. `LOG_TEST` is `VLOG(3)`, NOT a permanent no-op. It evaluates when `--v=3` and is zero-cost only at default verbosity.
8. `getCurrentExceptionMessage` is `noexcept`; safe outside catch (returns empty); never uses bare `throw`.
9. `tryLogCurrentException` is `noexcept`; never replaces the original exception.
10. `##__VA_ARGS__` for zero-varargs portability (GCC/Clang, required by Velox).
11. `auto&&` for logger binding — no `shared_ptr` ownership copy.
12. No Poco/raw logger, level argument, `std::string` name overload, `markAsLogged`, metrics, cancellation, Gluten, benchmark scope.
13. No ClickHouse `system.text_log` structured-argument claims.
14. No stacked PRs; no implementation beyond Task 017B scope.
15. Use "exception" not "crash" for logical failures.

---

## Acceptance Checklist

- [ ] All Milestone gates GREEN
- [ ] Mutation evidence RED for every MUT-01 through MUT-09
- [ ] Accumulated CTest gate: zero failures across all `velox_ch_*` targets (mono + non-mono)
- [ ] Worker never commits; result receipt written with `worker_status: ready_for_controller`

---

## 1. Authoritative Sources

| Source | Path | Purpose |
|--------|------|---------|
| Binding design | `port/design/filecache-task-017b-logging-exception-stack.md` | Approved specification |
| Task index | `port/task/017b-filecache-logging-exception-stack.md` | Task status and scope |
| Current shim | `velox/ch/Common/logger_useful.h` | No-op shim (Task 003), lines 1–72 |
| VeloxException | `velox/common/base/VeloxException.h:140,205,220,232,236,270` | `stackTrace`, `message`, `errorCode`, `errorSource`, `State::stackTrace` |
| StackTrace | `velox/common/process/StackTrace.h:29–46` | `const std::string& toString() const` (lazy-init) |
| Stack flags | `velox/common/base/VeloxException.h:33–34` | `FLAGS_velox_exception_system_stacktrace_enabled/user` |
| glog LogSink pattern | `velox/common/time/tests/HierarchicalTimerTest.cpp:32–62` | Verified `google::LogSink` / `AddLogSink` / `RemoveLogSink` capture pattern |
| Library CMake | `velox/ch/Common/CMakeLists.txt:74–100` | `velox_add_library` positional source list |
| Non-mono link | `velox/ch/Common/CMakeLists.txt:108–123` | `if(NOT VELOX_MONO_LIBRARY)` link block |
| Test CMake | `velox/ch/Common/tests/CMakeLists.txt:15–28` | `velox_ch_common_test` target |
| Existing tests | `velox/ch/Common/tests/BasicShimsTest.cpp:58–109` | Tests to preserve/replace |
| velox_add_library | `CMake/VeloxUtils.cmake:91–213` | Mono: `target_sources(velox PRIVATE ...)` / Non-mono: standalone lib |
| FileCache callers | `velox/ch/Interpreters/FileCache/FileCache.cpp`, `FileSegment.cpp`, `Metadata.cpp`, `EvictionCandidates.cpp` | `tryLogCurrentException(__PRETTY_FUNCTION__)`, `tryLogCurrentException(log, "msg")`, `tryLogCurrentException(log)`. These are the actual `tryLogCurrentException` callers and are in `velox_ch_filecache_core` (rebuilt at M3-C1). |
| Getter-only call sites | `velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp`, `velox/ch/Common/FileCacheScheduler.cpp` | These call `getCurrentExceptionMessage` only; they are not `tryLogCurrentException` callers. `FileCacheScheduler.cpp` is part of `velox_ch_filecache` (rebuilt at M1-G1). |

---

## 2. File Scope

### New files

| File | Purpose |
|------|---------|
| `velox/ch/Common/logger_useful.cpp` | Non-template function definitions: `detail::formatExceptionPtr`, `getCurrentExceptionMessage`, `tryLogCurrentException` overloads, `detail::defaultExceptionEmitter`, `detail::testExceptionEmitter` |
| `velox/ch/Common/tests/CompileStripProbe.cpp` | Separate executable compiled with `GOOGLE_STRIP_LOG=1`; proves stripped levels evaluate no expressions |

### Modified files

| File | Change summary |
|------|----------------|
| `velox/ch/Common/logger_useful.h` | Add `#include <fmt/format.h>`, `#include <glog/logging.h>`; keep `FileCacheLogger`/`LoggerPtr`/`getLogger` unchanged; add `detail::chLogFormat` helper and `detail::ExceptionEmitter`/`testExceptionEmitter`/`defaultExceptionEmitter` declarations; replace macro section with real three-gate/two-gate macros; replace `getCurrentExceptionMessage` and `tryLogCurrentException` declarations (remove old inline stubs) |
| `velox/ch/Common/tests/BasicShimsTest.cpp` | Replace `AllLogMacrosDoNotEvaluateArguments` (line 65), `CurrentExceptionMessageRemainsEmptyFirstPhase` (line 85), `TryLogCurrentExceptionIsNoOpFirstPhase` (line 98) with real-behavior tests; add new test cases; add `TestLogSink` and `EmitterGuard` helpers |
| `velox/ch/Common/CMakeLists.txt` | Add `logger_useful.cpp` to `velox_add_library` source list; add `velox_process` and `glog::glog` to non-mono link block |
| `velox/ch/Common/tests/CMakeLists.txt` | Add `glog::glog` to `velox_ch_common_test` link; add `velox_ch_compile_strip_probe` executable |

---

## 3. Logging-Level Mapping

| Compatibility macro | glog backend | Compile-strip gate | Runtime gate | Default state |
|---|---|---|---|---|
| `LOG_TEST` | `VLOG(3)` | `GOOGLE_STRIP_LOG <= 0` | `0 >= FLAGS_minloglevel && VLOG_IS_ON(3)` | disabled |
| `LOG_TRACE` | `VLOG(2)` | `GOOGLE_STRIP_LOG <= 0` | `0 >= FLAGS_minloglevel && VLOG_IS_ON(2)` | disabled |
| `LOG_DEBUG` | `VLOG(1)` | `GOOGLE_STRIP_LOG <= 0` | `0 >= FLAGS_minloglevel && VLOG_IS_ON(1)` | disabled |
| `LOG_INFO` | `LOG(INFO)` | `GOOGLE_STRIP_LOG <= 0` | `0 >= FLAGS_minloglevel` | enabled |
| `LOG_WARNING` | `LOG(WARNING)` | `GOOGLE_STRIP_LOG <= 1` | `1 >= FLAGS_minloglevel` | enabled |
| `LOG_ERROR` | `LOG(ERROR)` | `GOOGLE_STRIP_LOG <= 2` | `2 >= FLAGS_minloglevel` | enabled |

glog severity constants: `INFO=0`, `WARNING=1`, `ERROR=2`, `FATAL=3`.
`GOOGLE_STRIP_LOG` defaults to 0 (nothing stripped). `FLAGS_minloglevel` defaults to 0 (INFO).
`VLOG` uses `INFO` (0) as its underlying severity: stripped when `GOOGLE_STRIP_LOG > 0`, suppressed when `FLAGS_minloglevel > 0`.

---

## 4. Milestones

### Milestone 1: Exception Formatting & CMake Infrastructure

**Goal:** Create `logger_useful.cpp`, register it in CMake, implement
`getCurrentExceptionMessage` with TDD, and prove mutations RED.

**Files:**
- Create: `velox/ch/Common/logger_useful.cpp`
- Modify: `velox/ch/Common/logger_useful.h` (exception declarations)
- Modify: `velox/ch/Common/CMakeLists.txt` (source + link)
- Modify: `velox/ch/Common/tests/CMakeLists.txt` (glog link)
- Modify: `velox/ch/Common/tests/BasicShimsTest.cpp` (exception tests)

**Interfaces:**
- Produces: `getCurrentExceptionMessage(bool withStackTrace = false) noexcept -> std::string`
- Produces: `detail::formatExceptionPtr(std::exception_ptr, bool) -> std::string` (internal)
- Consumes: `velox::VeloxException::stackTrace()`, `::message()`, `::errorSource()`, `::errorCode()`
- Consumes: `velox::process::StackTrace::toString()`

#### 4.1 Header declarations (in `logger_useful.h`)

Remove the existing inline stubs for `getCurrentExceptionMessage` and
`tryLogCurrentException` (lines 53–61) and replace with declarations only.
The `tryLogCurrentException` declarations are placed here for compilation but
implemented in Milestone 3.

Add after the existing `#include <utility>` (line 21):

```cpp
#include <fmt/format.h>
#include <glog/logging.h>
```

Replace the two inline stubs (lines 53–61) with:

```cpp
/// Formats the currently-handled exception. Safe outside catch (returns empty).
/// Captures std::current_exception exactly once; never uses bare throw.
std::string getCurrentExceptionMessage(bool withStackTrace = false) noexcept;

/// Log the current exception through a LoggerPtr. Noexcept, never replaces
/// the original exception being handled.
void tryLogCurrentException(
    const LoggerPtr & logger,
    const std::string & context = {}) noexcept;

/// Log the current exception using a function/log name.
void tryLogCurrentException(
    const char * logName,
    const std::string & context = {}) noexcept;

namespace detail
{

/// Internal emitter function pointer for exception logging.
/// Default: glog LOG(ERROR) with attribution.
/// Tests may inject a different function via testExceptionEmitter().
/// This is NOT a google::LogSink and NOT a production backend.
using ExceptionEmitter = void (*)(const char * attribution, const char * message);

/// Returns a reference to the currently installed exception emitter.
ExceptionEmitter & testExceptionEmitter();

/// Default emitter: writes to glog LOG(ERROR) with [attribution] prefix.
void defaultExceptionEmitter(const char * attribution, const char * message);

} // namespace detail
```

#### 4.2 Implementation (`logger_useful.cpp`)

```cpp
#include "velox/ch/Common/logger_useful.h"

#include <exception>
#include <string>

#include <unistd.h>

#include <fmt/format.h>
#include <glog/logging.h>

#include "velox/common/base/VeloxException.h"
#include "velox/common/process/StackTrace.h"

namespace facebook::velox::ch
{

namespace detail
{

/// Internal formatter: accepts an exception_ptr and inspects its type
/// via std::rethrow_exception (not bare throw). Not noexcept: callers
/// must contain any failure from string construction.
std::string formatExceptionPtr(std::exception_ptr eptr, bool withStackTrace)
{
    try
    {
        std::rethrow_exception(eptr);
    }
    catch (const velox::VeloxException & e)
    {
        try
        {
            std::string msg = fmt::format(
                "VeloxException: [{}] [{}] {}",
                e.errorSource(),
                e.errorCode(),
                e.message());
            if (withStackTrace)
            {
                if (const auto * st = e.stackTrace())
                {
                    const std::string & trace = st->toString();
                    if (!trace.empty())
                    {
                        msg += "\nStack trace:\n";
                        msg += trace;
                    }
                }
            }
            return msg;
        }
        catch (...)
        {
            return "VeloxException (formatting failed)";
        }
    }
    catch (const std::exception & e)
    {
        try
        {
            return fmt::format("std::exception: {}", e.what());
        }
        catch (...)
        {
            return "std::exception (formatting failed)";
        }
    }
    catch (...)
    {
        return "Unknown exception";
    }
}

void defaultExceptionEmitter(const char * attribution, const char * message)
{
    LOG(ERROR) << "[" << (attribution ? attribution : "null") << "] " << message;
}

ExceptionEmitter & testExceptionEmitter()
{
    static ExceptionEmitter emitter = defaultExceptionEmitter;
    return emitter;
}

} // namespace detail

std::string getCurrentExceptionMessage(bool withStackTrace) noexcept
{
    try
    {
        auto eptr = std::current_exception();
        if (!eptr)
        {
            return {};
        }
        return detail::formatExceptionPtr(eptr, withStackTrace);
    }
    catch (...)
    {
        // Catastrophic: even the fixed fallback string could not be
        // represented (e.g. OOM during small-string construction).
        // Empty return is reserved for this case per the binding spec.
        return {};
    }
}

void tryLogCurrentException(
    const LoggerPtr & logger,
    const std::string & context) noexcept
{
    try
    {
        const char * attribution =
            logger ? logger->name().c_str() : "null";

        auto eptr = std::current_exception();
        if (!eptr)
        {
            detail::testExceptionEmitter()(
                attribution,
                "tryLogCurrentException called with no active exception");
            return;
        }

        std::string msg = detail::formatExceptionPtr(eptr, /*withStackTrace=*/true);
        if (!context.empty())
        {
            msg = context + ": " + msg;
        }

        detail::testExceptionEmitter()(attribution, msg.c_str());
    }
    catch (...)
    {
        static constexpr char kEmergency[] =
            "tryLogCurrentException: emergency — logging failure, "
            "original exception may be lost\n";
        [[maybe_unused]] auto rc =
            ::write(STDERR_FILENO, kEmergency, sizeof(kEmergency) - 1);
    }
}

void tryLogCurrentException(
    const char * logName,
    const std::string & context) noexcept
{
    try
    {
        const char * attribution = logName ? logName : "null";

        auto eptr = std::current_exception();
        if (!eptr)
        {
            detail::testExceptionEmitter()(
                attribution,
                "tryLogCurrentException called with no active exception");
            return;
        }

        std::string msg = detail::formatExceptionPtr(eptr, /*withStackTrace=*/true);
        if (!context.empty())
        {
            msg = context + ": " + msg;
        }

        detail::testExceptionEmitter()(attribution, msg.c_str());
    }
    catch (...)
    {
        static constexpr char kEmergency[] =
            "tryLogCurrentException: emergency — logging failure, "
            "original exception may be lost\n";
        [[maybe_unused]] auto rc =
            ::write(STDERR_FILENO, kEmergency, sizeof(kEmergency) - 1);
    }
}

} // namespace facebook::velox::ch
```

#### 4.3 CMake changes

**`velox/ch/Common/CMakeLists.txt`** — add `logger_useful.cpp` to the source
list (works for both mono and non-mono via `velox_add_library`):

```cmake
velox_add_library(
  velox_ch_filecache
  CurrentMetrics.cpp
  ProfileEvents.cpp
  FileCacheStats.cpp
  StatusFile.cpp
  ThreadPool.cpp
  FileCacheQueryIdScope.cpp
  FileCacheScheduler.cpp
  SipHash128.cpp
  logger_useful.cpp
  HEADERS
    ClickHouseAliases.h
    ClickHouseAssert.h
    CurrentMetrics.h
    FailPoint.h
    FileCacheBoundedQueue.h
    FileCacheException.h
    FileCacheFilesystem.h
    FileCacheQueryIdScope.h
    FileCacheScheduler.h
    FileCacheStats.h
    FilesystemCacheLog.h
    logger_useful.h
    OpenTelemetryTraceContext.h
    ProfileEvents.h
    QueryStatus.h
    SharedMutex.h
    SipHash128.h
    StatusFile.h
    ThreadPool.h
)
```

Extend the non-mono link block with `velox_process` (for `StackTrace::toString`)
and `glog::glog` (for `LOG(ERROR)` in emitter, `VLOG`/`VLOG_IS_ON` in macros):

```cmake
if(NOT VELOX_MONO_LIBRARY)
  target_link_libraries(
    velox_ch_filecache
    PUBLIC
      velox_common_config
      velox_exception
      velox_process
      Folly::folly
      fmt::fmt
      glog::glog
  )
endif()
```

**`velox/ch/Common/tests/CMakeLists.txt`** — add `glog::glog` to the
`velox_ch_common_test` link list:

```cmake
target_link_libraries(
  velox_ch_common_test
  PRIVATE
    velox_ch_filecache
    velox_test_util
    velox_exception
    Folly::folly
    fmt::fmt
    glog::glog
    GTest::gtest
    GTest::gtest_main
)
```

#### 4.4 TDD Steps — Exception Formatting

- [ ] **M1-T1** Write `ExceptionFormattingTest.VeloxExceptionWithStack` in
  `BasicShimsTest.cpp`:

```cpp
TEST(ExceptionFormattingTest, VeloxExceptionWithStack)
{
    // Ensure stack capture is enabled for this test.
    const auto saved = FLAGS_velox_exception_system_stacktrace_enabled;
    FLAGS_velox_exception_system_stacktrace_enabled = true;
    auto restore = folly::makeGuard(
        [&] { FLAGS_velox_exception_system_stacktrace_enabled = saved; });

    try
    {
        VELOX_FAIL("test error");
    }
    catch (...)
    {
        auto msg = getCurrentExceptionMessage(/*withStackTrace=*/true);
        EXPECT_NE(msg.find("VeloxException"), std::string::npos);
        EXPECT_NE(msg.find("RUNTIME"), std::string::npos);
        EXPECT_NE(msg.find("test error"), std::string::npos);
        EXPECT_NE(msg.find("Stack trace"), std::string::npos);
    }
}
```

- [ ] **M1-T2** Write `ExceptionFormattingTest.VeloxExceptionWithoutStack`:

```cpp
TEST(ExceptionFormattingTest, VeloxExceptionWithoutStack)
{
    try
    {
        VELOX_FAIL("test error");
    }
    catch (...)
    {
        auto msg = getCurrentExceptionMessage(/*withStackTrace=*/false);
        EXPECT_NE(msg.find("VeloxException"), std::string::npos);
        EXPECT_NE(msg.find("test error"), std::string::npos);
        EXPECT_EQ(msg.find("Stack trace"), std::string::npos)
            << "withStackTrace=false must suppress stack. Got: " << msg;
    }
}
```

- [ ] **M1-T3** Write `ExceptionFormattingTest.StdException`:

```cpp
TEST(ExceptionFormattingTest, StdException)
{
    try
    {
        throw std::runtime_error("std boom");
    }
    catch (...)
    {
        auto msg = getCurrentExceptionMessage(true);
        EXPECT_NE(msg.find("std::exception"), std::string::npos);
        EXPECT_NE(msg.find("std boom"), std::string::npos);
    }
}
```

- [ ] **M1-T4** Write `ExceptionFormattingTest.UnknownException`:

```cpp
TEST(ExceptionFormattingTest, UnknownException)
{
    try
    {
        throw 42;
    }
    catch (...)
    {
        auto msg = getCurrentExceptionMessage(true);
        EXPECT_NE(msg.find("Unknown exception"), std::string::npos);
    }
}
```

- [ ] **M1-T5** Write `ExceptionFormattingDeathTest.NoActiveExceptionDoesNotTerminate`:

```cpp
TEST(ExceptionFormattingDeathTest, NoActiveExceptionDoesNotTerminate)
{
    // Runs in a subprocess (EXPECT_EXIT death-test boundary) so that a mutation
    // that both removes the null guard and replaces std::rethrow_exception(eptr)
    // with bare throw; terminates only the child and makes the
    // ExitedWithCode(0) assertion RED.  In production the null guard returns {}
    // and the subprocess exits 0 normally.
    EXPECT_EXIT(
    {
        auto msg = getCurrentExceptionMessage(true);
        if (!msg.empty())
        {
            std::cerr << "Expected empty result outside catch, got non-empty\n";
            _exit(1);
        }
        _exit(0);
    },
    ::testing::ExitedWithCode(0),
    "");
}
```

- [ ] **M1-T6** Write `ExceptionFormattingTest.StackRespectedWhenDisabled`:

```cpp
TEST(ExceptionFormattingTest, StackRespectedWhenDisabled)
{
    // Disable stack capture at throw-time, then request stack at format-time.
    // The formatter must not collect a new stack; it only uses the existing one.
    const auto saved = FLAGS_velox_exception_system_stacktrace_enabled;
    FLAGS_velox_exception_system_stacktrace_enabled = false;
    auto restore = folly::makeGuard(
        [&] { FLAGS_velox_exception_system_stacktrace_enabled = saved; });

    try
    {
        VELOX_FAIL("no stack error");
    }
    catch (...)
    {
        auto msg = getCurrentExceptionMessage(/*withStackTrace=*/true);
        EXPECT_NE(msg.find("VeloxException"), std::string::npos);
        EXPECT_NE(msg.find("no stack error"), std::string::npos);
        // Stack was not captured at throw-time, so even with withStackTrace=true
        // there is no stack trace in the output.
        EXPECT_EQ(msg.find("Stack trace"), std::string::npos)
            << "Stack must not appear when throw-time capture was disabled. Got: " << msg;
    }
}
```

- [ ] **M1-DEL** Delete the three stale first-phase tests from
  `BasicShimsTest.cpp`:
  - `AllLogMacrosDoNotEvaluateArguments` (line 65) — contradicts new semantics
    where `LOG_WARNING`/`LOG_ERROR` evaluate, and `LOG_TEST` evaluates when
    `--v=3`.
  - `CurrentExceptionMessageRemainsEmptyFirstPhase` (line 85) — contradicts real
    formatting.
  - `TryLogCurrentExceptionIsNoOpFirstPhase` (line 98) — contradicts real
    logging.

  Preserve `ArgumentsAreNotEvaluated` (line 58) and
  `GetLoggerReturnsNonNullWithNameIdentity` (line 78) unchanged.

- [ ] **M1-I1** Create `logger_useful.cpp` with the body from §4.2.

- [ ] **M1-I2** Apply CMake changes from §4.3.

- [ ] **M1-I3** Add required includes to `BasicShimsTest.cpp`:

```cpp
#include <unistd.h>

#include <folly/ScopeGuard.h>
#include <glog/logging.h>

#include "velox/common/base/VeloxException.h"
```

- [ ] **M1-G1** Build and run (mono):

```bash
cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  ninja -C _build/debug velox_ch_common_test \
    > _build/debug/build_017b_m1.log 2>&1
```

```bash
source /root/oss/velox-helper/env.sh && \
  cd /root/oss/velox/_build/debug && \
  ctest -R velox_ch_common_test --output-on-failure \
    > ctest_017b_m1.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug/build_017b_m1.log` and `_build/debug/ctest_017b_m1.log` and
return only a concise summary. Do not read the logs inline.

**GREEN gate:** All `ExceptionFormattingTest` cases and
`ExceptionFormattingDeathTest.NoActiveExceptionDoesNotTerminate` PASS;
preserved tests (`ArgumentsAreNotEvaluated`,
`GetLoggerReturnsNonNullWithNameIdentity`) still PASS.

---

### Milestone 2: Lazy Logging Macros

**Goal:** Replace the no-op macro block with real three-gate/two-gate macros.
Prove lazy evaluation, exactly-once evaluation, zero ownership copy, severity
capture, logger-name attribution, null attribution, and one-message overload.

**Files:**
- Modify: `velox/ch/Common/logger_useful.h` (macro block + `detail::chLogFormat`)
- Modify: `velox/ch/Common/tests/BasicShimsTest.cpp` (logging tests)

**Interfaces:**
- Produces: `LOG_TEST`, `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`,
  `LOG_ERROR` macros with signature `(logger, fmt_str, ...)`
- Produces: `detail::chLogFormat(std::string_view)` → one-message overload
- Produces: `detail::chLogFormat(fmt::format_string<Args...>, Args&&...)` → fmt overload

#### 4.5 Formatting helper (in `logger_useful.h`, new namespace block after line 62)

Insert between the closing brace of `namespace facebook::velox::ch` (line 62) and
the macro block (line 64):

```cpp
namespace facebook::velox::ch::detail
{

/// One-message overload: already-formatted string, no format arguments.
inline std::string chLogFormat(std::string_view msg)
{
    return std::string(msg);
}

/// Compile-time checked format: fmt::format_string validates placeholders.
template <typename... Args>
std::string chLogFormat(fmt::format_string<Args...> fmtStr, Args &&... args)
{
    return fmt::format(fmtStr, std::forward<Args>(args)...);
}

} // namespace facebook::velox::ch::detail
```

#### 4.6 Macro block (replaces lines 64–72 in `logger_useful.h`)

Replace the entire `#define LOG_TEST` through `#define LOG_ERROR` block with:

```cpp
// glog severity: INFO=0 WARNING=1 ERROR=2 FATAL=3
// GOOGLE_STRIP_LOG: compile-time strip (default 0, nothing stripped).
// FLAGS_minloglevel: runtime filter (default 0, INFO).
// VLOG_IS_ON(level): runtime, true when FLAGS_v >= level.

// --- VLOG-based helper (underlying severity = INFO = 0) ---
// Three-gate: compile-strip, minloglevel, VLOG_IS_ON.
// When any gate rejects: no logger, format, or argument evaluation.
// When all gates pass: logger bound once (auto&&), format args once.
#if GOOGLE_STRIP_LOG <= 0
#define CH_VLOG_IMPL_(logger, vlog_level, fmt_str, ...)                         \
    do                                                                           \
    {                                                                            \
        if (0 >= FLAGS_minloglevel && VLOG_IS_ON(vlog_level))                   \
        {                                                                        \
            auto && _ch_log_ = (logger);                                        \
            VLOG(vlog_level)                                                     \
                << "["                                                           \
                << (_ch_log_                                                     \
                        ? std::string_view{_ch_log_->name()}                    \
                        : std::string_view{"null"})                             \
                << "] "                                                          \
                << ::facebook::velox::ch::detail::chLogFormat(                  \
                       fmt_str, ##__VA_ARGS__);                                 \
        }                                                                        \
    } while (false)
#else
#define CH_VLOG_IMPL_(logger, vlog_level, fmt_str, ...)                         \
    do                                                                           \
    {                                                                            \
    } while (false)
#endif

// --- Native-severity helper ---
// Two-gate: compile-strip (via #if around each user macro), minloglevel.
#define CH_SEVERITY_IMPL_(logger, glog_sev, sev_int, fmt_str, ...)              \
    do                                                                           \
    {                                                                            \
        if ((sev_int) >= FLAGS_minloglevel)                                     \
        {                                                                        \
            auto && _ch_log_ = (logger);                                        \
            LOG(glog_sev)                                                        \
                << "["                                                           \
                << (_ch_log_                                                     \
                        ? std::string_view{_ch_log_->name()}                    \
                        : std::string_view{"null"})                             \
                << "] "                                                          \
                << ::facebook::velox::ch::detail::chLogFormat(                  \
                       fmt_str, ##__VA_ARGS__);                                 \
        }                                                                        \
    } while (false)

// --- VLOG compatibility macros ---
#define LOG_TEST(logger, fmt_str, ...)  CH_VLOG_IMPL_(logger, 3, fmt_str, ##__VA_ARGS__)
#define LOG_TRACE(logger, fmt_str, ...) CH_VLOG_IMPL_(logger, 2, fmt_str, ##__VA_ARGS__)
#define LOG_DEBUG(logger, fmt_str, ...) CH_VLOG_IMPL_(logger, 1, fmt_str, ##__VA_ARGS__)

// --- Native severity compatibility macros ---
#if GOOGLE_STRIP_LOG <= 0
#define LOG_INFO(logger, fmt_str, ...) \
    CH_SEVERITY_IMPL_(logger, INFO, 0, fmt_str, ##__VA_ARGS__)
#else
#define LOG_INFO(logger, fmt_str, ...) do {} while (false)
#endif

#if GOOGLE_STRIP_LOG <= 1
#define LOG_WARNING(logger, fmt_str, ...) \
    CH_SEVERITY_IMPL_(logger, WARNING, 1, fmt_str, ##__VA_ARGS__)
#else
#define LOG_WARNING(logger, fmt_str, ...) do {} while (false)
#endif

#if GOOGLE_STRIP_LOG <= 2
#define LOG_ERROR(logger, fmt_str, ...) \
    CH_SEVERITY_IMPL_(logger, ERROR, 2, fmt_str, ##__VA_ARGS__)
#else
#define LOG_ERROR(logger, fmt_str, ...) do {} while (false)
#endif
```

#### 4.7 Test helpers (in `BasicShimsTest.cpp`, anonymous namespace)

Add after the existing anonymous namespace opening:

```cpp
/// Test sink capturing glog messages. Pattern verified from
/// velox/common/time/tests/HierarchicalTimerTest.cpp:32-62.
class TestLogSink : public google::LogSink
{
public:
    TestLogSink()
    {
        google::AddLogSink(this);
    }

    ~TestLogSink() override
    {
        google::RemoveLogSink(this);
    }

    TestLogSink(const TestLogSink &) = delete;
    TestLogSink & operator=(const TestLogSink &) = delete;

    void send(
        google::LogSeverity severity,
        const char * /*full_filename*/,
        const char * /*base_filename*/,
        int /*line*/,
        const struct ::tm * /*tm_time*/,
        const char * message,
        size_t message_len) override
    {
        last_severity_ = severity;
        captured_ += std::string(message, message_len);
    }

    const std::string & captured() const { return captured_; }
    google::LogSeverity lastSeverity() const { return last_severity_; }

    void clear()
    {
        captured_.clear();
        last_severity_ = google::GLOG_INFO;
    }

private:
    std::string captured_;
    google::LogSeverity last_severity_ = google::GLOG_INFO;
};

/// RAII guard: saves/restores glog flags and optionally swaps the
/// exception emitter. Prevents cross-test leakage.
struct GlogFlagGuard
{
    int32_t saved_v;
    int32_t saved_minloglevel;
    GlogFlagGuard()
        : saved_v(FLAGS_v)
        , saved_minloglevel(FLAGS_minloglevel)
    {
    }
    ~GlogFlagGuard()
    {
        FLAGS_v = saved_v;
        FLAGS_minloglevel = saved_minloglevel;
    }
};

/// RAII guard for the exception emitter function pointer.
struct EmitterGuard
{
    detail::ExceptionEmitter old_;
    explicit EmitterGuard(detail::ExceptionEmitter newEmitter)
        : old_(detail::testExceptionEmitter())
    {
        detail::testExceptionEmitter() = newEmitter;
    }
    ~EmitterGuard()
    {
        detail::testExceptionEmitter() = old_;
    }
};
```

#### 4.8 TDD Steps — Lazy Logging

- [ ] **M2-T1** Write `LazyLoggingTest.LogTestEvaluatesWhenEnabled` — proves
  `LOG_TEST` is NOT a permanent no-op:

```cpp
TEST(LazyLoggingTest, LogTestEvaluatesWhenEnabled)
{
    GlogFlagGuard guard;
    FLAGS_v = 3;
    FLAGS_minloglevel = 0;

    int evaluated = 0;
    auto logger = getLogger("test");
    LOG_TEST(logger, "value {}", ++evaluated);
    EXPECT_EQ(evaluated, 1)
        << "LOG_TEST must evaluate when --v=3 and minloglevel=0";
}
```

- [ ] **M2-T2** Write `LazyLoggingTest.VlogLevelsFilteredAtDefaultV` — uses a
  side-effect lambda for the logger expression to also catch MUT-03 (pre-gate
  logger eval):

```cpp
TEST(LazyLoggingTest, VlogLevelsFilteredAtDefaultV)
{
    GlogFlagGuard guard;
    FLAGS_v = 0;
    FLAGS_minloglevel = 0;

    int logger_eval = 0;
    int arg_eval = 0;
    auto makeLogger = [&]() -> LoggerPtr
    {
        ++logger_eval;
        return getLogger("test");
    };

    LOG_TEST(makeLogger(), "v {}", ++arg_eval);
    LOG_TRACE(makeLogger(), "v {}", ++arg_eval);
    LOG_DEBUG(makeLogger(), "v {}", ++arg_eval);
    EXPECT_EQ(arg_eval, 0)
        << "VLOG-based macros must not evaluate args at default FLAGS_v=0";
    EXPECT_EQ(logger_eval, 0)
        << "VLOG-based macros must not evaluate logger at default FLAGS_v=0";
}
```

- [ ] **M2-T3** Write `LazyLoggingTest.VlogMinloglevelGate` — the combined gate
  case from the binding spec (`--v=3 --minloglevel=1`):

```cpp
TEST(LazyLoggingTest, VlogMinloglevelGate)
{
    GlogFlagGuard guard;
    FLAGS_v = 3;            // enables VLOG_IS_ON(3)
    FLAGS_minloglevel = 1;  // suppresses underlying INFO (0)

    int evaluated = 0;
    auto logger = getLogger("test");
    LOG_TEST(logger, "v {}", ++evaluated);
    LOG_TRACE(logger, "v {}", ++evaluated);
    LOG_DEBUG(logger, "v {}", ++evaluated);
    EXPECT_EQ(evaluated, 0)
        << "VLOG macros must not evaluate when minloglevel suppresses INFO";
}
```

- [ ] **M2-T4** Write `LazyLoggingTest.NativeSeverityMinloglevelGate`:

```cpp
TEST(LazyLoggingTest, NativeSeverityMinloglevelGate)
{
    GlogFlagGuard guard;
    FLAGS_minloglevel = 3; // suppress INFO, WARNING, ERROR

    int info_eval = 0, warn_eval = 0, error_eval = 0;
    auto logger = getLogger("test");
    LOG_INFO(logger, "v {}", ++info_eval);
    LOG_WARNING(logger, "v {}", ++warn_eval);
    LOG_ERROR(logger, "v {}", ++error_eval);
    EXPECT_EQ(info_eval, 0);
    EXPECT_EQ(warn_eval, 0);
    EXPECT_EQ(error_eval, 0);
}
```

- [ ] **M2-T5** Write `LazyLoggingTest.ExactlyOnceEvaluation`:

```cpp
TEST(LazyLoggingTest, ExactlyOnceEvaluation)
{
    GlogFlagGuard guard;
    FLAGS_v = 3;
    FLAGS_minloglevel = 0;

    int logger_eval = 0;
    int fmt_eval = 0;
    int arg_eval = 0;

    auto makeLogger = [&]() -> LoggerPtr
    {
        ++logger_eval;
        return getLogger("once");
    };

    LOG_TEST(makeLogger(), "{}", (++arg_eval, ++fmt_eval, 42));
    EXPECT_EQ(logger_eval, 1) << "Logger evaluated more than once";
    EXPECT_EQ(fmt_eval, 1) << "Format args evaluated more than once";
    EXPECT_EQ(arg_eval, 1) << "Arguments evaluated more than once";
}
```

- [ ] **M2-T6** Write `LazyLoggingTest.NoOwnershipCopy`:

```cpp
TEST(LazyLoggingTest, NoOwnershipCopy)
{
    GlogFlagGuard guard;
    FLAGS_minloglevel = 0;

    auto logger = getLogger("ownership");
    ASSERT_EQ(logger.use_count(), 1);

    long use_count_during = 0;
    LOG_WARNING(logger, "count={}",
        (use_count_during = logger.use_count(), 42));

    EXPECT_EQ(use_count_during, 1)
        << "auto&& must not copy shared_ptr (would be 2 with auto)";
    EXPECT_EQ(logger.use_count(), 1);
}
```

- [ ] **M2-T7** Write `LazyLoggingTest.SeverityNameMessageCapture`:

```cpp
TEST(LazyLoggingTest, SeverityNameMessageCapture)
{
    GlogFlagGuard guard;
    FLAGS_minloglevel = 0;
    FLAGS_v = 3;
    TestLogSink sink;

    auto logger = getLogger("MyComponent");

    sink.clear();
    LOG_WARNING(logger, "warn {}", 1);
    EXPECT_NE(sink.captured().find("[MyComponent]"), std::string::npos);
    EXPECT_NE(sink.captured().find("warn 1"), std::string::npos);
    EXPECT_EQ(sink.lastSeverity(), google::GLOG_WARNING);

    sink.clear();
    LOG_ERROR(logger, "err {}", 2);
    EXPECT_NE(sink.captured().find("[MyComponent]"), std::string::npos);
    EXPECT_NE(sink.captured().find("err 2"), std::string::npos);
    EXPECT_EQ(sink.lastSeverity(), google::GLOG_ERROR);

    sink.clear();
    LOG_INFO(logger, "info {}", 3);
    EXPECT_NE(sink.captured().find("[MyComponent]"), std::string::npos);
    EXPECT_NE(sink.captured().find("info 3"), std::string::npos);
    EXPECT_EQ(sink.lastSeverity(), google::GLOG_INFO);

    sink.clear();
    LOG_TEST(logger, "test {}", 4);
    EXPECT_NE(sink.captured().find("[MyComponent]"), std::string::npos);
    EXPECT_NE(sink.captured().find("test 4"), std::string::npos);
}
```

- [ ] **M2-T8** Write `LazyLoggingTest.NullLoggerAttribution`:

```cpp
TEST(LazyLoggingTest, NullLoggerAttribution)
{
    GlogFlagGuard guard;
    FLAGS_minloglevel = 0;
    TestLogSink sink;

    LoggerPtr null_logger;
    LOG_WARNING(null_logger, "null test");
    EXPECT_NE(sink.captured().find("[null]"), std::string::npos)
        << "Null logger must be attributed as [null]. Got: " << sink.captured();
}
```

- [ ] **M2-T9** Write `LazyLoggingTest.OneMessageOverload`:

```cpp
TEST(LazyLoggingTest, OneMessageOverload)
{
    GlogFlagGuard guard;
    FLAGS_minloglevel = 0;
    TestLogSink sink;

    auto logger = getLogger("test");
    LOG_WARNING(logger, "already formatted message");
    EXPECT_NE(sink.captured().find("already formatted message"), std::string::npos);
}
```

- [ ] **M2-I1** Apply header changes from §4.5 and §4.6.

- [ ] **M2-G1** Build and run (mono):

```bash
cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  ninja -C _build/debug velox_ch_common_test \
    > _build/debug/build_017b_m2.log 2>&1
```

```bash
source /root/oss/velox-helper/env.sh && \
  cd /root/oss/velox/_build/debug && \
  ctest -R velox_ch_common_test --output-on-failure \
    > ctest_017b_m2.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to analyze each log.

**GREEN gate:** All `LazyLoggingTest` cases PASS; all M1 tests still PASS.

---

### Milestone 3: Exception Logging

**Goal:** Prove `tryLogCurrentException` overloads with TDD — both call shapes,
no-active-exception diagnostic, attribution, original rethrow, Velox/std/unknown
capture, and emergency `stderr` path. The implementation is already in
`logger_useful.cpp` from M1 (§4.2); this milestone writes and runs the tests.

**Files:**
- Modify: `velox/ch/Common/tests/BasicShimsTest.cpp` (exception logging tests)

**Interfaces:**
- Consumes: `tryLogCurrentException(const LoggerPtr&, const std::string&)` noexcept
- Consumes: `tryLogCurrentException(const char*, const std::string&)` noexcept
- Consumes: `detail::testExceptionEmitter()` (for injection)
- Consumes: `detail::defaultExceptionEmitter` (for restore)

#### 4.9 TDD Steps — Exception Logging

- [ ] **M3-T1** Write `ExceptionLoggingTest.LoggerPtrOverload`:

```cpp
TEST(ExceptionLoggingTest, LoggerPtrOverload)
{
    TestLogSink sink;
    try
    {
        throw std::runtime_error("captured");
    }
    catch (...)
    {
        EXPECT_NO_THROW(tryLogCurrentException(getLogger("LPO"), "during op"));
    }
    EXPECT_NE(sink.captured().find("[LPO]"), std::string::npos);
    EXPECT_NE(sink.captured().find("captured"), std::string::npos);
    EXPECT_NE(sink.captured().find("during op"), std::string::npos);
}
```

- [ ] **M3-T2** Write `ExceptionLoggingTest.ConstCharOverload`:

```cpp
TEST(ExceptionLoggingTest, ConstCharOverload)
{
    TestLogSink sink;
    try
    {
        throw std::runtime_error("char overload");
    }
    catch (...)
    {
        EXPECT_NO_THROW(tryLogCurrentException("MyFunc"));
        EXPECT_NO_THROW(tryLogCurrentException("MyFunc2", "ctx"));
    }
    EXPECT_NE(sink.captured().find("[MyFunc]"), std::string::npos);
    EXPECT_NE(sink.captured().find("char overload"), std::string::npos);
}
```

- [ ] **M3-T3** Write `ExceptionLoggingTest.OriginalRethrow`:

```cpp
TEST(ExceptionLoggingTest, OriginalRethrow)
{
    bool caughtOriginal = false;
    try
    {
        try
        {
            VELOX_FAIL("original error");
        }
        catch (...)
        {
            tryLogCurrentException(getLogger("Test"), "logging");
            throw; // rethrow — must still be the original exception
        }
    }
    catch (const velox::VeloxException & e)
    {
        caughtOriginal =
            (e.message().find("original error") != std::string::npos);
    }
    EXPECT_TRUE(caughtOriginal)
        << "tryLogCurrentException must preserve the original exception";
}
```

- [ ] **M3-T4** Write `ExceptionLoggingTest.VeloxExceptionCaptured`:

```cpp
TEST(ExceptionLoggingTest, VeloxExceptionCaptured)
{
    TestLogSink sink;
    try
    {
        VELOX_FAIL("velox detail");
    }
    catch (...)
    {
        tryLogCurrentException(getLogger("VCap"));
    }
    EXPECT_NE(sink.captured().find("VeloxException"), std::string::npos);
    EXPECT_NE(sink.captured().find("velox detail"), std::string::npos);
    EXPECT_NE(sink.captured().find("[VCap]"), std::string::npos);
}
```

- [ ] **M3-T5** Write `ExceptionLoggingTest.UnknownExceptionCaptured`:

```cpp
TEST(ExceptionLoggingTest, UnknownExceptionCaptured)
{
    TestLogSink sink;
    try
    {
        throw 42;
    }
    catch (...)
    {
        EXPECT_NO_THROW(tryLogCurrentException(getLogger("X")));
        EXPECT_NO_THROW(tryLogCurrentException("Y", "ctx"));
    }
    EXPECT_NE(sink.captured().find("Unknown exception"), std::string::npos)
        << "Unknown exception must be logged, not swallowed. Got: "
        << sink.captured();
}
```

- [ ] **M3-T6** Write `ExceptionLoggingTest.NoActiveExceptionDiagnostic`:

```cpp
TEST(ExceptionLoggingTest, NoActiveExceptionDiagnostic)
{
    TestLogSink sink;
    // Called outside any catch block — misuse diagnostic, never crash.
    EXPECT_NO_THROW(tryLogCurrentException(getLogger("Misuse")));
    EXPECT_NE(sink.captured().find("no active exception"), std::string::npos)
        << "Misuse diagnostic must be emitted. Got: " << sink.captured();
}
```

- [ ] **M3-T7** Write `ExceptionLoggingTest.EmergencyStderr`:

```cpp
TEST(ExceptionLoggingTest, EmergencyStderr)
{
    // Inject a throwing emitter to force the emergency stderr path.
    EmitterGuard emitterGuard([](const char *, const char *)
    {
        throw std::runtime_error("injected emitter failure");
    });

    // Capture stderr via pipe.
    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0);
    int saved_stderr = ::dup(STDERR_FILENO);
    ASSERT_NE(saved_stderr, -1);
    ::dup2(pipefd[1], STDERR_FILENO);

    try
    {
        throw std::runtime_error("original");
    }
    catch (...)
    {
        EXPECT_NO_THROW(tryLogCurrentException(getLogger("Emergency")));
    }

    // Restore stderr before reading.
    ::dup2(saved_stderr, STDERR_FILENO);
    ::close(saved_stderr);
    ::close(pipefd[1]);

    char buf[512];
    ssize_t n = ::read(pipefd[0], buf, sizeof(buf) - 1);
    ::close(pipefd[0]);
    ASSERT_GT(n, 0) << "Emergency stderr path must write output";
    buf[n] = '\0';
    std::string captured(buf);

    EXPECT_NE(captured.find("emergency"), std::string::npos)
        << "Emergency message must appear on stderr. Got: " << captured;
}
```

- [ ] **M3-T8** Write `ExceptionLoggingTest.FileSegmentCallPatterns` — verify the
  exact `tryLogCurrentException` overload shapes used by its four callers
  (`FileCache.cpp`, `FileSegment.cpp`, `Metadata.cpp`,
  `EvictionCandidates.cpp`). The getter-only call sites
  (`SLRUFileCachePriority.cpp`, `FileCacheScheduler.cpp`) are rebuilt through
  their production library owners at M3-C1/M1-G1 and use the separately tested
  `getCurrentExceptionMessage` API:

```cpp
TEST(ExceptionLoggingTest, FileSegmentCallPatterns)
{
    TestLogSink sink;
    auto log = getLogger("FileCache");
    try
    {
        VELOX_FAIL("simulated");
    }
    catch (...)
    {
        // Pattern: FileSegment.cpp:1466, FileCache.cpp:458,507,1861,1877,2551
        tryLogCurrentException(__PRETTY_FUNCTION__);
        // Pattern: FileSegment.cpp:701,1050,1089,1424
        tryLogCurrentException(log, "Failed to finalize cache writer");
        // Pattern: FileSegment.cpp:775 (fmt::format context)
        tryLogCurrentException(
            log,
            fmt::format("Failed to rename cache file '{}'", "/tmp/test"));
        // Pattern: FileCache.cpp:1553,1665,1695,1710,1817,1947,1963
        tryLogCurrentException(log, "Error in background thread");
        // Pattern: FileSegment.cpp:1424 (no context)
        tryLogCurrentException(log);
    }
    EXPECT_NE(sink.captured().find("[FileCache]"), std::string::npos);
    EXPECT_NE(sink.captured().find("simulated"), std::string::npos);
}
```

- [ ] **M3-G1** Build and run (mono):

```bash
cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  ninja -C _build/debug velox_ch_common_test \
    > _build/debug/build_017b_m3.log 2>&1
```

```bash
source /root/oss/velox-helper/env.sh && \
  cd /root/oss/velox/_build/debug && \
  ctest -R velox_ch_common_test --output-on-failure \
    > ctest_017b_m3.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to analyze each log.

**GREEN gate:** All `ExceptionLoggingTest` cases PASS; all M1/M2 tests still
PASS.

- [ ] **M3-C1** Build all FileCache targets (mono) to verify call-site
  compatibility (no source changes in callers):

```bash
cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  ninja -C _build/debug \
    velox_ch_filecache \
    velox_ch_filecache_core \
    velox_ch_filecache_manager \
    velox_ch_filecache_dwio \
    > _build/debug/build_017b_m3_callers.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to analyze the log.

**GREEN gate:** All FileCache targets compile and link with the new declarations.

---

### Milestone 4: Compile-Strip Proof

**Goal:** A separate executable compiled with `GOOGLE_STRIP_LOG=1` proves that
stripped levels (`LOG_TEST`, `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`) evaluate no
logger expression, format expression, or arguments.

**Files:**
- Create: `velox/ch/Common/tests/CompileStripProbe.cpp`
- Modify: `velox/ch/Common/tests/CMakeLists.txt`

#### 4.10 Probe source

```cpp
/// Compile-strip probe: compiled with GOOGLE_STRIP_LOG=1.
/// At strip level 1, all VLOG-based macros (LOG_TEST/TRACE/DEBUG) and
/// LOG_INFO are compiled out. This probe verifies no expression is
/// evaluated for stripped levels.
/// Exit status 0 = pass, non-zero = fail.

// GOOGLE_STRIP_LOG must be set before glog inclusion. This is done
// via target_compile_definitions in CMake.

#include "velox/ch/Common/logger_useful.h"

using namespace facebook::velox::ch;

int main()
{
    int evaluated = 0;
    auto logger = getLogger("strip-probe");

    // These must all be compiled out at strip level 1.
    LOG_TEST(logger, "v {}", ++evaluated);
    LOG_TRACE(logger, "v {}", ++evaluated);
    LOG_DEBUG(logger, "v {}", ++evaluated);
    LOG_INFO(logger, "v {}", ++evaluated);

    if (evaluated != 0)
    {
        return 1; // FAIL: stripped levels evaluated expressions
    }

    // LOG_WARNING and LOG_ERROR are NOT stripped at level 1.
    // We do not test them here because they are expected to be active.

    return 0;
}
```

#### 4.11 CMake registration

Append to `velox/ch/Common/tests/CMakeLists.txt`:

```cmake
add_executable(velox_ch_compile_strip_probe CompileStripProbe.cpp)
add_test(velox_ch_compile_strip_probe velox_ch_compile_strip_probe)

target_compile_definitions(
  velox_ch_compile_strip_probe
  PRIVATE
    GOOGLE_STRIP_LOG=1
)

target_link_libraries(
  velox_ch_compile_strip_probe
  PRIVATE
    velox_ch_filecache
    Folly::folly
    fmt::fmt
    glog::glog
)
```

#### 4.12 TDD Steps

- [ ] **M4-I1** Create `CompileStripProbe.cpp` from §4.10.

- [ ] **M4-I2** Add CMake registration from §4.11.

- [ ] **M4-G1** Build and run:

```bash
cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  ninja -C _build/debug velox_ch_compile_strip_probe \
    > _build/debug/build_017b_m4.log 2>&1
```

```bash
source /root/oss/velox-helper/env.sh && \
  cd /root/oss/velox/_build/debug && \
  ctest -R velox_ch_compile_strip_probe --output-on-failure \
    > ctest_017b_m4.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to analyze each log.

**GREEN gate:** Probe exits with status 0.

---

### Milestone 5: Accumulated Gate (Mono + Non-Mono)

**Goal:** All registered `velox_ch_*` targets build and pass in both mono and
non-mono configurations. This proves that CMake dependencies are complete and
that no existing behavior is broken.

#### 5.1 Complete `velox_ch_*` target list

Discovered from CMake at head `0c5b5918eb` (Task 018R changed only the four
Arrow CMake files and did not change this target list):

**Test targets (add_test):**
| Target | Source |
|--------|--------|
| `velox_ch_common_test` | `velox/ch/Common/tests/CMakeLists.txt` |
| `velox_ch_threadpool_test` | `velox/ch/Common/tests/CMakeLists.txt` |
| `velox_ch_scheduler_test` | `velox/ch/Common/tests/CMakeLists.txt` |
| `velox_ch_chassert_release_probe` | `velox/ch/Common/tests/CMakeLists.txt` |
| `velox_ch_chassert_sanitizer_gate_test` | `velox/ch/Common/tests/CMakeLists.txt` |
| `velox_ch_metrics_snapshot_test` | `velox/ch/Common/tests/CMakeLists.txt` |
| `velox_ch_compile_strip_probe` | `velox/ch/Common/tests/CMakeLists.txt` (new) |
| `velox_ch_guards_test` | `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` |
| `velox_ch_leaf_types_test` | `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` |
| `velox_ch_sharded_map_test` | `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` |
| `velox_ch_settings_test` | `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` |
| `velox_ch_filecache_core_scc_test` | `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` |
| `velox_ch_filecache_priority_cursor_test` | `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` |
| `velox_ch_filecache_manager_test` | `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` |
| `velox_ch_io_test` | `velox/ch/IO/tests/CMakeLists.txt` |
| `velox_ch_filecache_e2e_test` | `velox/ch/Disks/IO/tests/CMakeLists.txt` |

**Benchmark targets (add_executable, not add_test):**
| Target | Source |
|--------|--------|
| `velox_ch_filecache_seek_benchmark` | `velox/ch/benchmarks/CMakeLists.txt` |
| `velox_ch_fcbi_benchmark` | `velox/ch/benchmarks/CMakeLists.txt` |

**Library targets:**
| Target | Source |
|--------|--------|
| `velox_ch_filecache` | `velox/ch/Common/CMakeLists.txt` |
| `velox_ch_filecache_dwio` | `velox/ch/Disks/IO/CMakeLists.txt` |
| `velox_ch_filecache_core` | `velox/ch/Interpreters/FileCache/CMakeLists.txt` |
| `velox_ch_filecache_manager` | `velox/ch/Interpreters/FileCache/CMakeLists.txt` |

#### 5.2 TDD Steps

- [ ] **M5-B1** Build ALL `velox_ch_*` targets (mono):

```bash
cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  targets=$(grep -rh "^add_executable\|^add_test" \
    velox/ch/*/tests/CMakeLists.txt \
    velox/ch/*/*/tests/CMakeLists.txt \
    velox/ch/benchmarks/CMakeLists.txt 2>/dev/null | \
    grep -oP '(?<=\()[^ )]+' | sort -u) && \
  ninja -C _build/debug $targets \
    > _build/debug/build_017b_mono_all.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to analyze the log.

- [ ] **M5-B2** Run accumulated CTest (mono):

```bash
source /root/oss/velox-helper/env.sh && \
  cd /root/oss/velox/_build/debug && \
  ctest -R "velox_ch_" --output-on-failure \
    > ctest_017b_mono.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to analyze the log.

**GREEN gate:** zero failures across all `velox_ch_*` test targets.

- [ ] **M5-B3** Configure and build (non-mono):

```bash
mkdir -p /root/oss/velox/_build/debug-task017b-nonmono && \
  cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  cmake -B _build/debug-task017b-nonmono -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=/root/oss/gluten/dev/vcpkg/toolchain.cmake \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DVELOX_GFLAGS_TYPE=static \
    -DVELOX_BUILD_TESTING=ON \
    -DVELOX_ENABLE_BENCHMARKS=ON \
    -DVELOX_ENABLE_EXEC=ON \
    -DVELOX_ENABLE_PARQUET=OFF \
    -DVELOX_ENABLE_REMOTE_FUNCTIONS=ON \
    -DVELOX_ENABLE_GROUPED_TESTS=OFF \
    -DVELOX_MONO_LIBRARY=OFF \
    -DVELOX_BUILD_RUNNER=OFF \
    -DVELOX_ENABLE_GEO=OFF \
    -DVELOX_BUILD_MINIMAL=OFF \
    -DVELOX_SIMDJSON_SKIPUTF8VALIDATION=ON \
    -GNinja \
    > _build/debug-task017b-nonmono/cmake_017b.log 2>&1
```

```bash
cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  targets=$(grep -rh "^add_executable\|^add_test" \
    velox/ch/*/tests/CMakeLists.txt \
    velox/ch/*/*/tests/CMakeLists.txt \
    velox/ch/benchmarks/CMakeLists.txt 2>/dev/null | \
    grep -oP '(?<=\()[^ )]+' | sort -u) && \
  ninja -C _build/debug-task017b-nonmono $targets \
    > _build/debug-task017b-nonmono/build_017b_all.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to analyze the logs.

- [ ] **M5-B4** Run accumulated CTest (non-mono):

```bash
source /root/oss/velox-helper/env.sh && \
  cd /root/oss/velox/_build/debug-task017b-nonmono && \
  ctest -R "velox_ch_" --output-on-failure \
    > ctest_017b_nonmono.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to analyze the log.

**GREEN gate:** zero failures across all `velox_ch_*` test targets.

---

## 5. RED/GREEN/Mutation Outcome Matrix

Every mutation below must produce a real test failure (not structural
inspection). The Worker applies the mutation, rebuilds `velox_ch_common_test`,
runs the affected test(s), and records the RED failure text.

| ID | Mutation | How to apply | Affected test(s) | Expected RED |
|----|----------|--------------|-------------------|--------------|
| MUT-01 | Permanent `LOG_TEST` no-op: replace `CH_VLOG_IMPL_` with `do {} while (false)` in `LOG_TEST` definition only | Change `#define LOG_TEST(...)  CH_VLOG_IMPL_(...)` to `#define LOG_TEST(logger, fmt_str, ...) do {} while (false)` | `LogTestEvaluatesWhenEnabled` | `evaluated` stays 0, expected 1 |
| MUT-02 | Missing minloglevel gate: remove `0 >= FLAGS_minloglevel &&` from `CH_VLOG_IMPL_` | Delete the `0 >= FLAGS_minloglevel &&` prefix in the `if` condition | `VlogMinloglevelGate` | `evaluated` is nonzero, expected 0 |
| MUT-03 | Pre-gate logger eval: move `auto && _ch_log_ = (logger);` before the `if` in `CH_VLOG_IMPL_` | Move the `auto && _ch_log_` line above the `if (0 >= FLAGS_minloglevel ...)` | `VlogLevelsFilteredAtDefaultV` | `logger_eval` is 3 (one per macro call), expected 0 |
| MUT-04 | Ownership copy: change `auto &&` to `auto` in `CH_SEVERITY_IMPL_` | Replace `auto && _ch_log_` with `auto _ch_log_` | `NoOwnershipCopy` | `use_count_during` is 2, expected 1 |
| MUT-05 | Missing attribution: remove `<< "[" << (...name...) << "] "` from `CH_SEVERITY_IMPL_` | Delete the three `<<` lines that emit `[name]` | `SeverityNameMessageCapture` | `sink.captured().find("[MyComponent]")` returns npos |
| MUT-06 | Bare throw outside catch: remove `if (!eptr) return {};` null guard from `getCurrentExceptionMessage` AND replace `std::rethrow_exception(eptr);` with `throw;` in `formatExceptionPtr` — together, calling `getCurrentExceptionMessage` outside a catch invokes `throw;` with no active exception → `std::terminate` | Remove `if (!eptr) return {};` (lines 287-290) AND change `std::rethrow_exception(eptr);` to `throw;` in `formatExceptionPtr` | `ExceptionFormattingDeathTest.NoActiveExceptionDoesNotTerminate` | Subprocess terminates non-zero; `EXPECT_EXIT(…, ExitedWithCode(0), "")` fails → RED |
| MUT-07 | Ignored stack flag: always append stack regardless of `withStackTrace` in `formatExceptionPtr` | Remove the `if (withStackTrace)` guard | `VeloxExceptionWithoutStack` | `"Stack trace"` found when it should be absent |
| MUT-08 | Swallowed emergency failure: replace emergency `::write(STDERR_FILENO, ...)` with `// nothing` | Comment out the `::write(...)` call in the outer `catch (...)` | `EmergencyStderr` | `n` (bytes read from pipe) is 0, expected > 0 |
| MUT-09 | Missing verbosity gate: remove `VLOG_IS_ON(vlog_level) &&` from `CH_VLOG_IMPL_` — leaves only the `minloglevel` gate, so at `FLAGS_v=0` (default) all three VLOG macros evaluate their logger and arguments | Delete `VLOG_IS_ON(vlog_level) &&` from the `if` condition in `CH_VLOG_IMPL_` | `VlogLevelsFilteredAtDefaultV` | `arg_eval` and `logger_eval` are non-zero, expected 0 |

> **Compile-strip configuration verification (Milestone 4 only):** `velox_ch_compile_strip_probe` is built with `GOOGLE_STRIP_LOG=1` and its exit code checked in M4-G1. This is a compile-configuration check, not one of the nine spec mutations. No source code mutation is applied; the `GOOGLE_STRIP_LOG` definition is toggled only in the probe target's CMake compile definitions.

**Mutation workflow for each MUT-NN:**

```bash
# 1. Apply the mutation (edit logger_useful.h or logger_useful.cpp)
# 2. Rebuild:
cd /root/oss/velox && \
  source /root/oss/velox-helper/env.sh && \
  ninja -C _build/debug velox_ch_common_test \
    > _build/debug/build_017b_mut_NN.log 2>&1
# 3. Run the affected test:
source /root/oss/velox-helper/env.sh && \
  cd /root/oss/velox/_build/debug && \
  ctest -R velox_ch_common_test --output-on-failure \
    > ctest_017b_mut_NN.log 2>&1
# 4. Log analysis: task subagent confirms RED failure for the expected test.
# 5. Revert the mutation.
```

---

## 6. Direct Dependencies (Non-Mono Mode)

`velox_ch_filecache` non-mono link additions (PUBLIC):

| Dependency | CMake target | Required by |
|------------|--------------|-------------|
| Velox process (StackTrace) | `velox_process` | `getCurrentExceptionMessage` → `StackTrace::toString()` |
| glog | `glog::glog` | `LOG(ERROR)` in emitter, `VLOG`/`VLOG_IS_ON`/`LOG` in macros |

Pre-existing PUBLIC dependencies (unchanged):

| Dependency | CMake target |
|------------|--------------|
| Velox common config | `velox_common_config` |
| Velox exception | `velox_exception` |
| Folly | `Folly::folly` |
| fmt | `fmt::fmt` |

In mono mode, all dependencies are linked transitively through the mono `velox`
target. No explicit link additions needed.

`velox_ch_common_test` test link additions (PRIVATE):

| Dependency | CMake target | Required by |
|------------|--------------|-------------|
| glog | `glog::glog` | `TestLogSink`, `google::AddLogSink`/`RemoveLogSink`, `FLAGS_v`/`FLAGS_minloglevel` |

`velox_ch_compile_strip_probe` link (PRIVATE):

| Dependency | CMake target |
|------------|--------------|
| `velox_ch_filecache` | Library under test |
| `Folly::folly` | Transitive |
| `fmt::fmt` | Used by header |
| `glog::glog` | Used by macros |

---

## 7. Required Test File Includes

The test file (`BasicShimsTest.cpp`) needs these additional includes:

```cpp
#include <unistd.h>           // pipe, dup, dup2, read, write, STDERR_FILENO

#include <folly/ScopeGuard.h> // folly::makeGuard (for stack flag RAII)
#include <glog/logging.h>     // google::LogSink, FLAGS_v, FLAGS_minloglevel

#include "velox/common/base/VeloxException.h" // VELOX_FAIL, VeloxException
```

Existing includes that remain: `<gtest/gtest.h>`, `"velox/ch/Common/logger_useful.h"`,
`"velox/common/base/Exceptions.h"`.

---

## 8. VeloxException API Reference (unchanged at live source `0c5b5918eb`)

| Symbol | Location | Signature |
|--------|----------|-----------|
| `stackTrace` | `VeloxException.h:205` | `const process::StackTrace* stackTrace() const` — null when stack capture disabled at throw-time |
| `message` | `VeloxException.h:220` | `const std::string& message() const` |
| `errorCode` | `VeloxException.h:232` | `const std::string& errorCode() const` — e.g. `"INVALID_STATE"` |
| `errorSource` | `VeloxException.h:236` | `const std::string& errorSource() const` — e.g. `"RUNTIME"` |
| `StackTrace::toString` | `StackTrace.h:46` | `const std::string& toString() const` — lazy-init on first call |
| System stack flag | `VeloxException.h:33` | `DECLARE_bool(velox_exception_system_stacktrace_enabled)` |
| User stack flag | `VeloxException.h:34` | `DECLARE_bool(velox_exception_user_stacktrace_enabled)` |

---

## 9. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| `getCurrentExceptionMessage` called outside catch | `std::current_exception()` returns null → empty string return. No UB, no `std::terminate`. |
| glog linker errors in mono | glog linked transitively via Folly/velox mono target. Explicit dep is for non-mono only. |
| `##__VA_ARGS__` portability | GCC and Clang both support this extension. Velox requires one of these compilers. |
| `StackTrace::toString` expensive on first call | Only invoked when `withStackTrace=true` and pointer is non-null. Lazy-init is StackTrace's responsibility. |
| `FLAGS_v`/`FLAGS_minloglevel` cross-test leakage | Every test saves/restores flags with `GlogFlagGuard` RAII. |
| `google::LogSink` thread safety | Tests are single-threaded. Sink lives on stack with deterministic lifetime. |
| Non-mono ODR violations from inline functions | Non-template function bodies moved to `.cpp`. Only templates, `inline` functions, and macros remain in header. |
| Existing test `AllLogMacrosDoNotEvaluateArguments` conflict | Explicitly replaced by `LazyLoggingTest` cases that test correct new semantics. |
| Emitter injection unsafe in production | Emitter is a static function pointer initialized to `defaultExceptionEmitter`. No LogSink, no virtual dispatch, no runtime registration. Test-only swaps are RAII-guarded. |
| `std::string_view` streaming to glog | **Toolchain-verified.** Compiled, linked, and ran a probe against the installed vcpkg glog (fmt 11.0.2): `LOG(WARNING) << std::string_view{...}` and `chLogFormat` overload resolution both work correctly on this toolchain. Not a concern. |
| Emergency stderr path unreliable | `::write(STDERR_FILENO, ...)` is async-signal-safe and allocation-free. A broken stderr is outside our control; the spec promises only a best-effort attempt. |
| Newly-enabled `fmt::format_string` type check fails at compile time | `fmt::format_string` performs compile-time placeholder/type validation for the first time on ~96 call sites. A scan found 0 placeholder/arg-count mismatches and all argument types are string or integer-like. If a genuine pre-existing type mismatch surfaces during M2-G1 or M3-C1, the Worker stops, records the exact failing call site and error, and escalates to the Controller. No silent call-site rewrite is permitted. |

---

## 10. Worker Protocol

1. Worker never stages, commits, or pushes. Controller commits after independent
   task review.
2. Implementation requires the Controller to set
   `implementation_authorized: true` in the task index; this prerequisite was
   satisfied on 2026-07-24 and is recorded in the plan-review receipt.
3. Every redirected build/test log is analyzed by a `task` subagent that returns
   only a concise summary. The Worker never pastes full build/test logs into its
   context or the result receipt.
4. After all milestones GREEN and all mutations RED, the Worker writes the result
   receipt to the canonical location:

   ```text
   port/task/result/017b-filecache-logging-exception-stack-result.md
   ```

   The receipt header must include:

   ```text
   worker_status: ready_for_controller
   ```

5. After writing the result receipt, the Worker stops. It does not create a PR,
   branch, or stacked change.
6. After Task 017B acceptance, Task 019 implementation may begin.

---

## 11. Deliberate Differences from Stale Plan

| Area | Stale plan | This plan |
|------|-----------|-----------|
| `LOG_TEST` | Permanent no-op `do {} while (false)` | `VLOG(3)` — evaluates when `--v=3` |
| `LOG_TRACE` | `VLOG(3)` | `VLOG(2)` |
| `LOG_DEBUG` | `VLOG(2)` | `VLOG(1)` |
| `LOG_INFO` | `VLOG(1)` | Native `LOG(INFO)` — enabled by default |
| Logger binding | `auto _ch_log_ptr` (copies `shared_ptr`) | `auto && _ch_log_` (reference, no copy) |
| Null attribution | `(null)` with parentheses | `null` without parentheses: `[null]` |
| Minloglevel gate | Only `VLOG_IS_ON` check | Three-gate for VLOG: strip + minloglevel + `VLOG_IS_ON` |
| Native severity gate | Unconditional (no minloglevel check) | Two-gate: strip + minloglevel |
| tryLogCurrentException failure | Silent `catch (...)` swallow | Emergency `stderr` write with fixed allocation-free diagnostic |
| tryLogCurrentException overloads | `LoggerPtr`, `const char*`, `std::string` | `LoggerPtr` and `const char*` only — no `std::string` name overload |
| No-active-exception handling | Not addressed | Fixed `LOG(ERROR)` misuse diagnostic |
| Test emitter injection | Not designed | `detail::ExceptionEmitter` function pointer with RAII swap |
| Compile-strip proof | Not present | Separate `CompileStripProbe` binary with `GOOGLE_STRIP_LOG=1` |
| Ownership-copy test | Not present | `use_count` observation during format argument evaluation |
| Mutation MUT-02 (minloglevel) | Not present | `VlogMinloglevelGate` catches missing `0 >= FLAGS_minloglevel` |
| Mutation MUT-08 (emergency) | Not present | `EmergencyStderr` catches swallowed emergency write |
