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

3. Data Types and Representations
==================================

3.1 Itanium Representations
----------------------------

The C++ standard library types used in contract violations have corresponding
"Itanium representations" used when passing data to the entrypoint function:

.. list-table::
   :header-rows: 1
   :widths: 30 35 35

   * - Standard Type
     - Itanium Representation
     - Underlying Type
   * - ``std::source_location``
     - ``source_location_ptr_t``
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

The ``std::source_location`` type is used to represent information about
the source code location of a contract annotation. To ensure ABI compatibility
across compilers and standard libraries, this section specifies the layout of
the underlying data structure that ``std::source_location`` references.

3.2.1 ``std::source_location`` Representation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An object of type ``std::source_location`` shall contain a single
data member: a pointer to an object of the layout specified in
§3.2.2. The pointer has type
``const __cxa_source_location*`` as defined below.

The size and alignment of ``std::source_location`` are those of a
pointer type as defined by the base platform ABI.

3.2.2 Source Location Data Layout
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The data referenced by a ``std::source_location`` object has the
following layout, which must be identical across all implementations to ensure
that contract violation data can be correctly interpreted by any conforming
runtime library:

.. code-block:: cpp

    struct __cxa_source_location {
        const char* file_name;
        const char* function_name;
        unsigned line;
        unsigned column;
    };

The fields have the following semantics:

``file_name``
    A pointer to a null-terminated string containing the presumed name of the
    source file. The string has static storage duration. If the file name is
    unavailable, this pointer shall be null.

``function_name``
    A pointer to a null-terminated string containing the name of the function.
    The string has static storage duration. The exact format of the function
    name is implementation-defined, but should be suitable for diagnostic
    purposes. If the function name is unavailable, this pointer shall be null.

``line``
    The presumed line number in the source file, represented as an
    ``unsigned int``. If the line number is unavailable, this value
    shall be zero.

``column``
    The presumed column number in the source file, represented as an
    ``unsigned int``. If the column number is unavailable, this value
    shall be zero.

3.2.3 Layout Requirements
~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``__cxa_source_location`` structure has the following layout properties:

* The structure has standard layout (C++11 [class.prop]).
* The offset of each member is determined by the base platform ABI's
  layout rules for C structures.
* The alignment of the structure is the maximum of the alignments of its
  members, as specified by the base platform ABI.
* On typical 64-bit platforms:

  - ``sizeof(__cxa_source_location)`` is 24 bytes
  - ``alignof(__cxa_source_location)`` is 8 bytes
  - Offset of ``file_name``: 0
  - Offset of ``function_name``: 8
  - Offset of ``line``: 16
  - Offset of ``column``: 20

* On typical 32-bit platforms:

  - ``sizeof(__cxa_source_location)`` is 16 bytes
  - ``alignof(__cxa_source_location)`` is 4 bytes
  - Offset of ``file_name``: 0
  - Offset of ``function_name``: 4
  - Offset of ``line``: 8
  - Offset of ``column``: 12

3.2.4 Static Data Storage
~~~~~~~~~~~~~~~~~~~~~~~~~~

Instances of ``__cxa_source_location`` generated by the compiler for contract
annotations have static storage duration and are typically placed in read-only
data sections. Multiple ``std::source_location`` objects representing
the same source location may share a single ``__cxa_source_location`` instance.

Compilers may emit ``__cxa_source_location`` objects with vague linkage (see
`§5.2 <abi.html#vague>`_) when the source location appears in
templates or inline functions, ensuring that only one definition is retained
by the linker.

3.2.5 Null Representation
~~~~~~~~~~~~~~~~~~~~~~~~~~

A default-constructed ``std::source_location`` object may contain
either:

* A null pointer, or
* A pointer to a ``__cxa_source_location`` object where ``file_name``
  and ``function_name`` are null, and ``line`` and
  ``column`` are zero.

Runtime libraries must handle both representations equivalently when processing
contract violations.

3.3 Enumerator Values
----------------------

3.3.1 ``__cxa_assertion_kind_t``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - Enumerator
     - Value
   * - Not specified
     - ``0x00``
   * - ``std::assertion_kind::pre``
     - ``0x01``
   * - ``std::assertion_kind::post``
     - ``0x02``
   * - ``std::assertion_kind::contract_assert``
     - ``0x03``

3.3.2 ``__cxa_evaluation_semantic_t``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - Enumerator
     - Value
   * - Not specified
     - ``0x00``
   * - ``std::evaluation_semantic::enforced``
     - ``0x01``
   * - ``std::evaluation_semantic::observed``
     - ``0x02``

3.3.3 ``__cxa_detection_mode_t``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - Enumerator
     - Value
   * - Not specified
     - ``0x00``
   * - ``std::detection_mode::predicate_false``
     - ``0x01``
   * - ``std::detection_mode::evaluation_exception``
     - ``0x02``

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
        source_location_ptr    = 0x11,
        source_location_inline = 0x12,
        source_text            = 0x13,
        assertion_kind         = 0x14,

        // Reserved for future standard fields: 0x15 - 0x2F

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
   * - ``__cxa_source_location*``
     - ``0``
     - ``sizeof(void*)``
   * - ``const char*`` (source text)
     - ``sizeof(void*)``
     - ``sizeof(void*)``
   * - ``__cxa_assertion_kind_t``
     - ``sizeof(void*) * 2``
     - ``sizeof(unsigned char)``

Implementations may omit fields by providing null pointers or by using
explicit descriptor entries.

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
     - ``const __cxa_source_location**``
     - Pointer to pointer to source location data
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
