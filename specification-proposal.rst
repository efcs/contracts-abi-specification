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

4. Entrypoint Function Signature
=================================

The runtime shall provide the following entrypoint function:

.. code-block:: cpp

    extern "C"
    void __cxa_contract_violation_entrypoint(
        // Static descriptor and data
        __cxa_descriptor_t *static_descriptor,
        void *static_data,

        // Detection mode (always required)
        __cxa_detection_mode_t mode,

        // Evaluation semantic (may be runtime-determined in future)
        __cxa_evaluation_semantic_t semantic,

        // Dynamic data (optional, may be nullptr)
        __cxa_runtime_data_t *dynamic_data,

        // Reserved for future extensions
        void *reserved
    );

Compilers shall call this function when a contract violation occurs. The function
is responsible for constructing the ``std::contract_violation`` object
and invoking the appropriate violation handler.

.. note::
   Compilers may implement optimizations such as specialized wrapper overloads
   that encode common parameter combinations in their names. Such optimizations are not
   part of this ABI specification. For guidance on implementing these optimizations, see
   the Compiler Optimization Guide.

5. Descriptor Table Specification
==================================

5.1 Descriptor Table Structure
-------------------------------

The descriptor table identifies the layout and contents of the static data:

.. code-block:: cpp

    namespace __cxxabiv1 {

    enum vendor_id_t : unsigned char {
        VENDOR_GENERIC = 0x00,
        VENDOR_CLANG   = 0x01,
        VENDOR_GCC     = 0x02,
        VENDOR_MSVC    = 0x03,
        // Future vendors: 0x04 - 0x0F
    };

    struct __cxa_descriptor_table_t {
        unsigned char version   : 4;  // Version for future extensions
        unsigned char vendor_id : 4;  // Vendor ID
        unsigned char num_entries;    // Number of descriptor entries
        __cxa_base_descriptor_entry_t *entries[];  // Array of entry pointers
    };

    } // namespace __cxxabiv1

5.2 Descriptor Entry Types
---------------------------

.. code-block:: cpp

    namespace __cxxabiv1 {

    enum contract_violation_field_t : unsigned char {
        // Default summary containing common fields
        summary = 0x01,

        // Individual builtin field types
        source_location = 0x11,  // __cxa_source_location embedded inline
        source_text     = 0x12,
        assertion_kind  = 0x13,

        // Reserved for future standard fields: 0x14 - 0x2F

        // Extended descriptor (for complex future extensions)
        extended = 0x30,

        // Vendor-specific descriptors
        vendor = 0x40,
    };

    struct __cxa_base_descriptor_entry_t {
        contract_violation_field_t description_type;
        uint16_t offset;  // Offset in static data
    };

    struct __cxa_extended_descriptor_entry_t : __cxa_base_descriptor_entry_t {
        uint16_t size;           // Size of data in bytes
        const char* data_type;   // Type information
        const char* name;        // Field name
    };

    struct __cxa_vendor_extended_descriptor_entry_t : __cxa_base_descriptor_entry_t {
        // Vendor-specific content (vendor identified by __cxa_descriptor_table_t::vendor_id)
    };

    } // namespace __cxxabiv1

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

.. code-block:: cpp

    namespace __cxxabiv1 {

    enum class contract_violation_field_t : unsigned char {
        // Core fields
        source_location      = 0x01,
        source_text          = 0x02,
        assertion_kind       = 0x03,
        evaluation_semantic  = 0x04,
        detection_mode       = 0x05,

        // Reserved for future standard fields: 0x06 - 0x3F

        // Vendor-specific fields start at 0x40
        vendor_specific_base = 0x40,
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
   :widths: 30 30 40

   * - Field
     - Output Type
     - Description
   * - ``source_location``
     - ``const __cxa_source_location*``
     - Pointer to inline source location data
   * - ``source_text``
     - ``const char**``
     - Pointer to pointer to source text string
   * - ``assertion_kind``
     - ``__cxa_assertion_kind_t*``
     - Pointer to assertion kind value
   * - ``evaluation_semantic``
     - ``__cxa_evaluation_semantic_t*``
     - Pointer to evaluation semantic value
   * - ``detection_mode``
     - ``__cxa_detection_mode_t*``
     - Pointer to detection mode value

6.5 Usage Example
------------------

.. code-block:: cpp

    // Inside the runtime entrypoint function
    void __cxa_contract_violation_entrypoint_pf_se(
        __cxa_descriptor_t *static_descriptor,
        void *static_data)
    {
        // Construct contract violation data structure from entrypoint parameters
        __cxxabiv1::__cxa_contract_violation_data_t cv_data = {
            .static_descriptor = static_descriptor,
            .static_data = static_data,
            .mode = __cxxabiv1::__cxa_detection_mode_t::predicate_false,
            .semantic = __cxxabiv1::__cxa_evaluation_semantic_t::enforced,
            .dynamic_data = nullptr,
            .reserved = nullptr
        };

        // Access specific fields using the accessor API
        const __cxa_source_location* source_loc = nullptr;
        if (__cxxabiv1::__cxa_get_contract_violation_field(
                &cv_data,
                __cxxabiv1::contract_violation_field_t::source_location,
                &source_loc)) {
            // Use source_loc for diagnostics
            printf("Contract violation at %s:%u:%u in %s\n",
                   source_loc->file_name,
                   source_loc->line,
                   source_loc->column,
                   source_loc->function_name);
        }

        const char* text = nullptr;
        if (__cxxabiv1::__cxa_get_contract_violation_field(
                &cv_data,
                __cxxabiv1::contract_violation_field_t::source_text,
                &text)) {
            // Use text for diagnostics
            printf("Failed contract: %s\n", text);
        }

        __cxxabiv1::__cxa_assertion_kind_t kind;
        if (__cxxabiv1::__cxa_get_contract_violation_field(
                &cv_data,
                __cxxabiv1::contract_violation_field_t::assertion_kind,
                &kind)) {
            // Use assertion kind
        }

        // Construct std::contract_violation from cv_data
        // and invoke user's violation handler
        std::contract_violation violation =
            construct_standard_violation(cv_data);
        invoke_violation_handler(violation);

        // For enforced semantics, terminate
        std::terminate();
    }

7. Implementation Considerations
=================================

7.1 Future Extensibility
-------------------------

Future extensions may include:

* New enumerator values for ``assertion_kind``,
  ``evaluation_semantic``, or ``detection_mode``
* Custom contract labels and identifiers
* Per-contract violation handlers
* Custom diagnostic messages
* Contract grouping and filtering metadata
* Exception handling mode indicators (``-fno-exceptions``)

The design accommodates these extensions through:

* Reserved ranges in all enumerations
* Version field in descriptor tables
* Extended and vendor-specific descriptor entry types
* Reserved parameter in the generic entrypoint function
* Extensible field enumeration in the accessor API

----

.. raw:: html

   <p>
   <font size=-1>
   <i>
   Document version: 1.0<br>
   Last modified: 2025-10-06
   </i>
   </font>
   </p>
