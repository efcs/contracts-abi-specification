==========================================
Itanium C++ ABI: Contracts Specification
==========================================

.. contents::
   :local:
   :depth: 2

1. Introduction
===============

This document specifies the Itanium C++ ABI for the Contracts feature introduced
in C++26. The primary goal is to define a portable, stable, and extensible interface
between compilers and runtime libraries for handling contract violations.

When a contract fails at runtime, the compiler generates a call to a runtime
entrypoint function (``__cxa_contract_violation_entrypoint``) which constructs
a ``std::contracts::contract_violation`` object and invokes the user's violation
handler.

2. Overview
===========

2.1 Contract Entrypoint Responsibilities
-----------------------------------------

The contract entrypoint function has the following responsibilities:

* Unpack the compiler-generated contract violation data and use it to construct
  the ``std::contracts::contract_violation`` object.
* Select and call the user-provided contract violation handler, if one is provided,
  or the default handler otherwise.
* If the contract violation has an enforced semantic, terminate the program.

2.2 Design Goals
----------------

The ABI is designed to be:

**Stable**
    Future changes cannot break existing code.

**Extensible**
    The ABI cannot preclude future extensions to the C++ standard or
    vendor-specific extensions.

**Efficient**
    Minimal impact on code generation and code size.

**Portable**
    Works across different compilers (GCC, Clang) and standard libraries
    (libc++, libstdc++).

3. Data Types
==============

3.1 Type Representations
-------------------------

.. list-table::
   :header-rows: 1
   :widths: 40 40 20

   * - Standard Type
     - Itanium Type
     - Underlying
   * - ``std::source_location``
     - ``__cxa_source_location``
     - See §3.2
   * - ``std::assertion_kind``
     - ``__cxa_assertion_kind_t``
     - ``uint8_t``
   * - ``std::evaluation_semantic``
     - ``__cxa_evaluation_semantic_t``
     - ``uint8_t``
   * - ``std::detection_mode``
     - ``__cxa_detection_mode_t``
     - ``uint8_t``

3.2 Source Location
--------------------

.. code-block:: cpp

    struct __cxa_source_location {
        const char* file_name;
        const char* function_name;
        unsigned line;
        unsigned column;
    };

3.3 Enumerations
-----------------

.. code-block:: cpp

    enum __cxa_assertion_kind_t : uint8_t {
        unspecified      = 0x00,
        pre              = 0x01,
        post             = 0x02,
        contract_assert  = 0x03,
    };

    enum __cxa_evaluation_semantic_t : uint8_t {
        unspecified = 0x00,
        enforced    = 0x01,
        observed    = 0x02,
    };

    enum __cxa_detection_mode_t : uint8_t {
        unspecified           = 0x00,
        predicate_false       = 0x01,
        evaluation_exception  = 0x02,
    };

4. Entrypoint Functions
========================

4.1 Generic Entrypoint
-----------------------

The runtime shall provide the following generic entrypoint function:

.. code-block:: cpp

    extern "C"
    void __cxa_contract_violation_entrypoint(void *data);

The ``data`` parameter is a pointer to an object whose layout is
determined by a version number stored in its first byte. The layout for
each version is described using an *exposition-only* type.

**Version 1:**

.. code-block:: cpp

    // exposition only
    struct contract_violation_data_v1 {
        uint8_t                      version;  // = 1
        __cxa_detection_mode_t       mode;
        __cxa_evaluation_semantic_t  semantic;
        __cxa_descriptor_table_t*    static_descriptor;
        void*                        static_data;
    };

**Versioning rules:**

* The ``version`` field shall always be the first byte of the data region.
* New versions shall only **append** fields to the end of the structure.
  The meaning of existing bytes shall never change when the version is
  bumped.
* Older runtimes that encounter an unrecognized version shall process the
  fields they understand and ignore the rest.

This function constructs a ``std::contracts::contract_violation`` object and invokes
the appropriate violation handler.

.. todo::

   Define the linkage and symbol resolution mechanism for
   ``::handle_contract_violation``. The handler may need special
   treatment similar to ``main()`` (e.g., how it is declared, found,
   and replaced).

4.2 Compiler-Generated Wrappers
--------------------------------

Compilers shall emit translation-unit-local wrappers that construct the
versioned data object on the stack and call the generic entrypoint,
reducing each contract call site to a single pointer argument.

Compilers should emit one wrapper per detection mode / evaluation semantic
combination used in the translation unit. Wrappers for the ``enforced``
semantic should be annotated ``[[noreturn]]``, allowing the compiler to
omit fallthrough code at the contract site and enabling better
optimization of the surrounding code:

.. code-block:: cpp

    // Enforced semantic: marked [[noreturn]] so the compiler can omit
    // fallthrough code at the contract site.
    static [[noreturn]] void __cv_v1_pf_se(void *static_data) {
        contract_violation_data_v1 data = {  // exposition only
            .version = 1,
            .mode = predicate_false,
            .semantic = enforced,
            .static_descriptor = &__descriptor_table,
            .static_data = static_data,
        };
        __cxa_contract_violation_entrypoint(&data);
    }

    // Observed semantic: control returns to the contract site after the
    // violation handler executes.
    static void __cv_v1_pf_so(void *static_data) {
        contract_violation_data_v1 data = {  // exposition only
            .version = 1,
            .mode = predicate_false,
            .semantic = observed,
            .static_descriptor = &__descriptor_table,
            .static_data = static_data,
        };
        __cxa_contract_violation_entrypoint(&data);
    }

This reduces each contract call site to:

.. code-block:: nasm

    lea     rdi, [rip + .L_static_data]
    call    __cv_v1_pf_se

Since translation units typically have only 1-2 descriptor tables, the
compiler emits at most 4-8 small wrappers (one per mode/semantic
combination per descriptor), and every contract site becomes a
single-pointer call.

5. Descriptor Table Specification
==================================

5.1 Descriptor Table Structure
-------------------------------

The descriptor table uses parallel arrays to avoid relocations. The
``field_types`` array contains field identifiers, and the ``data`` array
contains corresponding offsets or pointers to extended data:

.. code-block:: cpp

    namespace __cxxabiv1 {

    enum __cxa_vendor_id_t : uint8_t {
        VENDOR_GENERIC = 0x00,
        VENDOR_CLANG   = 0x01,
        VENDOR_GCC     = 0x02,
        VENDOR_MSVC    = 0x03,
    };

    enum __cxa_field_type_t : uint8_t {
        // Standard fields: data[i].offset is a byte offset into static_data
        // where the value of the corresponding type is stored.
        __cxa_field_source_location = 0x11,  // __cxa_source_location
        __cxa_field_source_text     = 0x12,  // const char*
        __cxa_field_assertion_kind  = 0x13,  // __cxa_assertion_kind_t

        // Reserved: 0x14 - 0x3F (future standard fields)

        // Extended fields: data[i].extended_data is a pointer to
        // vendor-specific or future extended information.
        __cxa_field_extended        = 0x40,
    };

    union __cxa_descriptor_data_t {
        uintptr_t offset;        // For standard fields: byte offset into static_data
        void* extended_data;     // For extended fields: pointer to extended info
    };

    struct __cxa_descriptor_table_t {
        uint8_t version   : 4;
        uint8_t vendor_id : 4;
        uint8_t num_entries;
        uint8_t field_types[];
        // Followed by padding to align __cxa_descriptor_data_t
        // Followed by __cxa_descriptor_data_t data[num_entries]
    };

    } // namespace __cxxabiv1

The ``field_types[i]`` value determines how to interpret ``data[i]``:

- If ``field_types[i] < 0x40``: standard field, ``data[i].offset`` is the
  byte offset into ``static_data``
- If ``field_types[i] >= 0x40``: extended field, ``data[i].extended_data``
  points to extended information (requires relocation)

5.2 Default Static Data Layout
-------------------------------

A standard layout for the most common contract data:

.. list-table::
   :header-rows: 1
   :widths: 40 20 40

   * - Type
     - Offset
     - Size
   * - ``__cxa_source_location`` (inline)
     - ``0``
     - ``sizeof(__cxa_source_location)``
   * - ``const char*`` (source text)
     - ``sizeof(__cxa_source_location)``
     - ``sizeof(void*)``
   * - ``__cxa_assertion_kind_t``
     - ``sizeof(__cxa_source_location) + sizeof(void*)``
     - ``sizeof(unsigned char)``

Implementations may omit fields by using explicit descriptor entries that
exclude them.

6. Sample Implementations
==========================

The following pseudocode samples demonstrate how the specification is
used in practice. These are illustrative, not normative.

6.1 Runtime Entrypoint Implementation
---------------------------------------

This sample shows how a runtime library (e.g., libc++abi) implements the
generic entrypoint by reading the versioned data and the descriptor table:

.. code-block:: cpp

    // User-overridable violation handler
    void handle_contract_violation(
        const std::contracts::contract_violation&);

    // Generic entrypoint implementation (in libc++abi / libsupc++)
    extern "C"
    void __cxa_contract_violation_entrypoint(void *data) {
        uint8_t version = *static_cast<uint8_t*>(data);

        // For version 1, interpret as contract_violation_data_v1
        auto *v1 = static_cast<contract_violation_data_v1*>(data);

        // Walk the descriptor table to extract static fields
        auto *desc = v1->static_descriptor;
        auto *sdata = static_cast<const char*>(v1->static_data);

        __cxa_source_location const *loc = nullptr;
        const char *source_text = nullptr;
        __cxa_assertion_kind_t kind = unspecified;

        for (uint8_t i = 0; i < desc->num_entries; ++i) {
            auto offset = desc->data[i].offset;
            switch (desc->field_types[i]) {
            case __cxa_field_source_location:
                loc = reinterpret_cast<const __cxa_source_location*>(
                    sdata + offset);
                break;
            case __cxa_field_source_text:
                source_text = *reinterpret_cast<const char* const*>(
                    sdata + offset);
                break;
            case __cxa_field_assertion_kind:
                kind = *reinterpret_cast<const __cxa_assertion_kind_t*>(
                    sdata + offset);
                break;
            default:
                break;  // ignore unknown fields
            }
        }

        // Construct std::contracts::contract_violation from extracted data
        // (construction is implementation-defined by the standard library)
        std::contracts::contract_violation cv = /* ... */;

        handle_contract_violation(cv);

        if (v1->semantic == enforced)
            std::terminate();
    }

6.2 Compiler-Emitted Code
---------------------------

This sample shows pseudocode equivalent to what a compiler emits for a
translation unit containing contract assertions:

.. code-block:: cpp

    // --- Compiler-emitted per-TU data (in .rodata) ---

    static const __cxa_descriptor_table_t __desc = {
        .version = 1,
        .vendor_id = VENDOR_GENERIC,
        .num_entries = 3,
        .field_types = {
            __cxa_field_source_location,
            __cxa_field_source_text,
            __cxa_field_assertion_kind,
        },
        // data[] array follows with corresponding offsets
    };

    // --- Compiler-emitted per-TU wrappers (internal linkage) ---
    // Name encodes the data layout version (v1) and the mode/semantic.

    static [[noreturn]] void __cv_v1_pf_se(void *static_data) {
        contract_violation_data_v1 data = {
            .version = 1,
            .mode = predicate_false,
            .semantic = enforced,
            .static_descriptor = &__desc,
            .static_data = static_data,
        };
        __cxa_contract_violation_entrypoint(&data);
    }

    // --- Per-contract-site static data (in .rodata) ---

    // For: int foo(int x) pre(x > 0) { ... }
    static const struct {
        __cxa_source_location loc;
        const char *text;
        __cxa_assertion_kind_t kind;
    } __contract_data_foo_pre = {
        .loc = { "foo.cpp", "foo", 42, 0 },
        .text = "x > 0",
        .kind = pre,
    };

    // --- At the contract site ---

    int foo(int x) {
        if (!(x > 0)) {
            __cv_v1_pf_se((void*)&__contract_data_foo_pre);
            __builtin_unreachable();
        }
        // ... function body ...
    }

