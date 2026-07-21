# Task 017B: FileCache Logging and Exception Stack Formatting — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> - **Disposition:** implementation_plan_ready
> - **Task ID:** 017B
> - **Binding design:** `port/design/filecache-task-017-018-joint-design.md` §5
> - **Prerequisite:** Tasks 003–015 accepted; execute only after accepted Task 018
> - **Successor:** Task 019 design begins after 017B acceptance
> - **Execution order:** 017A → 018 → 017B → 019 design
> - **Environment:** `root-oss` (`/root/oss/velox`, branch `filecache`)
> - **Commit policy:** Worker never commits (`worker_commits: false`); Controller commits after independent review (`controller_commits: true`).

**Goal:** Replace the no-op `logger_useful.h` shim (accepted in Task 003) with real lazy logging, real exception formatting, and real exception logging — while preserving the existing `FileCacheLogger`/`LoggerPtr`/`getLogger` shape, the `LOG_TEST` non-evaluation invariant, and the `noexcept` exception-logging guarantee.

**Architecture:** Keep the Task-003 `FileCacheLogger`/`LoggerPtr`/`getLogger` surface and the header-only `LOG_TEST` no-op, but move every non-template exception function out of the header into a new `logger_useful.cpp` translation unit to avoid ODR problems in non-mono builds. The logging macros become real: `LOG_TRACE`/`DEBUG`/`INFO` are lazy `glog` `VLOG` calls guarded by `VLOG_IS_ON`, while `LOG_WARNING`/`ERROR` emit unconditionally with logger-name attribution. Exception formatting and `noexcept` exception logging are declared in the header and defined in the new `.cpp`. The component layout:

```text
logger_useful.h
  ├── FileCacheLogger class (unchanged)
  ├── LoggerPtr = shared_ptr<FileCacheLogger> (unchanged)
  ├── getLogger() factory (unchanged)
  ├── LOG_TEST: empty do-while (unchanged invariant)
  ├── LOG_TRACE/DEBUG/INFO: VLOG_IS_ON guard → lazy fmt → glog VLOG
  ├── LOG_WARNING/ERROR: unconditional → attributed glog LOG(severity)
  ├── detail::chLogFormat() template (header-only)
  └── declarations: getCurrentExceptionMessage, tryLogCurrentException overloads

logger_useful.cpp
  ├── getCurrentExceptionMessage: std::current_exception guard, rethrow/catch chain
  └── tryLogCurrentException: noexcept overloads (LoggerPtr, const char*, std::string)
```

**Tech Stack:** C++20 (Velox baseline), `glog::glog`, `fmt::fmt`, `Folly::folly`, GoogleTest, built via `velox_add_library` + ninja. Details:

| Component | Version/Source |
|-----------|---------------|
| C++ | C++20 (Velox baseline) |
| glog | vcpkg-installed, target `glog::glog` |
| fmt | vcpkg-installed, target `fmt::fmt` |
| Folly | `Folly::folly` (for `folly::makeGuard`, transitively links glog) |
| GTest | `GTest::gtest` + `GTest::gtest_main` |
| Build | CMake via `velox_add_library`; ninja |

## Global Constraints

1. Worker never commits; Controller commits after independent review.
2. Allman-style braces in all C++ code.
3. No C++ sleeps.
4. `LOG_TEST` must never evaluate its arguments (zero-evaluation invariant).
5. `getCurrentExceptionMessage` is `noexcept`; safe outside catch (returns empty).
6. `tryLogCurrentException` is `noexcept`; never replaces the original exception.
7. No broad catch-success fallbacks. Inside `getCurrentExceptionMessage`, internal
   `catch (...)` blocks return deterministic fallback text (e.g. "formatting
   failed"), never silently succeeding with an empty string. The outer
   `catch (...)` in `tryLogCurrentException` is different: it fires only when the
   logging backend itself throws, and swallows solely to uphold `noexcept` and
   preserve the original exception — it makes no diagnostic claim.
8. `##__VA_ARGS__` for empty-varargs portability (GCC/Clang required by Velox).
9. Macro arguments evaluated at most once (captured into a local variable).
10. Expression lifetimes: logger pointer captured before streaming to avoid dangling.
11. Use "exception" not "crash" for logical failures; "process terminates" for `std::terminate`.
12. Source registration must work in both mono and non-mono builds.
13. Execution authorized only after Task 018 is accepted.

---

## Acceptance Checklist

- [ ] All Milestone gates GREEN
- [ ] Mutation evidence RED for every M1–M7
- [ ] Accumulated CTest gate: zero failures across all `velox_ch_*` targets (mono + non-mono)
- [ ] Worker never commits; Controller commits after independent review

---

## 1. Authoritative Sources

| Source | Path | Purpose |
|--------|------|---------|
| Current shim | `velox/ch/Common/logger_useful.h` | No-op shim (Task 003) |
| VeloxException | `velox/common/base/VeloxException.h:205,220,232,236` | `stackTrace`, `message`, `errorCode`, `errorSource` |
| StackTrace | `velox/common/process/StackTrace.h:46` | `const std::string& toString() const` (lazy-init) |
| glog LogSink | `velox/common/time/tests/HierarchicalTimerTest.cpp:32-62` | Verified capture pattern |
| Library CMake | `velox/ch/Common/CMakeLists.txt:74-100` | `velox_add_library` positional source list |
| Non-mono link | `velox/ch/Common/CMakeLists.txt:114-123` | `if(NOT VELOX_MONO_LIBRARY)` link block (comment at 108-113) |
| Test CMake | `velox/ch/Common/tests/CMakeLists.txt:15-28` | `velox_ch_common_test` target |
| Existing tests | `velox/ch/Common/tests/BasicShimsTest.cpp:58-109` | Tests to preserve/replace |
| FileCache callers | `velox/ch/Interpreters/FileCache/FileCache.cpp`, `FileSegment.cpp`, `Metadata.cpp` | `tryLogCurrentException(__PRETTY_FUNCTION__)`, `tryLogCurrentException(getLog(), msg)` |
| Stack flag | `velox/common/base/VeloxException.h:33-34` | `FLAGS_velox_exception_system_stacktrace_enabled` |
| velox_add_library | `CMake/VeloxUtils.cmake:91-213` | Mono: `target_sources(velox PRIVATE ${_sources})` (:114; first invocation `add_library(velox ${_type} ${_sources})` at :124); non-mono: `velox_base_add_library(${TARGET} ${library_type} ${_sources})` (:182) |

---

## 2. File Scope

### New files

| File | Purpose |
|------|---------|
| `velox/ch/Common/logger_useful.cpp` | Non-template exception formatting and logging function definitions |

### Modified files

| File | Change summary |
|------|----------------|
| `velox/ch/Common/logger_useful.h` | Add `#include <glog/logging.h>`, `#include <fmt/format.h>`; keep class/accessors; replace macro section; add `detail::chLogFormat`; declare `getCurrentExceptionMessage` and `tryLogCurrentException` overloads; remove old inline stubs |
| `velox/ch/Common/tests/BasicShimsTest.cpp` | Replace `AllLogMacrosDoNotEvaluateArguments`, `CurrentExceptionMessageRemainsEmptyFirstPhase`, `TryLogCurrentExceptionIsNoOpFirstPhase` with real-behavior tests |
| `velox/ch/Common/CMakeLists.txt` | Add `logger_useful.cpp` to `velox_add_library` source list; add `velox_process` and `glog::glog` to non-mono link block |
| `velox/ch/Common/tests/CMakeLists.txt` | Add `glog::glog` to `velox_ch_common_test` link |

---

## 3. Milestones

### Milestone 1: Exception Formatting

**Goal:** `getCurrentExceptionMessage` correctly handles VeloxException (with stack),
std::exception, unknown, and the no-active-exception case.

#### 3.1 Interface (`logger_useful.h` declaration)

```cpp
/// Formats the currently-handled exception. Safe to call at any point:
/// returns empty string when no exception is active.
/// Never throws (noexcept).
std::string getCurrentExceptionMessage(bool withStackTrace = false) noexcept;
```

#### 3.2 Implementation (`logger_useful.cpp`)

```cpp
#include "velox/ch/Common/logger_useful.h"

#include <exception>
#include <string>

#include <fmt/format.h>
#include <glog/logging.h>

#include "velox/common/base/VeloxException.h"
#include "velox/common/process/StackTrace.h"

namespace facebook::velox::ch
{

std::string getCurrentExceptionMessage(bool withStackTrace) noexcept
{
    std::exception_ptr eptr = std::current_exception();
    if (!eptr)
    {
        return {};
    }

    try
    {
        std::rethrow_exception(eptr);
    }
    catch (const velox::VeloxException& e)
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
                const auto* st = e.stackTrace();
                if (st)
                {
                    const std::string& trace = st->toString();
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
    catch (const std::exception& e)
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

} // namespace facebook::velox::ch
```

**Design notes:**
- `std::current_exception()` returns null outside a catch block — no UB, no
  `std::terminate`. Never uses bare `throw;`.
- VeloxException is caught first (more specific than `std::exception`).
- `stackTrace()` returns `nullptr` when
  `FLAGS_velox_exception_system_stacktrace_enabled` is false (flag controls
  capture at throw-time; we respect whatever was captured).
- Inner `catch (...)` blocks return deterministic fallback text (e.g.
  `"VeloxException (formatting failed)"`), never silently returning empty. This is
  targeted internal containment within a `noexcept` function; the fallback text is
  returned to the caller and logged like any other message.

#### 3.3 TDD Steps

- [ ] **M1-T1** (3 min): Write `ExceptionFormattingTest.VeloxExceptionWithStack`:

```cpp
TEST(ExceptionFormattingTest, VeloxExceptionWithStack)
{
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
        if (FLAGS_velox_exception_system_stacktrace_enabled)
        {
            EXPECT_NE(msg.find("Stack trace"), std::string::npos);
        }
    }
}
```

- [ ] **M1-T2** (2 min): Write `ExceptionFormattingTest.VeloxExceptionWithoutStack`:

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
        EXPECT_EQ(msg.find("Stack trace"), std::string::npos);
    }
}
```

- [ ] **M1-T3** (2 min): Write `ExceptionFormattingTest.StdException`:

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

- [ ] **M1-T4** (2 min): Write `ExceptionFormattingTest.UnknownException`:

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

- [ ] **M1-T5** (2 min): Write `ExceptionFormattingTest.NoActiveException`:

```cpp
TEST(ExceptionFormattingTest, NoActiveException)
{
    auto msg = getCurrentExceptionMessage(true);
    EXPECT_TRUE(msg.empty());
}
```

- [ ] **M1-I1** (5 min): Create `logger_useful.cpp` with body from §3.2. Register in CMake (§5.1).
- [ ] **M1-G1** (3 min): Build `velox_ch_common_test` (mono). All M1 tests GREEN.

```bash
ninja -C _build/debug velox_ch_common_test > _build/debug/build_017b_m1.log 2>&1
cd _build/debug && ctest -R velox_ch_common_test --output-on-failure \
  > ctest_017b_m1.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug/build_017b_m1.log` and `_build/debug/ctest_017b_m1.log` and return
only a concise summary — build success/failure with any compiler or linker errors
quoted verbatim, and per-test PASS/FAIL for the M1 `ExceptionFormattingTest`
cases. Do not read the logs inline.

#### 3.4 Mutations

| ID | Mutation | Affected test | Expected RED |
|----|----------|---------------|--------------|
| M1 | Remove `std::current_exception()` guard, use bare `throw;` | `NoActiveException` | Process terminates (`std::terminate` called outside catch) |
| M2 | VeloxException catch → `return {}` | `VeloxExceptionWithStack`, `VeloxExceptionWithoutStack` | `string::find` fails |
| M3 | std::exception catch → `return {}` | `StdException` | `string::find` fails |
| M4 | unknown catch → `return {}` | `UnknownException` | `string::find` fails |

---

### Milestone 2: Lazy/Attributed Logging

**Goal:** `LOG_TRACE`/`DEBUG`/`INFO` are lazy (args not evaluated at default
`FLAGS_v=0`); `LOG_WARNING`/`ERROR` always emit with logger-name attribution.
`LOG_TEST` remains a no-op that never evaluates arguments.

#### 3.5 Header changes (`logger_useful.h`)

Milestone 2 adds the logging includes, the `detail::chLogFormat` helper, and the
real macro definitions. Apply three edits to the current shim (live line numbers
from `velox/ch/Common/logger_useful.h`, branch `filecache`):

**Edit 1 — add two includes** to the existing include block, immediately after
`#include <utility>` (`logger_useful.h:21`):

```cpp
#include <fmt/format.h>
#include <glog/logging.h>
```

**Edit 2 — insert the `detail` helper** immediately after the closing brace of
`namespace facebook::velox::ch` (`logger_useful.h:62`) and before the macro
block. This is a new, self-contained block:

```cpp
namespace facebook::velox::ch::detail
{

inline std::string chLogFormat(std::string_view msg)
{
    return std::string(msg);
}

template <typename... Args>
std::string chLogFormat(fmt::format_string<Args...> fmtStr, Args&&... args)
{
    return fmt::format(fmtStr, std::forward<Args>(args)...);
}

} // namespace facebook::velox::ch::detail
```

**Edit 3 — replace the six no-op shim macros** (`logger_useful.h:64-72`, i.e.
`LOG_TEST`/`LOG_TRACE`/`LOG_DEBUG`/`LOG_INFO`/`LOG_WARNING`/`LOG_ERROR`) in their
entirety with:

```cpp
#define LOG_TEST(...) \
    do               \
    {                \
    } while (false)

#define CH_LOG_IMPL(logger, vlog_level, fmt_str, ...)          \
    do                                                          \
    {                                                           \
        if (VLOG_IS_ON(vlog_level))                            \
        {                                                       \
            auto _ch_log_ptr = (logger);                       \
            VLOG(vlog_level)                                    \
                << "[" << (_ch_log_ptr ? _ch_log_ptr->name()   \
                                      : std::string("(null)")) \
                << "] "                                         \
                << ::facebook::velox::ch::detail::              \
                       chLogFormat(fmt_str, ##__VA_ARGS__);     \
        }                                                       \
    } while (false)

#define CH_LOG_SEVERITY_IMPL(logger, severity, fmt_str, ...)   \
    do                                                          \
    {                                                           \
        auto _ch_log_ptr = (logger);                           \
        LOG(severity)                                           \
            << "[" << (_ch_log_ptr ? _ch_log_ptr->name()       \
                                  : std::string("(null)"))     \
            << "] "                                             \
            << ::facebook::velox::ch::detail::                  \
                   chLogFormat(fmt_str, ##__VA_ARGS__);         \
    } while (false)

#define LOG_TRACE(logger, fmt_str, ...) \
    CH_LOG_IMPL(logger, 3, fmt_str, ##__VA_ARGS__)

#define LOG_DEBUG(logger, fmt_str, ...) \
    CH_LOG_IMPL(logger, 2, fmt_str, ##__VA_ARGS__)

#define LOG_INFO(logger, fmt_str, ...) \
    CH_LOG_IMPL(logger, 1, fmt_str, ##__VA_ARGS__)

#define LOG_WARNING(logger, fmt_str, ...) \
    CH_LOG_SEVERITY_IMPL(logger, WARNING, fmt_str, ##__VA_ARGS__)

#define LOG_ERROR(logger, fmt_str, ...) \
    CH_LOG_SEVERITY_IMPL(logger, ERROR, fmt_str, ##__VA_ARGS__)
```

**Design notes:**
- `logger` is evaluated exactly once (captured into `_ch_log_ptr`).
- `VLOG_IS_ON` short-circuits: at default `FLAGS_v=0`, TRACE/DEBUG/INFO never
  evaluate `fmt_str` or varargs.
- `##__VA_ARGS__` handles zero variadic args (GCC/Clang extension, required by Velox).
- `LOG(WARNING)` and `LOG(ERROR)` always evaluate — this is intentional for
  production visibility. The existing `AllLogMacrosDoNotEvaluateArguments` test
  (which asserts WARNING/ERROR don't evaluate) must be **replaced**, not preserved.
- glog's `LOG(severity)` includes file/line/severity prefix; we add `[name]` attribution.

#### 3.6 TDD Steps

- [ ] **M2-T1** (3 min): Write `LoggerUsefulTest.LogTestNeverEvaluates` (preserves the
  invariant from the existing `ArgumentsAreNotEvaluated` test):

```cpp
TEST(LoggerUsefulTest, LogTestNeverEvaluates)
{
    int evaluated = 0;
    auto logger = getLogger("test");
    LOG_TEST(logger, "value {}", ++evaluated);
    EXPECT_EQ(evaluated, 0);
}
```

- [ ] **M2-T2** (5 min): Write `LoggerUsefulTest.LazyEvaluation` — verify TRACE/DEBUG/INFO
  don't evaluate at `FLAGS_v=0`; WARNING/ERROR do evaluate:

```cpp
TEST(LoggerUsefulTest, LazyEvaluation)
{
    const auto savedV = FLAGS_v;
    FLAGS_v = 0;
    auto restore = folly::makeGuard([&] { FLAGS_v = savedV; });

    int evaluated = 0;
    auto logger = getLogger("test");
    LOG_TEST(logger, "value {}", ++evaluated);
    LOG_TRACE(logger, "value {}", ++evaluated);
    LOG_DEBUG(logger, "value {}", ++evaluated);
    LOG_INFO(logger, "value {}", ++evaluated);
    EXPECT_EQ(evaluated, 0);
    // WARNING/ERROR always emit, so their args ARE evaluated:
    LOG_WARNING(logger, "value {}", ++evaluated);
    EXPECT_EQ(evaluated, 1);
    LOG_ERROR(logger, "value {}", ++evaluated);
    EXPECT_EQ(evaluated, 2);
}
```

- [ ] **M2-T3** (5 min): Write `LoggerUsefulTest.WarningAttributionCaptured` and
  `LoggerUsefulTest.ErrorAttributionCaptured`:

```cpp
namespace
{

/// Test sink capturing glog messages. Verified pattern from
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

    TestLogSink(const TestLogSink&) = delete;
    TestLogSink& operator=(const TestLogSink&) = delete;

    void send(
        google::LogSeverity /*severity*/,
        const char* /*full_filename*/,
        const char* /*base_filename*/,
        int /*line*/,
        const struct ::tm* /*tm_time*/,
        const char* message,
        size_t message_len) override
    {
        captured_ += std::string(message, message_len);
    }

    const std::string& captured() const
    {
        return captured_;
    }

    void clear()
    {
        captured_.clear();
    }

private:
    std::string captured_;
};

} // namespace

TEST(LoggerUsefulTest, WarningAttributionCaptured)
{
    TestLogSink sink;
    auto logger = getLogger("MyComponent");
    LOG_WARNING(logger, "something went wrong: {}", 42);
    EXPECT_NE(sink.captured().find("MyComponent"), std::string::npos)
        << "Logger name must appear in output. Got: " << sink.captured();
    EXPECT_NE(sink.captured().find("something went wrong: 42"), std::string::npos)
        << "Formatted message must appear. Got: " << sink.captured();
}

TEST(LoggerUsefulTest, ErrorAttributionCaptured)
{
    TestLogSink sink;
    auto logger = getLogger("ErrorComp");
    LOG_ERROR(logger, "fatal: {}", "disk full");
    EXPECT_NE(sink.captured().find("ErrorComp"), std::string::npos);
    EXPECT_NE(sink.captured().find("fatal: disk full"), std::string::npos);
}
```

- [ ] **M2-I1** (3 min): Implement macros in header (as shown in §3.5).
- [ ] **M2-G1** (3 min): Build and run. All M2 tests GREEN.

```bash
ninja -C _build/debug velox_ch_common_test > _build/debug/build_017b_m2.log 2>&1
cd _build/debug && ctest -R velox_ch_common_test --output-on-failure \
  > ctest_017b_m2.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug/build_017b_m2.log` and `_build/debug/ctest_017b_m2.log` and return
only a concise summary — build success/failure with any compiler or linker errors
quoted verbatim, and per-test PASS/FAIL for the M2 `LoggerUsefulTest` laziness and
attribution cases. Do not read the logs inline.

**Existing test changes** (the `LoggerUsefulTest` and exception cases in
`BasicShimsTest.cpp:58-109`):
- `ArgumentsAreNotEvaluated` (line 58): **kept** — identical to `LogTestNeverEvaluates`.
- `AllLogMacrosDoNotEvaluateArguments` (line 65): **replaced** by `LazyEvaluation` (M2) —
  the old test asserts WARNING/ERROR don't evaluate, which contradicts the new real behavior.
- `GetLoggerReturnsNonNullWithNameIdentity` (line 78): **preserved unchanged**.
- `CurrentExceptionMessageRemainsEmptyFirstPhase` (line 85): **replaced** by the M1
  `ExceptionFormattingTest` cases (§3.3) — the old test asserts an empty string even with
  an active exception, which the real formatter contradicts. Delete it when M1 lands.
- `TryLogCurrentExceptionIsNoOpFirstPhase` (line 98): **replaced** by the M3
  `ExceptionLoggingTest` cases (§3.10) — the old test asserts no-op behavior, which the
  real logger contradicts. Delete it when M3 lands.

#### 3.7 Mutations

| ID | Mutation | Affected test | Expected RED |
|----|----------|---------------|--------------|
| M5 | Make `LOG_TEST` evaluate: replace its no-op body with `CH_LOG_SEVERITY_IMPL(logger, WARNING, fmt_str, ##__VA_ARGS__)` (unconditional) | `LogTestNeverEvaluates` | `evaluated != 0` |
| M6 | Remove the `if (VLOG_IS_ON(vlog_level))` guard from `CH_LOG_IMPL` | `LazyEvaluation` | `evaluated != 0` at TRACE/DEBUG/INFO |
| M7 | Remove `"[" << _ch_log_ptr->name() << "] "` from `CH_LOG_SEVERITY_IMPL` | `WarningAttributionCaptured`, `ErrorAttributionCaptured` | LogSink find of logger name fails |

---

### Milestone 3: Exception Logging & Call-Site Compatibility

**Goal:** `tryLogCurrentException` overloads are `noexcept`, accept
`LoggerPtr` / `const char*` / `std::string`, never replace the current exception,
handle formatting/logging failure. All existing FileCache call patterns compile.

#### 3.8 Declarations (in `logger_useful.h`, inside namespace)

```cpp
namespace facebook::velox::ch
{

/// Log the current exception through a LoggerPtr. Noexcept, never replaces
/// the original exception being handled.
void tryLogCurrentException(
    LoggerPtr logger,
    const std::string& startOfMessage = "") noexcept;

/// Log the current exception using a function/log name (e.g. __PRETTY_FUNCTION__).
void tryLogCurrentException(
    const char* logName,
    const std::string& startOfMessage = "") noexcept;

/// Overload for std::string name (delegates to const char*).
void tryLogCurrentException(
    const std::string& logName,
    const std::string& startOfMessage = "") noexcept;

} // namespace facebook::velox::ch
```

#### 3.9 Implementation (in `logger_useful.cpp`, appended after `getCurrentExceptionMessage`)

```cpp
void tryLogCurrentException(
    LoggerPtr logger,
    const std::string& startOfMessage) noexcept
{
    try
    {
        std::string msg = getCurrentExceptionMessage(/*withStackTrace=*/true);
        if (!startOfMessage.empty())
        {
            msg = startOfMessage + ": " + msg;
        }
        if (logger)
        {
            LOG(ERROR) << "[" << logger->name() << "] " << msg;
        }
        else
        {
            LOG(ERROR) << msg;
        }
    }
    catch (...)
    {
        // Reached only if the logging backend itself throws (e.g. std::bad_alloc
        // while glog streams the record, or string concatenation runs out of
        // memory). getCurrentExceptionMessage is noexcept and already yields
        // deterministic fallback text, so this is not a formatting failure.
        // Swallowing is unavoidable here: it exists solely to uphold noexcept and
        // preserve the original exception being handled. No log line is emitted on
        // this path and none is claimed.
    }
}

void tryLogCurrentException(
    const char* logName,
    const std::string& startOfMessage) noexcept
{
    try
    {
        std::string msg = getCurrentExceptionMessage(/*withStackTrace=*/true);
        if (!startOfMessage.empty())
        {
            msg = startOfMessage + ": " + msg;
        }
        LOG(ERROR) << "[" << (logName ? logName : "(null)") << "] " << msg;
    }
    catch (...)
    {
        // Logging backend threw — swallowed only to uphold noexcept and preserve
        // the original exception; no log line on this path (see LoggerPtr overload).
    }
}

void tryLogCurrentException(
    const std::string& logName,
    const std::string& startOfMessage) noexcept
{
    tryLogCurrentException(logName.c_str(), startOfMessage);
}
```

**Design notes:**
- All overloads are `noexcept`. `getCurrentExceptionMessage` is itself `noexcept`
  and returns deterministic fallback text (e.g. `"Unknown exception"`,
  `"VeloxException (formatting failed)"`) when its own formatting fails, so `msg`
  is always well-defined: a formatting failure is logged as that fallback text,
  not lost.
- The outer `catch (...)` is reached only if the logging backend itself throws
  (e.g. `std::bad_alloc` while glog streams the record). On that path swallowing
  is unavoidable and exists solely to uphold `noexcept` and preserve the original
  exception being handled. No log line is written on that path, and the plan makes
  no claim that one is — an absent log line is not treated as a diagnostic.
- The `LoggerPtr` overload matches: `tryLogCurrentException(getLog(), "context msg")`
- The `const char*` overload matches: `tryLogCurrentException(__PRETTY_FUNCTION__)`
- The `std::string` overload matches: `tryLogCurrentException(std::string("name"), "")`

#### 3.10 TDD Steps

- [ ] **M3-T1** (3 min): Write `ExceptionLoggingTest.TryLogWithLoggerPtrDoesNotThrow`:

```cpp
TEST(ExceptionLoggingTest, TryLogWithLoggerPtrDoesNotThrow)
{
    try
    {
        VELOX_FAIL("test exception");
    }
    catch (...)
    {
        EXPECT_NO_THROW(tryLogCurrentException(getLogger("Test"), "context"));
    }
}
```

- [ ] **M3-T2** (3 min): Write `ExceptionLoggingTest.TryLogWithNameDoesNotThrow`:

```cpp
TEST(ExceptionLoggingTest, TryLogWithNameDoesNotThrow)
{
    try
    {
        VELOX_FAIL("test exception");
    }
    catch (...)
    {
        EXPECT_NO_THROW(tryLogCurrentException(__PRETTY_FUNCTION__));
        EXPECT_NO_THROW(tryLogCurrentException(std::string("func"), "msg"));
    }
}
```

- [ ] **M3-T3** (3 min): Write `ExceptionLoggingTest.OriginalExceptionPreserved`:

```cpp
TEST(ExceptionLoggingTest, OriginalExceptionPreserved)
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
            throw; // re-throw — must still be the same exception
        }
    }
    catch (const velox::VeloxException& e)
    {
        caughtOriginal =
            (e.message().find("original error") != std::string::npos);
    }
    EXPECT_TRUE(caughtOriginal);
}
```

- [ ] **M3-T4** (3 min): Write `ExceptionLoggingTest.LogOutputContainsExceptionMessage`:

```cpp
TEST(ExceptionLoggingTest, LogOutputContainsExceptionMessage)
{
    TestLogSink sink;
    try
    {
        throw std::runtime_error("captured failure");
    }
    catch (...)
    {
        tryLogCurrentException(getLogger("CaptureTest"), "during op");
    }
    EXPECT_NE(sink.captured().find("CaptureTest"), std::string::npos);
    EXPECT_NE(sink.captured().find("captured failure"), std::string::npos);
    EXPECT_NE(sink.captured().find("during op"), std::string::npos);
}
```

- [ ] **M3-T5** (2 min): Write `ExceptionLoggingTest.UnknownExceptionLoggedAsFallback`
  — a non-`std::exception` throw is logged with deterministic fallback text (never
  swallowed), and every overload stays `noexcept`:

```cpp
TEST(ExceptionLoggingTest, UnknownExceptionLoggedAsFallback)
{
    TestLogSink sink;
    try
    {
        throw 42;
    }
    catch (...)
    {
        EXPECT_NO_THROW(tryLogCurrentException(getLogger("X")));
        EXPECT_NO_THROW(tryLogCurrentException(__PRETTY_FUNCTION__));
        EXPECT_NO_THROW(tryLogCurrentException(std::string("Y"), "ctx"));
    }
    // getCurrentExceptionMessage returns the deterministic "Unknown exception"
    // fallback for a non-std throw, and that text IS logged — it is not swallowed.
    EXPECT_NE(sink.captured().find("Unknown exception"), std::string::npos)
        << "Unknown exception must be logged as fallback text. Got: "
        << sink.captured();
}
```

- [ ] **M3-T6** (2 min): Write `ExceptionLoggingTest.FunctionNameOverloadMatchesCallers`:

```cpp
TEST(ExceptionLoggingTest, FunctionNameOverloadMatchesCallers)
{
    // Verify the exact call patterns used in FileCache.cpp/FileSegment.cpp compile:
    TestLogSink sink;
    auto log = getLogger("FileCache");
    try
    {
        VELOX_FAIL("simulated error");
    }
    catch (...)
    {
        // Pattern from FileCache.cpp:458,509,1863
        tryLogCurrentException(__PRETTY_FUNCTION__);
        // Pattern from FileCache.cpp:1555,1667
        tryLogCurrentException(log, "Error in background thread");
    }
    EXPECT_NE(sink.captured().find("FileCache"), std::string::npos);
    EXPECT_NE(sink.captured().find("simulated error"), std::string::npos);
}
```

- [ ] **M3-I1** (3 min): Implement overloads in `logger_useful.cpp` (§3.9).
- [ ] **M3-G1** (3 min): Build and run. All M3 tests GREEN.

```bash
ninja -C _build/debug velox_ch_common_test > _build/debug/build_017b_m3.log 2>&1
cd _build/debug && ctest -R velox_ch_common_test --output-on-failure \
  > ctest_017b_m3.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug/build_017b_m3.log` and `_build/debug/ctest_017b_m3.log` and return
only a concise summary — build success/failure with any compiler or linker errors
quoted verbatim, and per-test PASS/FAIL for the M3 `ExceptionLoggingTest` cases.
Do not read the logs inline.

- [ ] **M3-C1** (2 min): Build all FileCache targets (mono) to verify call-site compatibility:

```bash
ninja -C _build/debug velox_ch_filecache > _build/debug/build_017b_m3_callers.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug/build_017b_m3_callers.log` and return only a concise summary —
whether `velox_ch_filecache` compiled and linked, with any errors quoted verbatim
(especially overload-resolution failures at the `tryLogCurrentException` call
sites). Do not read the log inline.

No source changes required in callers:
- `tryLogCurrentException(__PRETTY_FUNCTION__)` → `const char*` overload
- `tryLogCurrentException(getLog(), "message")` → `LoggerPtr` overload
- `getCurrentExceptionMessage(true)` → real formatting
- `LOG_TEST(log, "fmt", args...)` → no-op macro (unchanged)
- `LOG_TRACE`/`DEBUG`/`INFO`/`WARNING`/`ERROR(log, "fmt", args...)` → real macros

---

### Milestone 4: CMake & Accumulated Gate

**Goal:** All existing `velox_ch_*` targets build and pass in both mono and
non-mono configurations. Dynamic target discovery.

#### 5.1 CMake Changes

**`velox/ch/Common/CMakeLists.txt`** — add `logger_useful.cpp` to the
`velox_add_library` source list (works for both mono and non-mono):

```cmake
velox_add_library(
  velox_ch_filecache
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

**Rationale (verified against `CMake/VeloxUtils.cmake:91-213`):** in mono mode
(`VELOX_MONO_LIBRARY` set) `velox_add_library` forwards its positional sources to
`target_sources(velox PRIVATE ${_sources})` when the `velox` target already exists
(`VeloxUtils.cmake:114`), or to `add_library(velox ${_type} ${_sources})` on the
first invocation (`:124`), then exposes `velox_ch_filecache` as an alias of
`velox` (`:178`). In non-mono mode it forwards the same sources to
`velox_base_add_library(${TARGET} ${library_type} ${_sources})`, creating a
standalone library (`:182`). Listing `logger_useful.cpp` in this positional source
list therefore compiles it in BOTH configurations. The draft's previously-considered
approach (`if(NOT VELOX_MONO_LIBRARY) target_sources(velox_ch_filecache PRIVATE
logger_useful.cpp)`) would have DROPPED the source from the mono build, because that
guard is false when `VELOX_MONO_LIBRARY` is set.

**Non-mono link block** — extend the existing `if(NOT VELOX_MONO_LIBRARY)` block
(`velox/ch/Common/CMakeLists.txt:114-123`) with `velox_process` and `glog::glog`,
and extend its explanatory comment (`:108-113`) to mention the two new deps. The
block in full becomes:

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

**`velox_process`** is needed for `StackTrace::toString()` called in
`getCurrentExceptionMessage`. **`glog::glog`** is needed for `LOG(ERROR)` in
`tryLogCurrentException` and `VLOG`/`VLOG_IS_ON` in the macros. In mono builds,
both are already linked transitively through the mono `velox` target.

**`velox/ch/Common/tests/CMakeLists.txt`** — add `glog::glog` to test link:

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

#### 5.2 Build & Test Steps

- [ ] **M4-B1** (5 min): Build ALL `velox_ch_*` targets (mono):

```bash
targets=$(grep -rh "^add_executable" velox/ch/*/tests/CMakeLists.txt \
          velox/ch/*/*/tests/CMakeLists.txt 2>/dev/null | \
          sed 's/add_executable(\([^ )]*\).*/\1/' | sort -u)
ninja -C _build/debug $targets > _build/debug/build_017b_mono_all.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug/build_017b_mono_all.log` and return only a concise summary —
per-target build success/failure across all discovered `velox_ch_*` targets, with
any compiler or linker errors quoted verbatim. Do not read the log inline.

- [ ] **M4-B2** (3 min): Run accumulated CTest (mono):

```bash
cd _build/debug && ctest -R "velox_ch_" --output-on-failure \
  > ctest_017b_mono.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug/ctest_017b_mono.log` and return only a concise summary — total
tests, per-test PASS/FAIL, and the count of failures (the gate requires zero). Do
not read the log inline.

**GREEN gate**: zero failures.

- [ ] **M4-B3** (5 min): Configure and build non-mono:

```bash
cmake -B _build/debug-nonmono -S . \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVELOX_MONO_LIBRARY=OFF \
  -GNinja \
  > _build/debug-nonmono/cmake_017b.log 2>&1

targets=$(grep -rh "^add_executable" velox/ch/*/tests/CMakeLists.txt \
          velox/ch/*/*/tests/CMakeLists.txt 2>/dev/null | \
          sed 's/add_executable(\([^ )]*\).*/\1/' | sort -u)
ninja -C _build/debug-nonmono $targets > _build/debug-nonmono/build_017b_all.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug-nonmono/cmake_017b.log` and `_build/debug-nonmono/build_017b_all.log`
and return only a concise summary — whether CMake configured with
`VELOX_MONO_LIBRARY=OFF` and whether every `velox_ch_*` target built, with any
configure/compiler/linker errors quoted verbatim. Do not read the logs inline.

- [ ] **M4-B4** (3 min): Run accumulated CTest (non-mono):

```bash
cd _build/debug-nonmono && ctest -R "velox_ch_" --output-on-failure \
  > ctest_017b_nonmono.log 2>&1
```

**Log analysis (required):** dispatch a `task` subagent to read
`_build/debug-nonmono/ctest_017b_nonmono.log` and return only a concise summary —
total tests, per-test PASS/FAIL, and the count of failures (the gate requires
zero). Do not read the log inline.

**GREEN gate**: zero failures.

---

## 4. RED/GREEN/Mutation Outcome Matrix

| ID | Mutation | Affected test(s) | Expected RED |
|----|----------|-------------------|--------------|
| M1 | Remove `std::current_exception()` guard, use bare `throw;` | `NoActiveException` | Process terminates (`std::terminate`) |
| M2 | VeloxException catch → `return {}` | `VeloxExceptionWithStack`, `VeloxExceptionWithoutStack` | `string::find` assertion fails |
| M3 | std::exception catch → `return {}` | `StdException` | `string::find` assertion fails |
| M4 | unknown catch → `return {}` | `UnknownException` | `string::find` assertion fails |
| M5 | `LOG_TEST` evaluates args: replace its no-op body with `CH_LOG_SEVERITY_IMPL(logger, WARNING, fmt_str, ##__VA_ARGS__)` (unconditional) | `LogTestNeverEvaluates` | `evaluated != 0` |
| M6 | Remove the `if (VLOG_IS_ON(vlog_level))` guard from `CH_LOG_IMPL` | `LazyEvaluation` | `evaluated != 0` at TRACE/DEBUG/INFO |
| M7 | Remove `"[" << _ch_log_ptr->name() << "] "` from `CH_LOG_SEVERITY_IMPL` | `WarningAttributionCaptured`, `ErrorAttributionCaptured` | LogSink find of logger name fails |

All mutations produce a real test failure (not structural inspection).

---

## 5. Required Test File Includes

The test file (`BasicShimsTest.cpp`) needs these additional includes for the new tests:

```cpp
#include <folly/ScopeGuard.h>
#include <glog/logging.h>

#include "velox/common/base/VeloxException.h"
```

The `TestLogSink` class (§3.6) is defined once in an anonymous namespace and
shared by Milestone 2 and Milestone 3 tests within the same file.

---

## 6. VeloxException API Reference (from live source)

Verified from `velox/common/base/VeloxException.h` (filecache branch):
- `const process::StackTrace* stackTrace() const` (line 205) — returns pointer; null when stack capture was disabled at throw-time
- `const std::string& message() const` (line 220) — the formatted message
- `const std::string& errorCode() const` (line 232) — e.g. `"INVALID_STATE"`
- `const std::string& errorSource() const` (line 236) — e.g. `"RUNTIME"`, `"USER"`
- `const char* what() const noexcept override` — std::exception interface

Verified from `velox/common/process/StackTrace.h` (line 46):
- `const std::string& toString() const` — symbolic stack; returns const ref, lazy-initialized on first call

Flags (VeloxException.h:33-34):
- `DECLARE_bool(velox_exception_system_stacktrace_enabled)` — controls stack capture for RUNTIME errors
- `DECLARE_bool(velox_exception_user_stacktrace_enabled)` — controls stack capture for USER errors

Stack inclusion rule: `getCurrentExceptionMessage(true)` includes the stack only
when `stackTrace()` returns non-null AND `toString()` is non-empty. The flags
control capture at throw-time; we never override them at format-time.

---

## 7. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| `getCurrentExceptionMessage` called outside catch block | `std::current_exception()` guard returns empty — no UB, no terminate |
| glog linker errors in mono build | glog is already linked transitively via Folly; explicit dep is belt-and-suspenders for non-mono |
| `##__VA_ARGS__` portability | GCC and Clang both support this; Velox requires one of these compilers |
| `StackTrace::toString` expensive on first call | Only invoked when `withStackTrace=true` and pointer is non-null; lazy-init is StackTrace's responsibility |
| `FLAGS_v` mutation from other tests | Each test saves/restores `FLAGS_v` with `folly::makeGuard` scope guard |
| `google::LogSink` thread safety | Tests are single-threaded; sink lives on stack with deterministic lifetime |
| Non-mono ODR violations from inline functions | Non-template function bodies moved to `.cpp`; only templates and macros remain in header |
| Existing test `AllLogMacrosDoNotEvaluateArguments` conflicts | Explicitly replaced by `LazyEvaluation` which tests the correct new semantics |
| Macro name collision with system `LOG_ERROR`/`LOG_WARNING` | glog defines these as different macros (`LOG(ERROR)` etc.); our macros take a logger as first arg so overload resolution is unambiguous at the preprocessor level |

---

## 8. Worker Protocol

- Worker never stages or commits; Controller commits after independent task review.
- Execution is authorized only after Task 018 is accepted.
- Execution order: 017A → 018 → **017B** → 019 design.
- After Task 017B acceptance, Task 019 design may begin.
- Every redirected build/test log (`ninja`/`ctest`/`cmake` output captured to a
  `.log` file) is analyzed by a `task` subagent that returns only a concise
  summary; the Worker never pastes full build/test logs into its context or the
  result receipt.
- Result receipt: `port/task/result/017b-filecache-logging-exception-stack-result.md`
