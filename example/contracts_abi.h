// Contracts ABI specification -- portable wire-format header.
//
// This is a standalone, compiler-independent rendition of the ABI types
// defined in the specification.  Both GCC (bits/contracts_abi.h) and
// Clang (__contracts/abi.h) ship byte-compatible production headers.
//
// This file is part of the specification's compilable example.

#ifndef CONTRACTS_ABI_H
#define CONTRACTS_ABI_H

#include <cstdint>
#include <cstddef>

namespace __cxxabiv1 {

// -------------------------------------------------------------------
// Source location wire format (specification S3.2)
// -------------------------------------------------------------------

// Layout matches std::source_location::__impl in both libstdc++ and
// libc++.
struct __cxa_source_location
{
  const char* file_name;
  const char* function_name;
  unsigned    line;
  unsigned    column;
};

// -------------------------------------------------------------------
// ABI wire-format enums (specification S3.3)
// -------------------------------------------------------------------

// Decoupled from std::contracts enum representations.
// 0x00 is always "unspecified" (matches default when field is absent).

enum __cxa_assertion_kind_t : uint8_t
{
  CXA_AK_UNSPECIFIED    = 0x00,
  CXA_AK_PRE            = 0x01,
  CXA_AK_POST           = 0x02,
  CXA_AK_ASSERT         = 0x03,
  CXA_AK_MANUAL         = 0x04,
  CXA_AK_CASSERT        = 0x05,
  CXA_AK_POST_CAPTURE   = 0x06,
};

enum __cxa_evaluation_semantic_t : uint8_t
{
  CXA_ES_UNSPECIFIED    = 0x00,
  CXA_ES_IGNORE         = 0x01,
  CXA_ES_OBSERVE        = 0x02,
  CXA_ES_ENFORCE        = 0x03,
  CXA_ES_QUICK_ENFORCE  = 0x04,
};

enum __cxa_detection_mode_t : uint8_t
{
  CXA_DM_UNSPECIFIED          = 0x00,
  CXA_DM_PREDICATE_FALSE      = 0x01,
  CXA_DM_EVALUATION_EXCEPTION = 0x02,
};

// -------------------------------------------------------------------
// Field IDs (specification S3.4)
// -------------------------------------------------------------------

// Each ID identifies both a logical field and its wire-format encoding.
// Multiple IDs can represent the same logical field with different
// encodings; accessors search for any matching ID and take the first.

enum __cxa_field_id_t : uint8_t
{
  // Standard fields (< 0x40): byte offset into containing data block.
  CXA_FIELD_SOURCE_LOCATION     = 0x01,
  CXA_FIELD_COMMENT             = 0x02,
  CXA_FIELD_MESSAGE             = 0x03,
  CXA_FIELD_LOCAL_HANDLER       = 0x04,
  CXA_FIELD_QUERY_FUNCTION      = 0x05,
  CXA_FIELD_LABEL_PTR           = 0x06,
  CXA_FIELD_ASSERTION_KIND      = 0x07,
  CXA_FIELD_EVALUATION_SEMANTIC = 0x08,
  CXA_FIELD_DETECTION_MODE      = 0x09,
  CXA_FIELD_EXCEPTION_PTR       = 0x0A,
  // 0x0B - 0x3F: reserved for future standard fields.

  // Extended fields (>= 0x40): direct pointer, not offset.
  CXA_FIELD_EXTENDED            = 0x40,
  // 0x40 - 0x7F: vendor-specific extensions.
  // 0x80 - 0xFF: reserved for future use.
};

// -------------------------------------------------------------------
// Vendor IDs (4-bit, packed into descriptor table header)
// -------------------------------------------------------------------

enum __cxa_vendor_id_t : uint8_t
{
  CXA_VENDOR_GENERIC = 0x0,
  CXA_VENDOR_GCC     = 0x1,
  CXA_VENDOR_CLANG   = 0x2,
  CXA_VENDOR_MSVC    = 0x3,
};

// -------------------------------------------------------------------
// Descriptor data entry (specification S5.1.2)
// -------------------------------------------------------------------

// For standard fields (ID < 0x40), 'offset' is a byte offset into the
// containing data block.  For extended fields (ID >= 0x40), 'pointer'
// is a direct pointer to the data.
union __cxa_descriptor_data_t
{
  uintptr_t    offset;
  const void*  pointer;
};

// -------------------------------------------------------------------
// Descriptor table (specification S5.1.3)
// -------------------------------------------------------------------

// Describes which fields are present in a data block and where to find
// them.
//
// Binary layout:
//   byte 0:     [table_version : 4 bits][vendor_id : 4 bits]
//   byte 1:     num_entries
//   bytes 2..:  field_ids[num_entries]
//   <padding to alignof(__cxa_descriptor_data_t)>
//   data[num_entries] (__cxa_descriptor_data_t)
struct __cxa_descriptor_table_t
{
  uint8_t header;
  uint8_t num_entries;
  uint8_t field_ids[];

  uint8_t table_version() const { return header >> 4; }
  uint8_t vendor_id()     const { return header & 0x0F; }

  const __cxa_descriptor_data_t* data() const
  {
    auto p = reinterpret_cast<uintptr_t>(field_ids + num_entries);
    p = (p + alignof(__cxa_descriptor_data_t) - 1)
        & ~(alignof(__cxa_descriptor_data_t) - 1);
    return reinterpret_cast<const __cxa_descriptor_data_t*>(p);
  }
};

// -------------------------------------------------------------------
// Data block (specification S5.2)
// -------------------------------------------------------------------

// One node in a linked list of violation data.  Each block has its own
// descriptor table and field data.  The runtime walks head-to-tail,
// taking the first occurrence of each field (first-found-wins).
struct __cxa_contract_data_block
{
  const __cxa_descriptor_table_t*  descriptor;
  const __cxa_contract_data_block* next;
  // Field data follows (layout described by descriptor,
  // byte offsets relative to the start of this struct).
};

// -------------------------------------------------------------------
// Function pointer types (specification S3.5)
// -------------------------------------------------------------------

// Local handler: returns 1 if handled, 0 to continue dispatch.
using __cxa_local_handler_fn_t
    = int (*)(const void* /*label_ptr*/,
              const void* /*contract_violation_ptr*/);

// Query function: calls the label's query() method.
using __cxa_query_fn_t
    = void* (*)(const void* /*label_ptr*/,
                const void* /*key*/,
                std::size_t /*index*/);

// -------------------------------------------------------------------
// Chain-walking field lookup (specification S5.4)
// -------------------------------------------------------------------

// Walks the chain head-to-tail, searching each block's descriptor for
// any field ID in the 'ids' array.  Returns a pointer to the field data
// within the block where the first match was found, or nullptr.
const void*
__cxa_find_field(const __cxa_contract_data_block* chain,
                 const uint8_t* ids,
                 uint8_t num_ids);

// Typed convenience: find a scalar value field.
template<typename T>
T __cxa_find_field_value(const __cxa_contract_data_block* chain,
                         uint8_t field_id,
                         T default_value)
{
  uint8_t ids[] = { field_id };
  auto p = __cxa_find_field(chain, ids, 1);
  if (p)
    return *static_cast<const T*>(p);
  return default_value;
}

// Typed convenience: find a pointer to structured field data.
template<typename T>
const T* __cxa_find_field_ptr(const __cxa_contract_data_block* chain,
                              uint8_t field_id)
{
  uint8_t ids[] = { field_id };
  return static_cast<const T*>(__cxa_find_field(chain, ids, 1));
}

} // namespace __cxxabiv1

// -------------------------------------------------------------------
// Entry points (extern "C", specification S4)
// -------------------------------------------------------------------

// Provided by the ABI runtime.
// All take a single void* pointing to the head of a data block chain.
extern "C" {

// Universal entry points.
void __cxa_contract_violation(void* data);
void __cxa_contract_violation_noexcept(void* data) noexcept;

// Specialized: pre + observe
void __cxa_contract_violation_pre_observe_pf(void* data);
void __cxa_contract_violation_pre_observe_pf_noexcept(void* data) noexcept;
void __cxa_contract_violation_pre_observe_ex(void* data);
void __cxa_contract_violation_pre_observe_ex_noexcept(void* data) noexcept;

// Specialized: post + observe
void __cxa_contract_violation_post_observe_pf(void* data);
void __cxa_contract_violation_post_observe_pf_noexcept(void* data) noexcept;
void __cxa_contract_violation_post_observe_ex(void* data);
void __cxa_contract_violation_post_observe_ex_noexcept(void* data) noexcept;

// Specialized: assert + observe
void __cxa_contract_violation_assert_observe_pf(void* data);
void __cxa_contract_violation_assert_observe_pf_noexcept(void* data) noexcept;
void __cxa_contract_violation_assert_observe_ex(void* data);
void __cxa_contract_violation_assert_observe_ex_noexcept(void* data) noexcept;

// Specialized: pre + enforce
[[noreturn]] void __cxa_contract_violation_pre_enforce_pf(void* data);
[[noreturn]] void __cxa_contract_violation_pre_enforce_pf_noexcept(void* data) noexcept;
[[noreturn]] void __cxa_contract_violation_pre_enforce_ex(void* data);
[[noreturn]] void __cxa_contract_violation_pre_enforce_ex_noexcept(void* data) noexcept;

// Specialized: post + enforce
[[noreturn]] void __cxa_contract_violation_post_enforce_pf(void* data);
[[noreturn]] void __cxa_contract_violation_post_enforce_pf_noexcept(void* data) noexcept;
[[noreturn]] void __cxa_contract_violation_post_enforce_ex(void* data);
[[noreturn]] void __cxa_contract_violation_post_enforce_ex_noexcept(void* data) noexcept;

// Specialized: assert + enforce
[[noreturn]] void __cxa_contract_violation_assert_enforce_pf(void* data);
[[noreturn]] void __cxa_contract_violation_assert_enforce_pf_noexcept(void* data) noexcept;
[[noreturn]] void __cxa_contract_violation_assert_enforce_ex(void* data);
[[noreturn]] void __cxa_contract_violation_assert_enforce_ex_noexcept(void* data) noexcept;

// Specialized: post_capture + observe
void __cxa_contract_violation_post_capture_observe_pf(void* data);
void __cxa_contract_violation_post_capture_observe_pf_noexcept(void* data) noexcept;
void __cxa_contract_violation_post_capture_observe_ex(void* data);
void __cxa_contract_violation_post_capture_observe_ex_noexcept(void* data) noexcept;

// Specialized: post_capture + enforce
[[noreturn]] void __cxa_contract_violation_post_capture_enforce_pf(void* data);
[[noreturn]] void __cxa_contract_violation_post_capture_enforce_pf_noexcept(void* data) noexcept;
[[noreturn]] void __cxa_contract_violation_post_capture_enforce_ex(void* data);
[[noreturn]] void __cxa_contract_violation_post_capture_enforce_ex_noexcept(void* data) noexcept;

} // extern "C"

// Assert integration entry point (P3290, specification S4.6).
// Shared by libstdc++ and libc++: the <cassert> `assert` macro expands to a
// call to this single symbol.  It reports assertion_kind=CXA_AK_CASSERT and
// detection_mode=CXA_DM_PREDICATE_FALSE and terminates via std::abort() on
// any completion of the handler (normal return or an escaping exception).
// extern "C++" because of the std::source_location parameter.
namespace std { class source_location; }
extern "C++" [[noreturn]] void
__cxa_handle_cassert_violation(const char* comment,
                              std::source_location loc) noexcept;

#endif // CONTRACTS_ABI_H
