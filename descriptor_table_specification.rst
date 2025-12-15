.. _descriptor_table_specification:

===========================================================
Descriptor Table Approach: Technical Specification
===========================================================

.. contents::
  :depth: 2
  :local:


Specification Status
====================

This document provides a detailed technical specification for the descriptor
table approach to the Itanium C++ Contracts ABI. It incorporates robustness
and portability considerations:

- Header expanded: explicit data_size, header_size, flags, native-endian definition
- Wider counts: 16-bit num_entries and 32-bit offsets (removes hard caps)
- Alignment rules defined; padding in static_data allowed/required as needed
- Bounds and alignment validation rules for runtimes
- Corrected field ID ranges and type assignments
- Vendor field namespacing defined to avoid collisions
- Optional sorted entries and optional index for faster lookup
- Clarified deduplication scope (within link unit; not across DSOs)
- Reentrancy/termination rules for handlers
- Minimal dynamic_data TLV defined for future-proofing


Overview
========

This document specifies the descriptor table approach for the Itanium C++
Contracts ABI. The approach separates metadata describing data layout from the
field data itself, enabling ABI-stable evolution, efficient field omission, and
vendor extensibility without coordination.


Core Concept
============

Contract violation data consists of two components:

1. Metadata (descriptor table): Describes what fields exist and where they are
   located within the static data blob
2. Data (static_data blob): Tightly packed field values (pointers, strings,
   scalars), with padding as required for alignment

Properties:

- ABI stability: Metadata can describe any layout without breaking compatibility
- Efficiency: Metadata is shared across contracts; data is compact and aligned
- Extensibility: New field types are added without changing existing structures


ABI Interface
=============

Entrypoint Function
-------------------

.. code-block:: cpp

    namespace __cxxabiv1 {

    // Primary entrypoint - all parameters explicit
    [[noreturn]]
    void __cxa_contract_violation_entrypoint(
        const __cxa_descriptor_table_t* static_descriptor,
        const void* static_data,
        __cxa_detection_mode_t mode,
        __cxa_evaluation_semantic_t semantic,
        const __cxa_runtime_data_t* dynamic_data, // see TLV below (may be nullptr)
        void* reserved
    );

    } // namespace __cxxabiv1

Parameters:

- static_descriptor: Pointer to descriptor table (compile-time constant)
- static_data: Pointer to packed field data (compile-time constant)
- mode: How violation was detected (predicate_false or evaluation_exception)
- semantic: Evaluation semantic (enforced or observed)
- dynamic_data: Optional runtime-generated TLV data (nullptr if none)
- reserved: Reserved for future use

Runtime behavior requirements:

- The entrypoint constructs std::contract_violation and invokes the registered
  violation handler.
- If the handler returns or throws, the runtime must force termination
  (e.g., std::terminate()).
- A reentrancy guard must prevent infinite recursion if a contract fails within
  the handler; on reentry, terminate immediately.


Binary Format
=============

Endianness
----------

- All multi-byte integer fields in the descriptor and entries are encoded in
  the platform’s native endianness.
- Examples below use little-endian for illustration.

Header (v2)
-----------

The descriptor begins with a fixed-size header followed by an entry array. The
header includes a header_size field to allow future extension.

.. code-block:: cpp

    struct __cxa_descriptor_table_t {
        uint8_t  version;        // = 2 for this revision
        uint8_t  vendor_id;      // 0=standard, 1=GCC, 2=Clang, ...
        uint8_t  flags;          // bit0: entries_sorted_by_type
                                 // bit1: has_optional_index (after entries)
                                 // other bits: reserved (0)
        uint8_t  reserved0;      // must be 0 for v2

        uint16_t num_entries;    // number of descriptor entries
        uint16_t header_size;    // size of this header in bytes (>= 16)

        uint32_t data_size;      // total size in bytes of static_data blob
        uint8_t  data_alignment; // required base alignment for static_data in bytes (power of two)
        uint8_t  reserved1[3];   // must be 0
        // Followed by: __cxa_descriptor_entry_t entries[num_entries];
        // Optionally followed by an index section if flags.has_optional_index=1
    };
    // sizeof(__cxa_descriptor_table_t) == 16 bytes for v2

Descriptor Entry (v2)
---------------------

Each entry maps a field identifier to an offset within the static_data blob.
Offsets are 32-bit and must respect alignment constraints (see below).

.. code-block:: cpp

    struct __cxa_descriptor_entry_t {
        uint16_t field_type;  // field identifier, see Field Type Encoding
        uint16_t reserved;    // must be 0 for v2 (alignment/flags future use)
        uint32_t offset;      // byte offset from start of static_data
    };
    // sizeof(__cxa_descriptor_entry_t) == 8 bytes

Optional Index (v2)
-------------------

If flags.has_optional_index=1, an index immediately follows the entries to
accelerate lookups. The index format is intentionally simple and optional; a
runtime may ignore it.

.. code-block:: text

    uint16_t index_count      // number of index records
    struct index_record {
        uint16_t field_type   // key
        uint16_t entry_start  // start (inclusive) in entries[] for this key or key range
        uint16_t entry_count  // count of entries for this key or key range
    }[index_count]

When entries_sorted_by_type=1, index_count is typically small (e.g., one record
per distinct field_type present). Runtimes may binary-search either the entries
or the index.


Field Type Encoding
===================

Standard fields occupy the 0x0001–0x00FF range. Vendor-specific fields are
namespaced to avoid collisions.

- Standard field (namespace 0): 0x0001–0x00FF
- Vendor-specific field (namespace vendor_id): 0x8000 | (vendor_id << 8) | local_id
  - vendor_id is the same 8-bit code stored in header.vendor_id
  - local_id is vendor-local (0x01–0xFF)
  - Runtimes must only interpret vendor fields if the embedded vendor_id matches
    header.vendor_id. Otherwise, ignore the entry.

Recommended standard field assignments (v2):

.. code-block:: cpp

    enum class __cxa_contract_violation_field_t : uint16_t {
        // Pointers
        source_location_ptr   = 0x0001, // const __cxa_source_location*
        source_text_ptr       = 0x0002, // const char*
        contract_label_ptr    = 0x0003, // const char*

        // Scalars
        assertion_kind_u8     = 0x0011, // uint8_t
        // detection_mode and evaluation_semantic are passed as entrypoint params
        // and are not stored in static_data
    };

Type ranges:

- 0x0000: Reserved (invalid)
- 0x0001–0x00FF: Standard fields (Itanium ABI committee)
- 0x0100–0x7FFF: Reserved for future standard expansion
- 0x8000–0xFFFF: Vendor fields with embedded vendor_id


Static Data Layout and Alignment
================================

The static_data blob contains field values at specified offsets. Compilers may
insert padding to satisfy alignment. Offsets must respect the natural alignment
requirements of the referenced types on the target platform.

Rules:

- The static_data base address must be aligned to header.data_alignment bytes
  (a power of two). Recommended minimum is alignof(void*).
- For each entry, offset % alignof(field_type) == 0 must hold.
  - For pointer fields: alignof(void*)
  - For uint8_t scalars: alignof(uint8_t) (i.e., 1)
- The runtime may validate these constraints for standard fields.
- Offsets + known field sizes for standard fields must be within [0, data_size].
- Vendors are responsible for alignment of their own field types; unknown vendor
  fields are ignored by standard runtimes.

Example layout (location + text + kind):

.. code-block:: cpp

    struct {
        const __cxa_source_location* location;  // offset 0, size 8 (on LP64)
        const char* source_text;                // offset 8, size 8
        uint8_t assertion_kind;                 // offset 16, size 1
        // padding may be present but is not required by the format
    };

Binary example (little-endian, LP64):

.. code-block:: text

    static_data @ 0x6000 (data_size=17):
    0x6000:  00 70 00 00 00 00 00 00   // location ptr → 0x7000
    0x6008:  00 80 00 00 00 00 00 00   // text ptr → 0x8000
    0x6010:  01                        // kind = precondition


Descriptor Examples
===================

Example: Three-entry descriptor (v2)
------------------------------------

.. code-block:: text

    Header (16 bytes):
    0x5000:  02                // version=2
    0x5001:  02                // vendor_id=2 (Clang, example)
    0x5002:  01                // flags: entries_sorted_by_type=1
    0x5003:  00                // reserved0
    0x5004:  03 00             // num_entries=3
    0x5006:  10 00             // header_size=16
    0x5008:  11 00 00 00       // data_size=17
    0x500C:  08                // data_alignment=8
    0x500D:  00 00 00          // reserved1

    Entries (3 × 8 bytes):
    0x5010:  01 00  00 00  00 00 00 00   // field_type=0x0001 (source_location_ptr), offset=0
    0x5018:  02 00  00 00  08 00 00 00   // field_type=0x0002 (source_text_ptr),   offset=8
    0x5020:  11 00  00 00  10 00 00 00   // field_type=0x0011 (assertion_kind_u8), offset=16

Total size: 16 + 24 = 40 bytes (descriptor)


Code Generation
===============

Basic Example
-------------

.. code-block:: cpp

    // Source
    void withdraw(int amount)
        pre(amount > 0)
    {
        balance -= amount;
    }

Compiler Output (Pseudo-assembly)
----------------------------------

.. code-block:: asm

    withdraw:
        cmp     edi, 0
        jg      .L_contract_passed

    .L_contract_failed:
        lea     rdi, [rip + .L_descriptor_v2]     # static_descriptor
        lea     rsi, [rip + .L_static_data]       # static_data
        xor     edx, edx                          # mode = predicate_false
        xor     ecx, ecx                          # semantic = enforced
        xor     r8d, r8d                          # dynamic_data = nullptr
        xor     r9d, r9d                          # reserved = nullptr
        call    __cxa_contract_violation_entrypoint
        ud2

    .L_contract_passed:
        # ...
        ret

    .section .rodata
    .p2align 4
    .L_descriptor_v2:
        .byte   2           # version
        .byte   2           # vendor_id (Clang, exemplar)
        .byte   1           # flags: entries_sorted_by_type
        .byte   0           # reserved0
        .short  3           # num_entries
        .short  16          # header_size
        .long   17          # data_size
        .byte   8           # data_alignment
        .byte   0,0,0       # reserved1

        # entries
        .short  0x0001      # source_location_ptr
        .short  0           # reserved
        .long   0           # offset

        .short  0x0002      # source_text_ptr
        .short  0
        .long   8

        .short  0x0011      # assertion_kind_u8
        .short  0
        .long   16

    .p2align 3
    .L_static_data:
        .quad   .L_source_location
        .quad   .L_source_text
        .byte   0x01                    # precondition

    .L_source_location:
        .quad   .L_file_name            # file_name pointer
        .quad   .L_function_name        # function_name pointer
        .long   42                      # line
        .long   8                       # column

    .L_source_text:
        .asciz  "amount > 0"

    .L_file_name:
        .asciz  "bank.cpp"

    .L_function_name:
        .asciz  "withdraw"


Field Omission Example
----------------------

With -fno-contract-source-text:

.. code-block:: asm

    .L_descriptor_v2_no_text:
        .byte   2
        .byte   2
        .byte   1
        .byte   0
        .short  2               # 2 entries
        .short  16
        .long   9               # data_size
        .byte   8               # data_alignment
        .byte   0,0,0

        .short  0x0001          # source_location_ptr
        .short  0
        .long   0

        .short  0x0011          # assertion_kind_u8
        .short  0
        .long   8

    .p2align 3
    .L_static_data_no_text:
        .quad   .L_source_location
        .byte   0x01

Savings: 8 bytes in static_data and one entry (8 bytes) in descriptor.


Vendor Extensions
=================

Vendor field namespace
----------------------

- Vendor fields must use field_type = 0x8000 | (vendor_id << 8) | local_id.
- Runtimes must only interpret vendor fields when the embedded vendor_id equals
  header.vendor_id; otherwise ignore.
- Standard fields always use 0x0001–0x00FF.

Example (GCC local field 0x05):

.. code-block:: asm

    # vendor_id=1 (GCC)
    .byte   2     # version
    .byte   1     # vendor_id
    ...
    .short  0x8105   # 0x8000 | (1 << 8) | 0x05
    .short  0
    .long   0x11


Runtime Implementation
======================

Descriptor Parsing and Validation (v2)
--------------------------------------

Runtimes should validate descriptors in hardened or debug builds:

- header_size >= 16 and <= reasonable upper bound
- flags reserved bits are zero
- data_alignment is power-of-two and >= 1
- num_entries * sizeof(entry) does not overflow
- For each standard field entry:
  - offset + known_size <= data_size
  - offset % alignof(field_type) == 0
- Optionally verify non-decreasing offsets (advisory)

Entry lookup
------------

- If flags.entries_sorted_by_type=1, use binary search; else linear scan.
- Runtimes may cache resolved offsets per std::contract_violation instance to
  avoid repeated scans when multiple accessors are called.

Entrypoint
----------

.. code-block:: cpp

    namespace __cxxabiv1 {

    [[noreturn]]
    void __cxa_contract_violation_entrypoint(
        const __cxa_descriptor_table_t* static_descriptor,
        const void* static_data,
        __cxa_detection_mode_t mode,
        __cxa_evaluation_semantic_t semantic,
        const __cxa_runtime_data_t* dynamic_data,
        void*)
    {
        // (Optional) reentrancy guard here

        __cxa_contract_violation_info_t cv_info{
            .static_descriptor = static_descriptor,
            .static_data = static_data,
            .mode = mode,
            .semantic = semantic,
            .dynamic_data = dynamic_data
        };

        std::contract_violation cv(&cv_info);
        auto handler = get_contract_violation_handler();
        handler(cv);
        std::terminate();
    }

    } // namespace __cxxabiv1

Field Accessor API
------------------

.. code-block:: cpp

    namespace __cxxabiv1 {

    bool __cxa_get_contract_violation_field(
        const __cxa_contract_violation_info_t* cv_info,
        __cxa_contract_violation_field_t field,
        void* out_ptr);

    } // namespace __cxxabiv1

Implementation sketch (v2):

.. code-block:: cpp

    static bool read_entry(const __cxa_descriptor_table_t* desc,
                           __cxa_contract_violation_field_t field,
                           const __cxa_descriptor_entry_t*& out) {
        const auto* entries = reinterpret_cast<const __cxa_descriptor_entry_t*>(
            reinterpret_cast<const unsigned char*>(desc) + desc->header_size);
        uint16_t n = desc->num_entries;
        if (desc->flags & 0x01) { // sorted
            // binary search by field_type
            uint16_t key = static_cast<uint16_t>(field);
            int l = 0, r = n - 1;
            while (l <= r) {
                int m = (l + r) >> 1;
                uint16_t t = entries[m].field_type;
                if (t < key) l = m + 1; else if (t > key) r = m - 1; else {
                    out = &entries[m];
                    return true;
                }
            }
            return false;
        } else {
            for (uint16_t i = 0; i < n; ++i) {
                if (entries[i].field_type == static_cast<uint16_t>(field)) {
                    out = &entries[i];
                    return true;
                }
            }
            return false;
        }
    }

    bool __cxa_get_contract_violation_field(
        const __cxa_contract_violation_info_t* cv_info,
        __cxa_contract_violation_field_t field,
        void* output_ptr)
    {
        // Direct parameters (not in descriptor)
        switch (field) {
            // If you expose detection_mode or evaluation_semantic via this API,
            // handle them here as direct outputs.
            default: break;
        }

        const auto* desc = cv_info->static_descriptor;
        const auto* base = static_cast<const unsigned char*>(cv_info->static_data);

        const __cxa_descriptor_entry_t* e = nullptr;
        if (!read_entry(desc, field, e)) return false;

        // Bounds check for standard fixed-size fields
        auto within = [&](uint32_t size) {
            return (e->offset <= desc->data_size) &&
                   (desc->data_size - e->offset >= size);
        };

        switch (field) {
            case __cxa_contract_violation_field_t::source_location_ptr: {
                if (!within(sizeof(void*))) return false;
                auto ptr = *reinterpret_cast<const __cxa_source_location* const*>(base + e->offset);
                *static_cast<const __cxa_source_location**>(output_ptr) = ptr;
                return true;
            }
            case __cxa_contract_violation_field_t::source_text_ptr:
            case __cxa_contract_violation_field_t::contract_label_ptr: {
                if (!within(sizeof(void*))) return false;
                auto ptr = *reinterpret_cast<const char* const*>(base + e->offset);
                *static_cast<const char**>(output_ptr) = ptr;
                return true;
            }
            case __cxa_contract_violation_field_t::assertion_kind_u8: {
                if (!within(sizeof(uint8_t))) return false;
                *static_cast<uint8_t*>(output_ptr) = *(base + e->offset);
                return true;
            }
            default:
                return false; // Unknown standard field
        }
    }


std::contract_violation
=======================

.. code-block:: cpp

    namespace std {

    class contract_violation {
    public:
        explicit contract_violation(const __cxxabiv1::__cxa_contract_violation_info_t* info)
            : m_info(info) {}

        source_location location() const {
            const __cxxabiv1::__cxa_source_location* loc = nullptr;
            if (__cxxabiv1::__cxa_get_contract_violation_field(
                    m_info, __cxxabiv1::__cxa_contract_violation_field_t::source_location_ptr, &loc)
                && loc) {
                return source_location{ loc->line, loc->column, loc->file_name, loc->function_name };
            }
            return source_location{};
        }

        string_view comment() const {
            const char* text = nullptr;
            if (__cxxabiv1::__cxa_get_contract_violation_field(
                    m_info, __cxxabiv1::__cxa_contract_violation_field_t::source_text_ptr, &text)
                && text) {
                return string_view{text};
            }
            return string_view{};
        }

        // ... other accessors ...

    private:
        const __cxxabiv1::__cxa_contract_violation_info_t* m_info;
        // Implementations may memoize looked-up offsets for repeated access
    };

    } // namespace std


Dynamic Data (TLV, optional)
============================

Runtimes may pass additional transient data using a simple TLV encoding via
entrypoint dynamic_data. This avoids future ABI surface changes.

.. code-block:: cpp

    struct __cxa_tlv_header {
        uint16_t field_type;  // same encoding rules as descriptor field_type
        uint16_t length;      // length of payload in bytes (0 == terminator when field_type==0)
        // uint8_t payload[length];
    };

    struct __cxa_runtime_data_t {
        const uint8_t* base;  // points to a sequence of (header, payload) ... terminated by (0,0)
    };

- Consumers should ignore unknown TLVs.
- The TLV stream is independent of static_data; it is not covered by data_size.


Advantages
==========

- ABI-stable evolution with field-level extensibility
- True field omission (0 bytes for omitted fields)
- Vendor extensibility without collisions via namespacing
- Compact descriptor; static_data tightly packed but aligned
- Optional indexing for faster lookups; otherwise linear/binary search
- Hardened with bounds and alignment checks


Disadvantages and Trade-offs
============================

- Slightly larger header (16 bytes) versus v1, but negligible compared to code
- More specification detail (alignment, validation)
- Optional index increases complexity, but is purely optional


Deduplication Scope
===================

- Descriptors may be deduplicated by the linker within a link unit (e.g., via
  COMDAT/section folding).
- Deduplication does not occur across DSOs at runtime.


Validation and Debugging
========================

- Runtimes should provide debug-mode validation and assert failures on malformed
  descriptors.
- Pretty-printers can show header, entries, and decode standard fields.


Recommendation
==============

Adopt the v2 descriptor with native-endian encoding, 32-bit offsets, explicit
sizes, and alignment semantics. This preserves the core benefits of the
descriptor approach while addressing portability, safety, and extensibility
concerns.
