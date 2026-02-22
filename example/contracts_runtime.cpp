// contracts_runtime.cpp - Example runtime entrypoint implementation
//
// Implements __cxa_contract_violation_entrypoint and a sample
// std::contracts::contract_violation. In a real implementation:
//   - The entrypoint lives in the C++ runtime (libc++abi / libsupc++)
//   - contract_violation is defined by the standard library (libc++ / libstdc++)

#include "contracts_abi.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

// =====================================================================
// std::source_location (matching libc++ layout)
//
// The real libc++ source_location stores a pointer to an __impl struct
// whose layout is { const char*, const char*, unsigned, unsigned } —
// the same layout as __cxa_source_location. The runtime constructs a
// source_location by pointing it at the __cxa_source_location in the
// compiler-emitted static data via __create_from_pointer.
// =====================================================================

namespace std {

class source_location {
    // The names __impl, _M_file_name, _M_function_name, _M_line, and
    // _M_column are hard-coded in the compiler and must not be changed.
    struct __impl {
        const char* _M_file_name;
        const char* _M_function_name;
        unsigned _M_line;
        unsigned _M_column;
    };

    const __impl* __ptr_ = nullptr;

public:
    // Used by the runtime to create a source_location from a pointer to
    // a layout-compatible struct (i.e. __cxa_source_location).
    static source_location __create_from_pointer(const void* __ptr) {
        source_location __loc;
        __loc.__ptr_ = static_cast<const __impl*>(__ptr);
        return __loc;
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
// Sample std::contracts implementation (normally in the standard library)
// =====================================================================

namespace std { namespace contracts {

// p2900 §3.7.2 enumerations
// Note: the standard uses different names/values than the ABI wire
// format. The runtime translates between them.
enum class assertion_kind {
    pre    = 0,
    post   = 1,
    assert = 2,
};

enum class evaluation_semantic {
    ignore        = 0,
    observe       = 1,
    enforce       = 2,
    quick_enforce = 3,
};

enum class detection_mode {
    predicate_false      = 0,
    evaluation_exception = 1,
};

// p2900 §3.7.3 - The class std::contracts::contract_violation
// Cannot be constructed, copied, moved, or mutated by the user.
class contract_violation {
    // Implementation-defined internal representation.
    // The ABI doesn't specify this layout; each standard library chooses
    // its own. This is one possible implementation.
    std::source_location   loc_;
    const char*            comment_;
    assertion_kind         kind_;
    detection_mode         detection_mode_;
    evaluation_semantic    semantic_;

    // Only the runtime can construct these
    friend void ::__cxa_contract_violation_entrypoint(void*);

    contract_violation() = default;

public:
    contract_violation(const contract_violation&) = delete;
    contract_violation& operator=(const contract_violation&) = delete;

    const char* comment() const noexcept { return comment_ ? comment_ : ""; }

    contracts::detection_mode detection_mode() const noexcept {
        return detection_mode_;
    }

    bool is_terminating() const noexcept {
        return semantic_ == evaluation_semantic::enforce
            || semantic_ == evaluation_semantic::quick_enforce;
    }

    assertion_kind kind() const noexcept { return kind_; }

    std::source_location location() const noexcept { return loc_; }

    evaluation_semantic semantic() const noexcept { return semantic_; }
};

void invoke_default_contract_violation_handler(const contract_violation&);

}} // namespace std::contracts

// =====================================================================
// Violation handler (user-replaceable via weak symbol)
// =====================================================================

extern "C" __attribute__((weak))
void handle_contract_violation(const std::contracts::contract_violation& cv) {
    const char *kind_str = "unknown";
    switch (cv.kind()) {
    case std::contracts::assertion_kind::pre:    kind_str = "precondition"; break;
    case std::contracts::assertion_kind::post:   kind_str = "postcondition"; break;
    case std::contracts::assertion_kind::assert: kind_str = "assertion"; break;
    }

    const char *semantic_str = "unknown";
    switch (cv.semantic()) {
    case std::contracts::evaluation_semantic::observe: semantic_str = "observe"; break;
    case std::contracts::evaluation_semantic::enforce: semantic_str = "enforce"; break;
    case std::contracts::evaluation_semantic::quick_enforce: semantic_str = "quick_enforce"; break;
    case std::contracts::evaluation_semantic::ignore: semantic_str = "ignore"; break;
    }

    auto loc = cv.location();
    fprintf(stderr, "contract violation: %s '%s' failed [%s]\n",
            kind_str, cv.comment(), semantic_str);
    fprintf(stderr, "  at %s:%u in %s\n",
            loc.file_name(), loc.line(), loc.function_name());
}

// =====================================================================
// §4.1 Generic Entrypoint Implementation
// =====================================================================

// Translation helpers: ABI wire format -> standard library enums.
// The ABI defines fixed uint8_t values for cross-compiler stability;
// the standard library may use different underlying values internally.
static std::contracts::assertion_kind
translate_kind(__cxxabiv1::__cxa_assertion_kind_t abi_kind) {
    switch (abi_kind) {
    case __cxxabiv1::pre:             return std::contracts::assertion_kind::pre;
    case __cxxabiv1::post:            return std::contracts::assertion_kind::post;
    case __cxxabiv1::contract_assert: return std::contracts::assertion_kind::assert;
    default:                          return std::contracts::assertion_kind::pre;
    }
}

static std::contracts::evaluation_semantic
translate_semantic(__cxxabiv1::__cxa_evaluation_semantic_t abi_sem) {
    switch (abi_sem) {
    case __cxxabiv1::enforced: return std::contracts::evaluation_semantic::enforce;
    case __cxxabiv1::observed: return std::contracts::evaluation_semantic::observe;
    default:                   return std::contracts::evaluation_semantic::enforce;
    }
}

static std::contracts::detection_mode
translate_mode(__cxxabiv1::__cxa_detection_mode_t abi_mode) {
    switch (abi_mode) {
    case __cxxabiv1::predicate_false:      return std::contracts::detection_mode::predicate_false;
    case __cxxabiv1::evaluation_exception: return std::contracts::detection_mode::evaluation_exception;
    default:                               return std::contracts::detection_mode::predicate_false;
    }
}

extern "C"
void __cxa_contract_violation_entrypoint(void *data) {
    uint8_t version = *static_cast<uint8_t*>(data);

    if (version < 1) {
        fprintf(stderr, "fatal: unknown contract violation data version %u\n", version);
        std::abort();
    }

    // For version >= 1, the first fields are always the v1 layout
    auto *v1 = static_cast<__cxxabiv1::contract_violation_data_v1*>(data);

    // Walk the descriptor table to extract static fields
    auto *desc = v1->static_descriptor;
    auto *sdata = static_cast<const char*>(v1->static_data);
    auto *data_array = desc->data();

    const __cxxabiv1::__cxa_source_location *loc = nullptr;
    const char *source_text = nullptr;
    __cxxabiv1::__cxa_assertion_kind_t kind = __cxxabiv1::unspecified;

    for (uint8_t i = 0; i < desc->num_entries; ++i) {
        auto offset = data_array[i].offset;
        switch (desc->field_types[i]) {
        case __cxxabiv1::__cxa_field_source_location:
            loc = reinterpret_cast<const __cxxabiv1::__cxa_source_location*>(sdata + offset);
            break;
        case __cxxabiv1::__cxa_field_source_text:
            source_text = *reinterpret_cast<const char* const*>(sdata + offset);
            break;
        case __cxxabiv1::__cxa_field_assertion_kind:
            kind = *reinterpret_cast<const __cxxabiv1::__cxa_assertion_kind_t*>(sdata + offset);
            break;
        default:
            break;  // ignore unknown fields
        }
    }

    // Construct std::contracts::contract_violation
    std::contracts::contract_violation cv;

    // __cxa_source_location has the same layout as source_location::__impl,
    // so we can create a source_location that points directly into the
    // compiler-emitted static data.
    if (loc) {
        cv.loc_ = std::source_location::__create_from_pointer(loc);
    }
    cv.comment_ = source_text;
    cv.kind_ = translate_kind(kind);
    cv.detection_mode_ = translate_mode(v1->mode);
    cv.semantic_ = translate_semantic(v1->semantic);

    handle_contract_violation(cv);

    // If the semantic is terminating, abort after the handler returns
    if (cv.is_terminating()) {
        std::abort();
    }
}
