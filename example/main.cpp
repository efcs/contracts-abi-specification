// main.cpp - Simulates compiler-emitted code for contract assertions
//
// This file manually constructs the data structures that a compiler
// emits in .rodata and calls entry points directly, demonstrating the
// full ABI flow without requiring a contracts-enabled compiler.
//
// With a contracts-enabled compiler, the user writes:
//   int foo(int x) pre(x > 0) { ... }
// and the compiler generates exactly this kind of code.

#include "contracts_abi.h"
#include <cstdio>
#include <cstddef>

using namespace __cxxabiv1;

// =====================================================================
// Per-TU descriptor table (normally in .rodata)
// =====================================================================

// The "basic" descriptor: 3 fields (source_location, comment, message).
// This is shared by all basic data blocks in the TU.
struct descriptor_table_basic {
    uint8_t header;
    uint8_t num_entries;
    uint8_t field_ids[3];
    uint8_t padding[3];
    __cxa_descriptor_data_t data[3];
};

// Per-assertion data block layout (basic variant).
struct contract_data_basic {
    const __cxa_descriptor_table_t*  descriptor;
    const __cxa_contract_data_block* next;
    __cxa_source_location loc;
    const char* comment;
    const char* message;
};

static const descriptor_table_basic desc_basic = {
    (1u << 4) | CXA_VENDOR_GENERIC,  // version=1, vendor=GENERIC
    3,
    { CXA_FIELD_SOURCE_LOCATION, CXA_FIELD_COMMENT, CXA_FIELD_MESSAGE },
    { 0, 0, 0 },
    {
        { offsetof(contract_data_basic, loc) },
        { offsetof(contract_data_basic, comment) },
        { offsetof(contract_data_basic, message) },
    },
};

static auto* desc = reinterpret_cast<const __cxa_descriptor_table_t*>(
    &desc_basic);

// =====================================================================
// Per-assertion static data blocks (normally in .rodata)
// =====================================================================

// int foo(int x) pre(x > 0) -- enforced
static const contract_data_basic data_foo_pre = {
    desc,
    nullptr,
    { "example.cpp", "foo", 10, 5 },
    "x > 0",
    "",
};

// int bar(int x) post(r: r >= 0) -- observed
static const contract_data_basic data_bar_post = {
    desc,
    nullptr,
    { "example.cpp", "bar", 20, 5 },
    "result >= 0",
    "",
};

// void baz(int* ptr) with contract_assert(ptr != nullptr) -- observed
static const contract_data_basic data_baz_assert = {
    desc,
    nullptr,
    { "example.cpp", "baz", 30, 9 },
    "ptr != nullptr",
    "",
};

// =====================================================================
// Functions with contracts
// =====================================================================

// With a real compiler, the user writes:
//   int foo(int x) pre(x > 0) { return x * 2; }
// The compiler emits the data block above and this check:

int foo(int x) {
    if (!(x > 0))
        __cxa_contract_violation_pre_enforce_pf(
            (void*)&data_foo_pre);
    return x * 2;
}

int bar(int x) {
    int result = x - 5;
    if (!(result >= 0))
        __cxa_contract_violation_post_observe_pf(
            (void*)&data_bar_post);
    return result;
}

void baz(int* ptr) {
    if (!(ptr != nullptr))
        __cxa_contract_violation_assert_observe_pf(
            (void*)&data_baz_assert);
    if (ptr)
        *ptr = 42;
}

// =====================================================================
// Main
// =====================================================================

int main() {
    std::fprintf(stderr, "=== Observed semantic (prints and continues) ===\n");
    int r = bar(3);  // 3 - 5 = -2, postcondition fails
    std::fprintf(stderr, "bar(3) returned %d (continued after observed violation)\n\n", r);

    std::fprintf(stderr, "=== Observed assertion (prints and continues) ===\n");
    baz(nullptr);  // assertion fails, observed -> continues
    std::fprintf(stderr, "baz(nullptr) returned (continued after observed violation)\n\n");

    std::fprintf(stderr, "=== Enforced semantic (prints and terminates) ===\n");
    foo(-1);  // precondition fails, enforced -> terminates
    std::fprintf(stderr, "this should not be reached\n");

    return 0;
}
