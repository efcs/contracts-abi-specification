// main.cpp - Simulates compiler-emitted code for contract assertions
//
// This file manually constructs the data structures that a compiler
// would emit in .rodata and the TU-local wrappers it would generate.
// It then triggers contract violations to exercise the runtime.

#include "contracts_abi.h"
#include <cstdio>
#include <cstddef>

// =====================================================================
// Compiler-emitted per-TU data (normally in .rodata)
// =====================================================================

// The descriptor table describes the layout of per-contract static data.
// Since __cxa_descriptor_table_t uses a flexible array member followed
// by aligned data, we use a concrete struct matching the binary layout.
struct descriptor_table_3fields {
    uint8_t header;       // version:4 | vendor_id:4
    uint8_t num_entries;
    uint8_t field_types[3];
    uint8_t padding[3];   // align to 8 for __cxa_descriptor_data_t
    __cxxabiv1::__cxa_descriptor_data_t data[3];
};

static const descriptor_table_3fields desc_storage = {
    .header = (1 /* version */ << 0) | (__cxxabiv1::VENDOR_GENERIC << 4),
    .num_entries = 3,
    .field_types = {
        __cxxabiv1::__cxa_field_source_location,
        __cxxabiv1::__cxa_field_source_text,
        __cxxabiv1::__cxa_field_assertion_kind,
    },
    .padding = {0, 0, 0},
    .data = {
        { .offset = 0 },                                                  // source_location at offset 0
        { .offset = offsetof(__cxxabiv1::__cxa_source_location, file_name)             // source_text follows source_location
                    + sizeof(__cxxabiv1::__cxa_source_location)
                    - offsetof(__cxxabiv1::__cxa_source_location, file_name) },
        { .offset = sizeof(__cxxabiv1::__cxa_source_location) + sizeof(const char*) }, // assertion_kind after text ptr
    },
};

// Alias for use as __cxa_descriptor_table_t*
static auto *desc = reinterpret_cast<__cxxabiv1::__cxa_descriptor_table_t*>(
    const_cast<descriptor_table_3fields*>(&desc_storage));

// =====================================================================
// Per-contract-site static data (normally in .rodata)
// =====================================================================

// Layout matches the default static data layout (§5.2):
//   __cxa_source_location | const char* (source text) | __cxa_assertion_kind_t
struct contract_static_data {
    __cxxabiv1::__cxa_source_location loc;
    const char *text;
    __cxxabiv1::__cxa_assertion_kind_t kind;
};

static const contract_static_data data_foo_pre = {
    .loc  = { "example.cpp", "foo", 10, 5 },
    .text = "x > 0",
    .kind = __cxxabiv1::pre,
};

static const contract_static_data data_bar_post = {
    .loc  = { "example.cpp", "bar", 20, 5 },
    .text = "result >= 0",
    .kind = __cxxabiv1::post,
};

static const contract_static_data data_baz_assert = {
    .loc  = { "example.cpp", "baz", 30, 9 },
    .text = "ptr != nullptr",
    .kind = __cxxabiv1::contract_assert,
};

// =====================================================================
// Compiler-emitted per-TU wrappers (§4.2)
// =====================================================================

// Enforced + predicate_false
[[noreturn]]
static void __cv_v1_pf_se(void *static_data) {
    __cxxabiv1::contract_violation_data_v1 data = {
        .version = 1,
        .mode = __cxxabiv1::predicate_false,
        .semantic = __cxxabiv1::enforced,
        .static_descriptor = desc,
        .static_data = static_data,
    };
    __cxa_contract_violation_entrypoint(&data);
    __builtin_unreachable();
}

// Observed + predicate_false
static void __cv_v1_pf_so(void *static_data) {
    __cxxabiv1::contract_violation_data_v1 data = {
        .version = 1,
        .mode = __cxxabiv1::predicate_false,
        .semantic = __cxxabiv1::observed,
        .static_descriptor = desc,
        .static_data = static_data,
    };
    __cxa_contract_violation_entrypoint(&data);
}

// =====================================================================
// Functions with contracts (simulating what the compiler generates)
// =====================================================================

// int foo(int x) pre(x > 0) -- enforced
int foo(int x) {
    if (!(x > 0)) {
        __cv_v1_pf_se((void*)&data_foo_pre);
    }
    return x * 2;
}

// int bar(int x) post(r: r >= 0) -- observed
int bar(int x) {
    int result = x - 5;
    if (!(result >= 0)) {
        __cv_v1_pf_so((void*)&data_bar_post);
    }
    return result;
}

// void baz(int *ptr) with contract_assert(ptr != nullptr) -- observed
void baz(int *ptr) {
    if (!(ptr != nullptr)) {
        __cv_v1_pf_so((void*)&data_baz_assert);
    }
    *ptr = 42;
}

// =====================================================================
// Main
// =====================================================================

int main() {
    printf("=== Testing observed semantic (should print and continue) ===\n");
    int r = bar(3);  // 3 - 5 = -2, postcondition fails
    printf("bar(3) returned %d (continued after observed violation)\n\n", r);

    printf("=== Testing enforced semantic (should print and abort) ===\n");
    foo(-1);  // precondition fails, enforced -> abort
    printf("this should not be reached\n");

    return 0;
}
