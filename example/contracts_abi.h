// contracts_abi.h - Itanium C++ ABI types for C++26 Contracts
//
// This header defines the ABI-level types specified in the contracts
// ABI specification. In a real implementation, these would be provided
// by the C++ runtime library (libc++abi / libsupc++).

#ifndef CONTRACTS_ABI_H
#define CONTRACTS_ABI_H

#include <cstdint>
#include <cstddef>

namespace __cxxabiv1 {

// --- §3.2 Source Location ---

struct __cxa_source_location {
    const char* file_name;
    const char* function_name;
    unsigned line;
    unsigned column;
};

// --- §3.3 Enumerations ---

enum __cxa_assertion_kind_t : uint8_t {
    unspecified      = 0x00,
    pre              = 0x01,
    post             = 0x02,
    contract_assert  = 0x03,
};

enum __cxa_evaluation_semantic_t : uint8_t {
    // unspecified = 0x00,  // conflicts with assertion_kind, use qualified name
    enforced    = 0x01,
    observed    = 0x02,
};

enum __cxa_detection_mode_t : uint8_t {
    // unspecified = 0x00,
    predicate_false       = 0x01,
    evaluation_exception  = 0x02,
};

// --- §5.1 Descriptor Table ---

enum __cxa_vendor_id_t : uint8_t {
    VENDOR_GENERIC = 0x00,
    VENDOR_CLANG   = 0x01,
    VENDOR_GCC     = 0x02,
    VENDOR_MSVC    = 0x03,
};

enum __cxa_field_type_t : uint8_t {
    __cxa_field_source_location = 0x11,
    __cxa_field_source_text     = 0x12,
    __cxa_field_assertion_kind  = 0x13,

    // Reserved: 0x14 - 0x3F

    __cxa_field_extended        = 0x40,
};

union __cxa_descriptor_data_t {
    uintptr_t offset;
    void* extended_data;
};

// The descriptor table uses a flexible array member for field_types[],
// followed by padding and then data[num_entries]. Since we can't express
// this directly as a C++ struct with two flexible arrays, we provide
// accessor helpers.
struct __cxa_descriptor_table_t {
    uint8_t version   : 4;
    uint8_t vendor_id : 4;
    uint8_t num_entries;
    uint8_t field_types[];  // flexible array member
    // Followed by: padding to alignof(__cxa_descriptor_data_t)
    // Followed by: __cxa_descriptor_data_t data[num_entries]

    // Helper to access the data array (accounting for padding)
    const __cxa_descriptor_data_t* data() const {
        // field_types is at offset 2, followed by num_entries bytes
        const uint8_t* base = reinterpret_cast<const uint8_t*>(this);
        uintptr_t after_fields = reinterpret_cast<uintptr_t>(base + 2 + num_entries);
        // Align up to alignof(__cxa_descriptor_data_t)
        constexpr size_t align = alignof(__cxa_descriptor_data_t);
        uintptr_t aligned = (after_fields + align - 1) & ~(align - 1);
        return reinterpret_cast<const __cxa_descriptor_data_t*>(aligned);
    }
};

// --- §4.1 Versioned Entrypoint Data (exposition only) ---

struct contract_violation_data_v1 {
    uint8_t                      version;
    __cxa_detection_mode_t       mode;
    __cxa_evaluation_semantic_t  semantic;
    __cxa_descriptor_table_t*    static_descriptor;
    void*                        static_data;
};

} // namespace __cxxabiv1

// --- §4.1 Generic Entrypoint (global scope, C linkage) ---

extern "C"
void __cxa_contract_violation_entrypoint(void *data);

#endif // CONTRACTS_ABI_H
