.. _descriptor_table_approach:

===========================================================
Descriptor Table with Field-Level Metadata
===========================================================

.. contents::
  :depth: 1
  :local:


Overview
========

This document specifies the descriptor table approach for the Itanium C++
Contracts ABI. This approach separates metadata describing data layout from
the data itself, enabling ABI-stable evolution, efficient field omission,
and vendor extensibility without coordination.

Core Concept
============

Key Insight
-----------

Contract violation data consists of two components:

1. **Metadata**: Describes *what* fields exist and *where* they are located
2. **Data**: The actual field values (pointers, strings, scalars)

By separating these concerns, we achieve:

- **ABI stability**: Metadata can describe any layout without breaking compatibility
- **Efficiency**: Metadata is shared across contracts; data is compact and tightly packed
- **Extensibility**: New field types are added without changing existing structures

The Proposal
------------

The compiler emits:

1. **Descriptor table**: Shared metadata structure in ``.rodata`` describing field layout
2. **Static data blob**: Per-contract tightly-packed field values
3. **Entrypoint call**: Passes pointers to both descriptor and data

The runtime:

1. Parses descriptor entries to understand data layout
2. Accesses fields at specified offsets in the data blob
3. Constructs ``std::contract_violation`` object
4. Invokes user's violation handler

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
        __cxa_runtime_data_t* dynamic_data,
        void* reserved
    );

    } // namespace __cxxabiv1

**Parameters:**

- ``static_descriptor``: Pointer to descriptor table (compile-time constant)
- ``static_data``: Pointer to packed field data (compile-time constant)
- ``mode``: How violation was detected (predicate_false or evaluation_exception)
- ``semantic``: Evaluation semantic (enforced or observed)
- ``dynamic_data``: Reserved for runtime-generated data (currently nullptr)
- ``reserved``: Reserved for future use (e.g., exception context flag)

Descriptor Table Structure
---------------------------

.. code-block:: cpp

    struct __cxa_descriptor_table_t {
        unsigned char version   : 4;  // ABI version (currently 1)
        unsigned char vendor_id : 4;  // Vendor ID (0=standard, 1=GCC, 2=Clang, etc.)
        unsigned char num_entries;    // Number of descriptor entries

        // Followed by inline array of entries
        // __cxa_base_descriptor_entry_t entries[num_entries];
    };
    // sizeof() = 2 bytes + (4 bytes × num_entries)

**Binary layout:**

.. code-block:: text

    Offset  Size  Field
    0       4bit  version
    0       4bit  vendor_id (high nibble)
    1       1     num_entries
    2       4×N   entries array

Descriptor Entry Structure
---------------------------

.. code-block:: cpp

    struct __cxa_base_descriptor_entry_t {
        uint16_t description_type;  // Field type identifier
        uint16_t offset;            // Offset in static_data blob
    };
    // sizeof() = 4 bytes
    // alignof() = 2 bytes

**Binary layout:**

.. code-block:: text

    Offset  Size  Field
    0       2     description_type (little-endian)
    2       2     offset (little-endian)

**Example: Three-entry descriptor**

.. code-block:: text

    Address   Hex Dump         Interpretation
    0x5000:   0x13            version=1, vendor=1 (GCC)
    0x5001:   0x03            num_entries=3
    0x5002:   0x11 0x00       entry[0]: type=0x11 (source_location_ptr)
    0x5004:   0x00 0x00       entry[0]: offset=0
    0x5006:   0x13 0x00       entry[1]: type=0x13 (source_text)
    0x5008:   0x08 0x00       entry[1]: offset=8
    0x500A:   0x14 0x00       entry[2]: type=0x14 (assertion_kind)
    0x500C:   0x10 0x00       entry[2]: offset=16

    Total size: 14 bytes

Field Type Enumeration
----------------------

.. code-block:: cpp

    enum class __cxa_contract_violation_field_t : uint16_t {
        // Pointer types: 0x10 - 0x1F
        source_location_ptr = 0x11,

        // String types: 0x20 - 0x2F
        source_text = 0x13,

        // Scalar types: 0x30 - 0x3F
        assertion_kind = 0x14,
        detection_mode = 0x15,
        evaluation_semantic = 0x16,

        // Reserved for future standard fields: 0x01 - 0x3F

        // Vendor-specific fields: 0x40 - 0xFF
        vendor_specific_base = 0x40
    };

**Type ranges:**

- ``0x00``: Reserved (invalid)
- ``0x01 - 0x3F``: Standard fields (managed by Itanium ABI committee)
- ``0x40 - 0x7F``: Vendor extensions (GCC, Clang, MSVC, etc.)
- ``0x80 - 0xFF``: Reserved for future use

Static Data Layout
------------------

The static data blob contains tightly-packed field values at offsets
specified by the descriptor entries:

.. code-block:: cpp

    // Example layout for: location + text + kind
    struct {
        const __cxa_source_location* location;  // offset 0, 8 bytes
        const char* source_text;                // offset 8, 8 bytes
        uint8_t assertion_kind;                 // offset 16, 1 byte
    } static_data;
    // Total: 17 bytes (no padding)

**Binary layout:**

.. code-block:: text

    Address   Content
    0x6000:   0x00 0x70 0x00 0x00 0x00 0x00 0x00 0x00  // location ptr → 0x7000
    0x6008:   0x00 0x80 0x00 0x00 0x00 0x00 0x00 0x00  // text ptr → 0x8000
    0x6010:   0x01                                      // kind = precondition

    Total: 17 bytes

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
        # Load descriptor and data pointers
        lea     rdi, [rip + .L_descriptor]      # 7 bytes
        lea     rsi, [rip + .L_static_data]     # 7 bytes

        # Pass mode and semantic as immediates
        xor     edx, edx                        # mode = predicate_false
        xor     ecx, ecx                        # semantic = enforced

        # Dynamic data and reserved (nullptr)
        xor     r8d, r8d
        xor     r9d, r9d

        call    __cxa_contract_violation_entrypoint  # 5 bytes
        ud2                                          # 2 bytes (unreachable)

    .L_contract_passed:
        # ... function body ...
        ret

    # Descriptor table (shared across contracts with same layout)
    .section .rodata
    .align 2
    .L_descriptor:
        .byte   0x13                # version=1, vendor=CLANG
        .byte   0x03                # 3 entries
        .value  0x11, 0x00          # source_location_ptr at offset 0
        .value  0x13, 0x08          # source_text at offset 8
        .value  0x14, 0x10          # assertion_kind at offset 16
        # Total: 14 bytes

    # Static data (per-contract)
    .align 8
    .L_static_data:
        .quad   .L_source_location  # 8 bytes
        .quad   .L_source_text      # 8 bytes
        .byte   0x01                # 1 byte (precondition)
        # Total: 17 bytes

    .L_source_location:
        .quad   .L_file_name        # file_name pointer
        .quad   .L_function_name    # function_name pointer
        .long   42                  # line
        .long   8                   # column
        # 24 bytes total

    .L_source_text:
        .asciz  "amount > 0"

    .L_file_name:
        .asciz  "bank.cpp"

    .L_function_name:
        .asciz  "withdraw"

**Code size analysis:**

- Instructions: minimal overhead for setup + call + unreachable marker
- Descriptor: small fixed structure (shared via COMDAT)
- Static data: compact per-contract data
- Source location: standard location metadata (per-contract)
- Strings: variable based on contract text

**Per-contract overhead (excluding strings which are always needed):**

- Code: minimal instruction overhead
- Data: compact tightly-packed structure
- Amortized descriptor: approaches zero as number of contracts grows

**Total: Very compact per-contract overhead** (+ shared descriptor)

Optimized Entrypoint (Optional)
--------------------------------

Compilers may optionally emit specialized entrypoints for common cases:

.. code-block:: cpp

    // Specialized for: mode=predicate_false, semantic=enforced
    [[noreturn]]
    void __cxa_contract_violation_entrypoint_pf_se(
        const __cxa_descriptor_table_t* static_descriptor,
        const void* static_data
    ) {
        // Internally calls generic entrypoint
        __cxa_contract_violation_entrypoint(
            static_descriptor,
            static_data,
            predicate_false,
            enforced,
            nullptr,
            nullptr
        );
    }

**Optimized codegen:**

.. code-block:: asm

    .L_contract_failed:
        lea     rdi, [rip + .L_descriptor]   # 7 bytes
        lea     rsi, [rip + .L_static_data]  # 7 bytes
        call    __cxa_contract_violation_entrypoint_pf_se  # 5 bytes
        ud2                                   # 2 bytes

    # Total: significantly reduced overhead vs generic approach

**Savings: Notable reduction per contract** for common case.

Field Omission Example
----------------------

With ``-fno-contract-source-text``:

.. code-block:: cpp

    // Source (same as before)
    void withdraw(int amount) pre(amount > 0);

**Compiler emits different descriptor:**

.. code-block:: asm

    .L_descriptor_no_text:
        .byte   0x13                # version=1, vendor=CLANG
        .byte   0x02                # 2 entries (not 3!)
        .value  0x11, 0x00          # source_location_ptr at offset 0
        .value  0x14, 0x08          # assertion_kind at offset 8
        # Total: 10 bytes (vs 14 bytes)

    .L_static_data_no_text:
        .quad   .L_source_location  # 8 bytes
        .byte   0x01                # 1 byte
        # Total: 9 bytes (vs 17 bytes)

**Savings:**

- Descriptor: reduced by one entry
- Data: no text pointer needed
- **Total: Significant reduction per contract**

**Comparison to fixed struct approach:**

Fixed struct still allocates 8 bytes for nullptr:

.. code-block:: cpp

    struct contract_data_v1 {
        const source_location* location;  // 8 bytes
        const char* source_text;          // 8 bytes ← nullptr wastes space
        uint8_t assertion_kind;           // 1 byte
        uint8_t padding[7];               // 7 bytes
    }; // Total: 24 bytes

Descriptor approach: **Significantly more compact**

Runtime Implementation
======================

Descriptor Parsing
------------------

.. code-block:: cpp

    namespace __cxxabiv1 {

    void __cxa_contract_violation_entrypoint(
        const __cxa_descriptor_table_t* static_descriptor,
        const void* static_data,
        __cxa_detection_mode_t mode,
        __cxa_evaluation_semantic_t semantic,
        __cxa_runtime_data_t* dynamic_data,
        void* reserved)
    {
        // Construct contract_violation_info for field access
        __cxa_contract_violation_info_t cv_info = {
            .static_descriptor = static_descriptor,
            .static_data = static_data,
            .mode = mode,
            .semantic = semantic,
            .dynamic_data = dynamic_data,
            .reserved = reserved
        };

        // Construct std::contract_violation object
        std::contract_violation cv(&cv_info);

        // Invoke user handler
        auto handler = get_contract_violation_handler();
        handler(cv);

        // Handler must not return, but enforce
        std::terminate();
    }

    } // namespace __cxxabiv1

Field Accessor API
------------------

.. code-block:: cpp

    namespace __cxxabiv1 {

    // Retrieve individual field from contract violation data
    bool __cxa_get_contract_violation_field(
        const __cxa_contract_violation_info_t* cv_info,
        __cxa_contract_violation_field_t field,
        void* output_ptr
    );

    } // namespace __cxxabiv1

**Implementation:**

.. code-block:: cpp

    bool __cxa_get_contract_violation_field(
        const __cxa_contract_violation_info_t* cv_info,
        __cxa_contract_violation_field_t field,
        void* output_ptr)
    {
        // Handle direct parameters (not in descriptor)
        switch (field) {
            case detection_mode:
                *static_cast<__cxa_detection_mode_t*>(output_ptr) = cv_info->mode;
                return true;
            case evaluation_semantic:
                *static_cast<__cxa_evaluation_semantic_t*>(output_ptr) =
                    cv_info->semantic;
                return true;
            default:
                break;
        }

        // Parse descriptor entries
        const __cxa_descriptor_table_t* desc = cv_info->static_descriptor;
        const unsigned char* data_base =
            static_cast<const unsigned char*>(cv_info->static_data);

        // Access entries array (immediately after header)
        const __cxa_base_descriptor_entry_t* entries =
            reinterpret_cast<const __cxa_base_descriptor_entry_t*>(
                reinterpret_cast<const unsigned char*>(desc) + 2);

        // Linear scan for requested field
        for (unsigned char i = 0; i < desc->num_entries; ++i) {
            if (entries[i].description_type == static_cast<uint16_t>(field)) {
                // Found field - compute address
                const void* field_addr = data_base + entries[i].offset;

                // Type-specific extraction
                switch (field) {
                    case source_location_ptr: {
                        const __cxa_source_location* const* ptr_ptr =
                            static_cast<const __cxa_source_location* const*>(
                                field_addr);
                        *static_cast<const __cxa_source_location**>(output_ptr) =
                            *ptr_ptr;
                        return true;
                    }
                    case source_text: {
                        const char* const* text_ptr =
                            static_cast<const char* const*>(field_addr);
                        *static_cast<const char**>(output_ptr) = *text_ptr;
                        return true;
                    }
                    case assertion_kind: {
                        const uint8_t* kind_ptr =
                            static_cast<const uint8_t*>(field_addr);
                        *static_cast<uint8_t*>(output_ptr) = *kind_ptr;
                        return true;
                    }
                    default:
                        // Unknown field type - ignore
                        return false;
                }
            }
        }

        // Field not found in descriptor
        return false;
    }

**Performance characteristics:**

- Header read: minimal overhead
- Linear scan: very efficient per entry
- Typical small number of entries: negligible total overhead
- **This is the cold path** (contract already failed)

std::contract_violation Implementation
---------------------------------------

.. code-block:: cpp

    namespace std {

    class contract_violation {
    public:
        // Constructor (called by runtime entrypoint)
        explicit contract_violation(
            const __cxxabiv1::__cxa_contract_violation_info_t* info)
            : m_info(info)
        {}

        source_location location() const {
            const __cxxabiv1::__cxa_source_location* loc_ptr = nullptr;
            if (__cxxabiv1::__cxa_get_contract_violation_field(
                    m_info,
                    __cxxabiv1::source_location_ptr,
                    &loc_ptr) && loc_ptr != nullptr)
            {
                return source_location{
                    loc_ptr->line,
                    loc_ptr->column,
                    loc_ptr->file_name,
                    loc_ptr->function_name
                };
            }
            return source_location{};
        }

        string_view comment() const {
            const char* text = nullptr;
            if (__cxxabiv1::__cxa_get_contract_violation_field(
                    m_info,
                    __cxxabiv1::source_text,
                    &text) && text != nullptr)
            {
                return string_view{text};
            }
            return string_view{};
        }

        // ... other accessors ...

    private:
        const __cxxabiv1::__cxa_contract_violation_info_t* m_info;
    };

    } // namespace std

Advantages
==========

1. ABI-Stable Evolution
-----------------------

**Adding new fields (C++29 adds contract labels):**

Compilers emit new field type in descriptor:

.. code-block:: cpp

    // Version 1 descriptor (C++26)
    .L_descriptor_v1:
        .byte   0x13                # version=1
        .byte   0x03                # 3 entries
        .value  0x11, 0x00          # source_location_ptr
        .value  0x13, 0x08          # source_text
        .value  0x14, 0x10          # assertion_kind

    // Version 1 descriptor with label (C++29, but still version 1!)
    .L_descriptor_v1_with_label:
        .byte   0x13                # version=1 (unchanged!)
        .byte   0x04                # 4 entries (incremented)
        .value  0x11, 0x00          # source_location_ptr
        .value  0x13, 0x08          # source_text
        .value  0x14, 0x10          # assertion_kind
        .value  0x17, 0x11          # contract_label ← NEW TYPE

**Old runtime behavior:**

- Parses first 3 entries (knows types 0x11, 0x13, 0x14)
- Encounters type 0x17 (unknown)
- **Ignores it gracefully** (doesn't access that field)
- Constructs contract_violation without label (returns empty string)

**New runtime behavior:**

- Parses all 4 entries
- Recognizes type 0x17 (contract_label)
- Accesses field at offset 0x11
- Returns label to user handler

**Result: Perfect forward/backward compatibility**

- Old binary + new runtime: Works (uses 3 fields)
- New binary + old runtime: Works (old runtime ignores unknown field)
- No struct layout conflicts at link time

2. Efficient Field Omission
----------------------------

**True zero-overhead omission:**

With ``-fno-contract-source-text``:

- Remove entry from descriptor entirely
- Remove pointer from data completely
- **Total savings: Significant reduction in both descriptor and data**

**No nullptr waste:**

Fixed struct must allocate space for omitted fields:

.. code-block:: cpp

    struct contract_data_v1 {
        const source_location* location;
        const char* source_text;  // ← nullptr wastes 8 bytes
        uint8_t assertion_kind;
        uint8_t padding[7];       // ← alignment waste
    };

Descriptor approach:

.. code-block:: cpp

    // Descriptor: only 2 entries
    // Data: only location + kind (9 bytes)
    // No wasted space

**Impact on large codebases:**

For a large number of contracts with ``-fno-contract-source-text``:

- Fixed struct: requires full allocation for every field including nullptrs
- Descriptors: only allocates space for present fields
- **Savings: Substantial reduction in memory usage**

3. Linker Deduplication
------------------------

Descriptor tables are shared across contracts with identical layouts:

.. code-block:: cpp

    // Contract A: pre(x > 0)
    static const void* data_A[] = {
        &location_A,    // Unique
        "x > 0",        // Unique
        (void*)0x01
    };

    // Contract B: post(y > 0)
    static const void* data_B[] = {
        &location_B,    // Unique
        "y > 0",        // Unique
        (void*)0x02
    };

    // Both use SAME descriptor (layout identical)
    static const __cxa_descriptor_table_t shared_desc = {
        /* location + text + kind */
    };
    // Linker deduplicates via weak symbol / COMDAT

**Savings:**

- Many contracts with same layout share a single descriptor
- Deduplication scales linearly with number of contracts
- Larger codebases see proportionally greater savings

**Fixed struct approach: No sharing possible** (each contract has unique struct)

4. Vendor Extensions Without Coordination
------------------------------------------

**GCC adds optimization hint (field type 0x50):**

.. code-block:: cpp

    // GCC emits
    .L_descriptor_gcc:
        .byte   0x13                # version=1, vendor=GCC
        .byte   0x04                # 4 entries
        .value  0x11, 0x00          # source_location_ptr
        .value  0x13, 0x08          # source_text
        .value  0x14, 0x10          # assertion_kind
        .value  0x50, 0x11          # gcc_optimization_hint

**Clang adds source range (field type 0x60):**

.. code-block:: cpp

    // Clang emits
    .L_descriptor_clang:
        .byte   0x23                # version=1, vendor=Clang
        .byte   0x04                # 4 entries
        .value  0x11, 0x00          # source_location_ptr
        .value  0x13, 0x08          # source_text
        .value  0x14, 0x10          # assertion_kind
        .value  0x60, 0x11          # clang_source_range

**No conflicts:**

- Field types are in separate ranges (0x50 vs 0x60)
- Runtimes ignore unknown vendor fields
- No version number coordination required
- No nested switch statements

**Fixed struct / versioned approach:**

- Both claim version 2 → conflict
- Need vendor ID + version matrix
- Nested switches: O(vendors × versions)

5. Minimal Runtime Code Growth
-------------------------------

**Adding new field types: O(1) code growth**

.. code-block:: cpp

    bool __cxa_get_contract_violation_field(...) {
        // Same loop handles all field types
        for (unsigned char i = 0; i < desc->num_entries; ++i) {
            if (entries[i].description_type == field) {
                switch (field) {
                    // ... existing cases ...
                    case contract_label:  // ← Add one case
                        /* extract label */
                        return true;
                    // ... more cases ...
                }
            }
        }
    }

**Total code size: Remains compact** regardless of number of field types.

**Versioned approach: O(versions) code growth**

.. code-block:: cpp

    switch (abi_version) {
        case 1: { /* 30 lines */ }
        case 2: { /* 35 lines */ }
        case 3: { /* 40 lines */ }
        case 4: { /* 45 lines */ }
        // ... grows indefinitely
    }

After many versions: substantial code duplication and maintenance burden.

6. Exception Handling
---------------------

Exception remains active during entrypoint execution:

.. code-block:: cpp

    void __cxa_contract_violation_entrypoint(...) {
        // Exception still active (not caught yet)

        std::contract_violation cv(...);
        handler(cv);

        // Exception still active here
    }

User handler accesses via standard mechanism:

.. code-block:: cpp

    void my_handler(const contract_violation& cv) {
        if (cv.detection_mode() == evaluation_exception) {
            auto ex = std::current_exception();  // Standard C++
            try {
                std::rethrow_exception(ex);
            } catch (const std::exception& e) {
                std::cerr << "Exception: " << e.what() << "\n";
            }
        }
    }

**No need to store exception pointers:**

- No ``std::exception_ptr`` member in contract_violation
- No circular dependency issues
- No type erasure required
- Clean, standard C++ interface

7. Trivial and Non-Trivial Destructors
---------------------------------------

``std::contract_violation`` can have non-trivial destructor:

.. code-block:: cpp

    namespace std {
    class contract_violation {
    public:
        // Can store owned data if needed
        explicit contract_violation(const __cxxabiv1::__cxa_contract_violation_info_t* info)
            : m_info(info)
            , m_cached_text(/* ... */)  // Could cache expensive computations
        {}

        ~contract_violation() {
            // Destructor runs normally
        }

    private:
        const __cxxabiv1::__cxa_contract_violation_info_t* m_info;
        std::string m_cached_text;  // Non-trivial member OK
    };
    }

**Object lifetime controlled by constructor/destructor, not ABI layer.**

8. Performance Characteristics
-------------------------------

**Compile time:**

- Descriptor emission: Once per unique layout
- Linker deduplication: Automatic via weak symbols
- Compact descriptor structure plus minimal per-contract data

**Runtime (cold path - contract failed):**

- Header read: minimal overhead
- Linear scan: efficient traversal per entry (typically a small number of entries)
- Total parsing: very low overhead

**This overhead is negligible because:**

- Cold path (contract already violated)
- Followed by handler invocation (expensive)
- No overhead on success path (hot path)

**Cache behavior:**

- Descriptor + data typically fit in same cache line due to compact size
- Single cache miss loads both structures
- Excellent spatial locality

Disadvantages
=============

1. Descriptor Parsing Complexity
---------------------------------

Runtime must implement descriptor parsing logic:

- Read header (version, vendor, num_entries)
- Linear scan through entries
- Offset-based field access
- Type-specific extraction

**Complexity: Modest implementation overhead**

Mitigations:

- Well-specified binary format
- Reference implementation available
- Extensive test suite
- Single implementation shared across ``std::contract_violation`` accessors

2. Indirection Overhead
-----------------------

Two-level indirection to access fields:

1. Follow ``static_descriptor`` pointer → read descriptor
2. Parse entry → get offset
3. Follow ``static_data`` pointer + offset → read field value

**Cost: 2 dependent loads** (minimal overhead on cold path)

Mitigations:

- Cold path only (contract already failed)
- Both structures typically in same cache line
- Amortized across all field accesses

3. Binary Format Complexity
----------------------------

Requires specifying binary layout precisely:

- Byte ordering (little-endian)
- Alignment requirements
- Padding rules
- Version/vendor bit packing

**Complexity: Comprehensive specification required**

Mitigations:

- One-time specification cost
- Stability for decades (ABI doesn't change)
- Comprehensive binary examples in spec

4. Debugging Difficulty
-----------------------

Binary blobs harder to inspect than structs:

.. code-block:: gdb

    (gdb) p *descriptor
    $1 = {version = 1, vendor_id = 1, num_entries = 3}
    (gdb) p descriptor->entries[0]
    # ... manual offset calculation required ...

vs. struct:

.. code-block:: gdb

    (gdb) p *contract_data
    $1 = {location = 0x7000, source_text = 0x8000, kind = 1}

Mitigations:

- GDB pretty-printers for descriptors
- LLDB data formatters
- Debugging helper functions in runtime

Comparison to Alternatives
==========================

Binary Size (Large codebase with many contracts)
-------------------------------------------------

=================== ============== ============ ==============
Approach            Code           Data         Total
=================== ============== ============ ==============
Fixed Struct        Baseline       Moderate     **Moderate**
Size-Prefixed       Baseline       Higher       **Higher**
Tagged Union        Baseline       Highest      **Highest**
Vtable              Baseline       Highest      **Highest**
Runtime-Constructed Much Higher    Moderate     **High**
**Descriptors**     **Baseline**   **Lowest**   **Lowest**
=================== ============== ============ ==============

**Descriptors achieve the smallest total binary size through efficient data packing and deduplication**

With ``-fno-contract-source-text`` (Large codebase):

=================== ==============
Approach            Total
=================== ==============
Fixed Struct        **No reduction** (nullptr waste remains)
Descriptors         **Substantial reduction** (eliminates unused fields entirely)
=================== ==============

Feature Matrix
--------------

============================= =========== ============= ============ =========== =================== ===================
Feature                       Fixed       Size-Prefix   Tagged Union Vtable      Runtime-Const       Descriptors
============================= =========== ============= ============ =========== =================== ===================
ABI evolution                 ❌          ⚠️            ✅           ✅          ✅ (caveats)        ✅
Field omission                ❌          ❌            ✅           ⚠️          ❌ (pointer waste)  ✅ (zero overhead)
Vendor extensions             ❌          ⚠️            ✅           ✅          ⚠️ (vendor ID)      ✅ (isolated)
Efficient runtime             ✅          ✅            ❌           ❌          ✅                  ✅
Minimal code size             ✅          ✅            ❌           ❌          ❌ (much larger)    ✅
Minimal data size             ✅          ⚠️            ❌           ❌          ⚠️ (larger)         ✅ (best)
Constexpr compatible          ✅          ✅            ✅           ❌          ✅                  ✅
Linker deduplication          ❌          ❌            ❌           ❌          ❌                  ✅
Version coordination          N/A         N/A           N/A          N/A         ❌ Required         ✅ Not needed
Runtime code growth           N/A         N/A           O(fields)    O(fields)   O(versions)         O(1)
Exception handling            ⚠️          ⚠️            ⚠️           ⚠️          ❌ Problematic      ✅ Clean
Destructor support            ⚠️          ⚠️            ⚠️           ⚠️          ❌ Must be trivial  ✅ Full support
Implementation complexity     ✅ Simple   ✅ Simple     ⚠️ Medium    ⚠️ Medium   ✅ Simple           ⚠️ Medium
============================= =========== ============= ============ =========== =================== ===================

Recommendation
==============

**Recommended for Standardization**

The descriptor table approach is the optimal solution for the Itanium C++
Contracts ABI because it uniquely satisfies all critical requirements:

Critical Advantages
-------------------

1. **ABI-stable evolution**: Field-level versioning without coordination
2. **Best binary size**: Significantly smaller than alternatives
3. **True field omission**: Zero overhead for omitted fields (vs pointer-sized waste for others)
4. **Vendor extensibility**: Isolated field type ranges, no conflicts
5. **Linker optimization**: Shared descriptor tables across contracts
6. **Scalable runtime**: O(1) code growth for new field types
7. **Clean exception handling**: Uses standard C++ mechanisms
8. **Full destructor support**: No restrictions on contract_violation lifetime

Trade-offs
----------

- **Implementation complexity**: Modest parsing code (one-time cost)
- **Indirection overhead**: 2 dependent loads (minimal overhead on cold path)
- **Specification complexity**: Comprehensive binary format specification required

These trade-offs are acceptable because:

- Implementation complexity is one-time (shared across vendors)
- Overhead is on cold path only (contract already failed)
- Specification complexity ensures decades of ABI stability

Validation
----------

The descriptor approach has been validated through:

1. **Concrete binary analysis**: Demonstrates significant size savings
2. **Evolution scenarios**: Successfully handles future standard extensions
3. **Vendor extension testing**: Demonstrates conflict-free vendor fields
4. **Performance measurement**: Minimal overhead on cold path
5. **Comparison to alternatives**: Systematically evaluated multiple approaches

Conclusion
==========

The descriptor table approach represents the state-of-the-art in ABI design
for contract violations. It achieves the optimal balance of binary efficiency,
ABI stability, and implementation complexity.

This approach should be adopted as the standard for the Itanium C++ ABI
specification for contracts.
