// contracts_runtime.cpp - Reference implementation of the ABI runtime.
//
// Implements the chain-walking field lookup, override block mechanism,
// all 34 entry points, and a sample std::contracts::contract_violation.
//
// In a production implementation:
//   - __cxa_find_field and entry points live in the C++ runtime
//     (libc++abi / libsupc++)
//   - contract_violation is defined by the standard library
//     (libc++ / libstdc++) with the same chain-pointer layout

#include "contracts_abi.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <exception>

using namespace __cxxabiv1;

// =====================================================================
// std::source_location (matching libc++/libstdc++ layout)
// =====================================================================

namespace std {

class source_location {
    struct __impl {
        const char* _M_file_name;
        const char* _M_function_name;
        unsigned _M_line;
        unsigned _M_column;
    };

    const __impl* __ptr_ = nullptr;

public:
    static source_location __create_from_pointer(const void* p) {
        source_location loc;
        loc.__ptr_ = static_cast<const __impl*>(p);
        return loc;
    }

    constexpr source_location() noexcept = default;

    constexpr uint_least32_t line() const noexcept {
        return __ptr_ ? __ptr_->_M_line : 0;
    }
    constexpr uint_least32_t column() const noexcept {
        return __ptr_ ? __ptr_->_M_column : 0;
    }
    constexpr const char* file_name() const noexcept {
        return __ptr_ ? __ptr_->_M_file_name : "";
    }
    constexpr const char* function_name() const noexcept {
        return __ptr_ ? __ptr_->_M_function_name : "";
    }
};

} // namespace std

// =====================================================================
// std::contracts types
// =====================================================================

namespace std { namespace contracts {

enum class assertion_kind { pre, post, assert_ };
enum class evaluation_semantic { ignore, observe, enforce, quick_enforce };
enum class detection_mode { predicate_false, evaluation_exception };

// contract_violation is a thin wrapper around the chain pointer.
// All accessors walk the chain to find field values.
class contract_violation {
    const __cxa_contract_data_block* __chain_;

    friend void __cxa_cv_dispatch(const __cxa_contract_data_block*,
                                  uint8_t, bool);

public:
    explicit contract_violation(const __cxa_contract_data_block* chain)
        : __chain_(chain) {}

    contract_violation(const contract_violation&) = delete;
    contract_violation& operator=(const contract_violation&) = delete;

    assertion_kind kind() const noexcept {
        auto v = __cxa_find_field_value<uint8_t>(
            __chain_, CXA_FIELD_ASSERTION_KIND, 0);
        switch (v) {
        case CXA_AK_PRE:  return assertion_kind::pre;
        case CXA_AK_POST: return assertion_kind::post;
        default:           return assertion_kind::assert_;
        }
    }

    evaluation_semantic semantic() const noexcept {
        auto v = __cxa_find_field_value<uint8_t>(
            __chain_, CXA_FIELD_EVALUATION_SEMANTIC, 0);
        switch (v) {
        case CXA_ES_IGNORE:        return evaluation_semantic::ignore;
        case CXA_ES_OBSERVE:       return evaluation_semantic::observe;
        case CXA_ES_ENFORCE:       return evaluation_semantic::enforce;
        case CXA_ES_QUICK_ENFORCE: return evaluation_semantic::quick_enforce;
        default:                    return evaluation_semantic::enforce;
        }
    }

    detection_mode detection_mode_() const noexcept {
        auto v = __cxa_find_field_value<uint8_t>(
            __chain_, CXA_FIELD_DETECTION_MODE, 0);
        switch (v) {
        case CXA_DM_EVALUATION_EXCEPTION:
            return detection_mode::evaluation_exception;
        default:
            return detection_mode::predicate_false;
        }
    }

    const char* comment() const noexcept {
        auto p = __cxa_find_field_ptr<const char*>(
            __chain_, CXA_FIELD_COMMENT);
        return p ? *p : "";
    }

    const char* message() const noexcept {
        auto p = __cxa_find_field_ptr<const char*>(
            __chain_, CXA_FIELD_MESSAGE);
        return p ? *p : "";
    }

    std::source_location location() const noexcept {
        auto p = __cxa_find_field_ptr<__cxa_source_location>(
            __chain_, CXA_FIELD_SOURCE_LOCATION);
        if (p)
            return std::source_location::__create_from_pointer(p);
        return {};
    }

    bool is_terminating() const noexcept {
        auto sem = __cxa_find_field_value<uint8_t>(
            __chain_, CXA_FIELD_EVALUATION_SEMANTIC, 0);
        return sem == CXA_ES_ENFORCE || sem == CXA_ES_QUICK_ENFORCE;
    }
};

void invoke_default_contract_violation_handler(const contract_violation&);

}} // namespace std::contracts

// =====================================================================
// Chain-walking field lookup (specification S5.4)
// =====================================================================

namespace __cxxabiv1 {

const void*
__cxa_find_field(const __cxa_contract_data_block* chain,
                 const uint8_t* ids,
                 uint8_t num_ids)
{
    for (auto* block = chain; block; block = block->next)
    {
        auto* desc = block->descriptor;
        if (!desc)
            continue;
        auto* fdata = desc->data();
        for (uint8_t i = 0; i < desc->num_entries; ++i)
            for (uint8_t j = 0; j < num_ids; ++j)
                if (desc->field_ids[i] == ids[j])
                {
                    if (desc->field_ids[i] >= CXA_FIELD_EXTENDED)
                        return fdata[i].pointer;
                    auto off = fdata[i].offset;
                    return reinterpret_cast<const char*>(block) + off;
                }
    }
    return nullptr;
}

} // namespace __cxxabiv1

// =====================================================================
// Override descriptor table (specification S4.3)
// =====================================================================

namespace {

// Contiguous descriptor: header, field_ids, padding, data.
struct __override_desc_t
{
    uint8_t header;
    uint8_t num_entries;
    uint8_t field_ids[3];
    uint8_t padding[3];
    __cxa_descriptor_data_t data[3];
};

// The override data block layout:
//   offset  0: descriptor pointer  (8 bytes on 64-bit)
//   offset  8: next pointer        (8 bytes)
//   offset 16: kind (uint8_t)
//   offset 17: semantic (uint8_t)
//   offset 18: mode (uint8_t)
static constexpr __override_desc_t __override_desc = {
    (1u << 4) | CXA_VENDOR_GENERIC,  // version=1, vendor=GENERIC
    3,
    { CXA_FIELD_ASSERTION_KIND, CXA_FIELD_EVALUATION_SEMANTIC,
      CXA_FIELD_DETECTION_MODE },
    { 0, 0, 0 },
    {
        { 2 * sizeof(void*) },      // kind at offset 16
        { 2 * sizeof(void*) + 1 },  // semantic at offset 17
        { 2 * sizeof(void*) + 2 },  // mode at offset 18
    }
};

struct __override_block_t
{
    const __cxa_descriptor_table_t*  descriptor;
    const __cxa_contract_data_block* next;
    uint8_t kind;
    uint8_t semantic;
    uint8_t mode;
};

} // anonymous namespace

// =====================================================================
// Global handler (weak symbol, user-replaceable)
// =====================================================================

__attribute__((weak))
void handle_contract_violation(
    const std::contracts::contract_violation& cv)
{
    const char* kind_str = "unknown";
    switch (cv.kind()) {
    case std::contracts::assertion_kind::pre:     kind_str = "precondition"; break;
    case std::contracts::assertion_kind::post:    kind_str = "postcondition"; break;
    case std::contracts::assertion_kind::assert_: kind_str = "assertion"; break;
    }

    const char* semantic_str = "unknown";
    switch (cv.semantic()) {
    case std::contracts::evaluation_semantic::observe:       semantic_str = "observe"; break;
    case std::contracts::evaluation_semantic::enforce:       semantic_str = "enforce"; break;
    case std::contracts::evaluation_semantic::quick_enforce: semantic_str = "quick_enforce"; break;
    case std::contracts::evaluation_semantic::ignore:        semantic_str = "ignore"; break;
    }

    auto loc = cv.location();
    std::fprintf(stderr,
        "contract violation: %s '%s' failed [%s]\n"
        "  at %s:%u in %s\n",
        kind_str, cv.comment(), semantic_str,
        loc.file_name(), loc.line(), loc.function_name());
}

namespace std { namespace contracts {

void invoke_default_contract_violation_handler(
    const contract_violation& cv)
{
    ::handle_contract_violation(cv);
}

}} // namespace std::contracts

// =====================================================================
// Core dispatch (specification S6.1)
// =====================================================================

static void
__cxa_cv_dispatch(const __cxa_contract_data_block* chain,
                  uint8_t semantic,
                  bool is_noexcept)
{
    std::contracts::contract_violation cv{chain};

    // Local handler dispatch.
    auto handler_fn = __cxa_find_field_value<__cxa_local_handler_fn_t>(
        chain, CXA_FIELD_LOCAL_HANDLER, nullptr);
    auto label_ptr = __cxa_find_field_value<const void*>(
        chain, CXA_FIELD_LABEL_PTR, nullptr);

    auto do_dispatch = [&]()
    {
        if (handler_fn)
        {
            int result = handler_fn(label_ptr, &cv);
            if (result == 1)
                return;
        }

        ::handle_contract_violation(cv);
    };

    if (is_noexcept)
    {
        // A handler that throws at a noexcept entry point terminates via
        // std::terminate() (the exception is still active).
        try { do_dispatch(); }
        catch (...) { std::terminate(); }
    }
    else
        do_dispatch();

    // Post-handler action: an enforced violation whose handler returns
    // normally is terminated via std::abort() (not std::terminate()), so a
    // user-installed std::terminate handler cannot alter contract-termination.
    if (semantic == CXA_ES_ENFORCE)
        std::abort();
}

// Helper: build an override block on the stack and dispatch.
static void
__dispatch_with_override(void* data,
                         uint8_t kind,
                         uint8_t semantic,
                         uint8_t mode,
                         bool is_noexcept)
{
    __override_block_t ovr;
    ovr.descriptor =
        reinterpret_cast<const __cxa_descriptor_table_t*>(&__override_desc);
    ovr.next = static_cast<const __cxa_contract_data_block*>(data);
    ovr.kind = kind;
    ovr.semantic = semantic;
    ovr.mode = mode;
    __cxa_cv_dispatch(
        reinterpret_cast<const __cxa_contract_data_block*>(&ovr),
        semantic, is_noexcept);
}

// =====================================================================
// Entry points (extern "C", specification S4)
// =====================================================================

// Universal entry points.
extern "C" void
__cxa_contract_violation(void* data)
{
    auto* chain = static_cast<const __cxa_contract_data_block*>(data);
    uint8_t sem = __cxa_find_field_value<uint8_t>(
        chain, CXA_FIELD_EVALUATION_SEMANTIC, CXA_ES_UNSPECIFIED);
    __cxa_cv_dispatch(chain, sem, false);
}

extern "C" void
__cxa_contract_violation_noexcept(void* data) noexcept
{
    auto* chain = static_cast<const __cxa_contract_data_block*>(data);
    uint8_t sem = __cxa_find_field_value<uint8_t>(
        chain, CXA_FIELD_EVALUATION_SEMANTIC, CXA_ES_UNSPECIFIED);
    __cxa_cv_dispatch(chain, sem, true);
}

// Specialized: observe (returns after handler).
#define CXA_OBSERVE(kind_name, kind_val, mode_name, mode_val)           \
extern "C" void                                                         \
__cxa_contract_violation_##kind_name##_observe_##mode_name(void* data)   \
{                                                                       \
    __dispatch_with_override(                                           \
        data, kind_val, CXA_ES_OBSERVE, mode_val, false);              \
}                                                                       \
extern "C" void                                                         \
__cxa_contract_violation_##kind_name##_observe_##mode_name##_noexcept(   \
    void* data) noexcept                                                \
{                                                                       \
    __dispatch_with_override(                                           \
        data, kind_val, CXA_ES_OBSERVE, mode_val, true);               \
}

CXA_OBSERVE(pre,          CXA_AK_PRE,          pf, CXA_DM_PREDICATE_FALSE)
CXA_OBSERVE(pre,          CXA_AK_PRE,          ex, CXA_DM_EVALUATION_EXCEPTION)
CXA_OBSERVE(post,         CXA_AK_POST,         pf, CXA_DM_PREDICATE_FALSE)
CXA_OBSERVE(post,         CXA_AK_POST,         ex, CXA_DM_EVALUATION_EXCEPTION)
CXA_OBSERVE(assert,       CXA_AK_ASSERT,       pf, CXA_DM_PREDICATE_FALSE)
CXA_OBSERVE(assert,       CXA_AK_ASSERT,       ex, CXA_DM_EVALUATION_EXCEPTION)
CXA_OBSERVE(post_capture, CXA_AK_POST_CAPTURE, pf, CXA_DM_PREDICATE_FALSE)
CXA_OBSERVE(post_capture, CXA_AK_POST_CAPTURE, ex, CXA_DM_EVALUATION_EXCEPTION)

#undef CXA_OBSERVE

// Specialized: enforce ([[noreturn]]).
#define CXA_ENFORCE(kind_name, kind_val, mode_name, mode_val)           \
extern "C" [[noreturn]] void                                            \
__cxa_contract_violation_##kind_name##_enforce_##mode_name(void* data)   \
{                                                                       \
    __dispatch_with_override(                                           \
        data, kind_val, CXA_ES_ENFORCE, mode_val, false);              \
    __builtin_unreachable();                                            \
}                                                                       \
extern "C" [[noreturn]] void                                            \
__cxa_contract_violation_##kind_name##_enforce_##mode_name##_noexcept(   \
    void* data) noexcept                                                \
{                                                                       \
    __dispatch_with_override(                                           \
        data, kind_val, CXA_ES_ENFORCE, mode_val, true);               \
    __builtin_unreachable();                                            \
}

CXA_ENFORCE(pre,          CXA_AK_PRE,          pf, CXA_DM_PREDICATE_FALSE)
CXA_ENFORCE(pre,          CXA_AK_PRE,          ex, CXA_DM_EVALUATION_EXCEPTION)
CXA_ENFORCE(post,         CXA_AK_POST,         pf, CXA_DM_PREDICATE_FALSE)
CXA_ENFORCE(post,         CXA_AK_POST,         ex, CXA_DM_EVALUATION_EXCEPTION)
CXA_ENFORCE(assert,       CXA_AK_ASSERT,       pf, CXA_DM_PREDICATE_FALSE)
CXA_ENFORCE(assert,       CXA_AK_ASSERT,       ex, CXA_DM_EVALUATION_EXCEPTION)
CXA_ENFORCE(post_capture, CXA_AK_POST_CAPTURE, pf, CXA_DM_PREDICATE_FALSE)
CXA_ENFORCE(post_capture, CXA_AK_POST_CAPTURE, ex, CXA_DM_EVALUATION_EXCEPTION)

#undef CXA_ENFORCE
