=======================================
Itanium C++ ABI: Contracts Specification
=======================================

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
a ``std::contract_violation`` object and invokes the user's violation
handler.

2. Overview
===========

2.1 Contract Entrypoint Responsibilities
-----------------------------------------

The contract entrypoint function has the following responsibilities:

* Unpack the compiler-generated contract violation data and use it to construct
  the ``std::contract_violation`` object.
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
    void __cxa_contract_violation_entrypoint(
        __cxa_descriptor_table_t *static_descriptor,
        void *static_data,
        __cxa_detection_mode_t mode,
        __cxa_evaluation_semantic_t semantic,
        __cxa_runtime_data_t *dynamic_data,
        void *reserved
    );

This function constructs a ``std::contract_violation`` object and invokes
the appropriate violation handler.

4.2 Wrapper Entrypoints
------------------------

To minimize code size at contract sites, the runtime shall provide wrapper
entrypoints that encode common parameter combinations in their names:

.. code-block:: cpp

    extern "C" {

    // predicate_false + enforced
    [[noreturn]] void __cxa_contract_violation_pf_se(
        __cxa_descriptor_table_t *static_descriptor,
        void *static_data);

    // predicate_false + observed
    void __cxa_contract_violation_pf_so(
        __cxa_descriptor_table_t *static_descriptor,
        void *static_data);

    // evaluation_exception + enforced
    [[noreturn]] void __cxa_contract_violation_pe_se(
        __cxa_descriptor_table_t *static_descriptor,
        void *static_data);

    // evaluation_exception + observed
    void __cxa_contract_violation_pe_so(
        __cxa_descriptor_table_t *static_descriptor,
        void *static_data);

    }

**Suffix encoding:**

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Suffix
     - Detection Mode
     - Evaluation Semantic
   * - ``_pf_se``
     - ``predicate_false``
     - ``enforced``
   * - ``_pf_so``
     - ``predicate_false``
     - ``observed``
   * - ``_pe_se``
     - ``evaluation_exception``
     - ``enforced``
   * - ``_pe_so``
     - ``evaluation_exception``
     - ``observed``

Wrappers with ``_se`` (enforced semantic) are marked ``[[noreturn]]``.

4.3 Compiler-Generated Wrappers
--------------------------------

Compilers should emit translation-unit-local wrappers that capture the
descriptor table pointer, reducing each contract call site to a single
pointer argument:

.. code-block:: cpp

    // Compiler-generated per-TU wrapper (internal linkage)
    static [[noreturn]] void contract_violation_pf_se(void *static_data) {
        __cxa_contract_violation_pf_se(
            &__descriptor_table,  // TU's descriptor table
            static_data);
    }

This reduces each contract call site to:

.. code-block:: asm

    lea     rdi, [rip + .L_static_data]
    call    contract_violation_pf_se

Since translation units typically have only 1-2 descriptor tables, the
compiler emits 4-8 small wrappers (one per mode/semantic combination per
descriptor), and every contract site becomes a single-pointer call.

See :doc:`code-size-comparison` for analysis of code size impact.

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
        // Standard fields (no relocation needed)
        field_summary         = 0x01,  // Default layout at offset 0
        field_source_location = 0x11,  // __cxa_source_location inline
        field_source_text     = 0x12,  // const char*
        field_assertion_kind  = 0x13,  // __cxa_assertion_kind_t

        // Reserved: 0x14 - 0x3F

        // Extended/vendor fields (may need relocation)
        field_extended        = 0x40,
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

5.3 Default Static Data Layout
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

6. Contract Violation Accessor API
===================================

The accessor API provides a standardized interface for retrieving individual
fields from an opaque ``std::contract_violation`` object. This API
is used internally by the runtime and is not exposed to user code.

6.1 Field Enumeration
----------------------

The accessor API uses the same field type values as the descriptor table
(``__cxa_field_type_t``), plus additional values for fields passed as
entrypoint parameters:

.. code-block:: cpp

    namespace __cxxabiv1 {

    // Additional field types for accessor API (not in descriptor table)
    enum {
        field_evaluation_semantic = 0x04,  // from entrypoint parameter
        field_detection_mode      = 0x05,  // from entrypoint parameter
    };

    } // namespace __cxxabiv1

6.2 Contract Violation Data Structure
--------------------------------------

The entrypoint function receives multiple pieces of contract violation data
as separate parameters. To facilitate internal processing, a structure is
defined to aggregate all entrypoint parameters into a single object:

.. code-block:: cpp

    namespace __cxxabiv1 {

    /**
     * Aggregated contract violation data structure.
     *
     * This structure is typically constructed by the entrypoint function to
     * package all contract violation information received from the compiler.
     * It serves as the canonical representation of a contract violation for
     * internal runtime use.
     */
    struct __cxa_contract_violation_data_t {
        // Static descriptor and data
        __cxa_descriptor_table_t* static_descriptor;
        void* static_data;

        // Detection mode (always present)
        __cxa_detection_mode_t mode;

        // Evaluation semantic (always present)
        __cxa_evaluation_semantic_t semantic;

        // Dynamic data (may be nullptr)
        __cxa_runtime_data_t* dynamic_data;

        // Reserved for future extensions (currently unused)
        void* reserved;
    };

    } // namespace __cxxabiv1

6.3 Accessor Function
----------------------

The accessor function provides a uniform interface to retrieve individual
fields from the aggregated contract violation data. This allows the runtime
to query specific information without needing to understand the internal
layout of the descriptor table and static data.

.. code-block:: cpp

    namespace __cxxabiv1 {

    /**
     * Retrieve a field from contract violation data.
     *
     * @param cv_data    Pointer to contract violation data structure
     * @param field      Field identifier to retrieve
     * @param output_ptr Pointer to receive the field value
     *
     * @return true if the field was successfully retrieved, false otherwise
     *         (e.g., if the field is not present or not supported)
     */
    bool __cxa_get_contract_violation_field(
        const __cxa_contract_violation_data_t* cv_data,
        contract_violation_field_t field,
        void* output_ptr
    );

    } // namespace __cxxabiv1

**Implementation Notes:**

* The function shall interpret the ``static_descriptor`` and
  ``static_data`` fields to locate statically-known contract
  information such as source location, source text, and assertion kind.
* The function shall return values from ``mode`` and
  ``semantic`` fields directly when those fields are requested.
* The function shall interpret ``dynamic_data`` when present to
  extract runtime-dependent information (reserved for future use).
* The function shall return ``false`` if a requested field is not
  present in the contract violation data.

6.4 Field Type Specifications
------------------------------

The ``output_ptr`` parameter must point to a variable of the
appropriate type for the requested field:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Field
     - Output Type
   * - ``field_source_location``
     - ``const __cxa_source_location*``
   * - ``field_source_text``
     - ``const char**``
   * - ``field_assertion_kind``
     - ``__cxa_assertion_kind_t*``
   * - ``field_evaluation_semantic``
     - ``__cxa_evaluation_semantic_t*``
   * - ``field_detection_mode``
     - ``__cxa_detection_mode_t*``

6.5 Usage Example
------------------

.. code-block:: cpp

    // User-overridable violation handler (weak symbol in standard library)
    [[noreturn]] void handle_contract_violation(
        const std::contract_violation&);

    // Runtime wrapper entrypoint implementation
    extern "C" [[noreturn]]
    void __cxa_contract_violation_pf_se(
        __cxa_descriptor_table_t *static_descriptor,
        void *static_data)
    {
        __cxa_contract_violation_data_t cv_data = {
            .static_descriptor = static_descriptor,
            .static_data = static_data,
            .mode = predicate_false,
            .semantic = enforced,
            .dynamic_data = nullptr,
            .reserved = nullptr
        };

        // Construct std::contract_violation and invoke handler
        std::contract_violation cv(&cv_data);
        handle_contract_violation(cv);
    }

