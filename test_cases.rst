.. _test_cases:

============================================
Requirements Validation Test Cases
============================================

.. contents::
   :local:
   :depth: 2

This document provides concrete test scenarios for validating that an ABI implementation satisfies the requirements. These tests are implementation-agnostic and should work regardless of which approach (descriptor table, runtime-constructed, or other) is adopted.

Critical Requirements Tests
============================

.. _test_abi_forward_compatibility:

Test: ABI Stability - Forward Compatibility
--------------------------------------------

**Scenario**: A compiler emits contract violations with fields that the runtime library doesn't recognize.

**Setup**:
  - Compiler: Implements contract ABI version N+1 with new field X
  - Runtime: Implements contract ABI version N without knowledge of field X
  - Compilation: Object file contains contracts with field X populated

**Expected behavior**:
  - Runtime successfully constructs ``std::contract_violation`` object
  - Runtime uses fields it recognizes from version N
  - Runtime ignores field X without error
  - Contract handler receives valid violation object with recognized fields

**Success criteria**:
  - No runtime errors or crashes
  - Handler receives contract information for all version-N fields
  - Missing field X does not prevent handler invocation

.. _test_abi_backward_compatibility:

Test: ABI Stability - Backward Compatibility
---------------------------------------------

**Scenario**: A runtime library expects fields that older object files don't provide.

**Setup**:
  - Compiler: Implements contract ABI version N
  - Runtime: Implements contract ABI version N+1, expects new field Y
  - Compilation: Object file contains contracts without field Y

**Expected behavior**:
  - Runtime detects missing field Y
  - Runtime uses appropriate default/null value for field Y
  - Runtime constructs valid ``std::contract_violation`` object
  - Contract handler receives violation object with defaults for missing fields

**Success criteria**:
  - No runtime errors or crashes
  - Handler receives contract information for all available fields
  - Missing field Y has sensible default (empty string, null pointer, etc.)

.. _test_cross_compiler_interop:

Test: Cross-Compiler Interoperability
--------------------------------------

**Scenario**: Object files compiled by different compilers are linked together and execute contract violations.

**Setup**:
  - C++ module or translation unit A: Compiled by compiler X with its contract implementation
  - C++ module or translation unit B: Compiled by compiler Y with its contract implementation
  - Linked: Both C++ modules/translation units linked into single executable
  - Runtime: Single shared standard library

**Expected behavior**:
  - Contract violations in C++ module/translation unit A construct proper ``std::contract_violation`` objects
  - Contract violations in C++ module/translation unit B construct proper ``std::contract_violation`` objects
  - Both C++ modules/translation units call the same handler with valid violation objects
  - Handler receives equivalent information regardless of which compiler produced the violation

**Success criteria**:
  - Both compilers emit compatible calling conventions
  - Standard library correctly interprets data from both compilers
  - No linker errors due to symbol conflicts
  - Handler sees consistent field values regardless of source compiler

.. _test_efficient_field_omission:

Test: Efficient Field Omission
-------------------------------

**Scenario**: User compiles with flags that omit specific contract data fields.

**Setup**:
  - Compilation flag: Disable source location information
  - Code: 1000 contracts throughout codebase
  - Measurement: Compare object file sizes and runtime behavior

**Expected behavior**:
  - Object files contain no storage for omitted source location data
  - No initialization code generated for omitted fields
  - Contract violations still work correctly with remaining fields
  - Handler receives null/empty values for omitted fields

**Success criteria**:
  - Object file size reduction proportional to omitted data
  - Zero overhead for omitted fields (no vestigial code or data)
  - No version explosion (single implementation handles omission)
  - Runtime correctly identifies omitted fields vs. present-but-empty fields

.. _test_constructor_isolation:

Test: Constructor Isolation
----------------------------

**Scenario**: Contract violation occurs with fields requiring standard library type construction.

**Setup**:
  - Contract: Uses ``std::source_location`` or ``std::string_view`` in violation data
  - User code: No contract-related includes (compiler-only syntax)
  - Violation: Trigger contract failure at runtime

**Expected behavior**:
  - No user-provided constructors are invoked
  - Standard library constructors for ``std::source_location``, ``std::string_view`` may be invoked by runtime
  - Violation object constructed successfully
  - Handler receives properly initialized violation object

**Success criteria**:
  - No user code executed between violation detection and handler call
  - Standard library types properly constructed with correct values
  - Compiler doesn't need to ``#include`` standard library headers at contract site
  - Implementation handles inline namespaces correctly

.. _test_minimal_code_generation:

Test: Minimal Code Generation Impact
-------------------------------------

**Scenario**: Large codebase adopts contracts and measures compilation impact.

**Setup**:
  - Baseline: Existing codebase at linker size limits
  - Change: Add contracts (preconditions, postconditions, assertions)
  - Measurement: Object file size, compilation time, runtime performance

**Expected behavior**:
  - Per-contract code generation is minimal (ideally single function call)
  - Contract failure code doesn't inhibit inlining of hot path
  - Cold path (contract failure) isolated from hot path (normal execution)
  - Object file growth scales reasonably with number of contracts

**Success criteria**:
  - Per-contract overhead < 50 bytes in .text section
  - No inlining budget exhaustion in hot functions
  - Contract failure code in separate section or marked cold
  - Total object file growth remains manageable for projects at limits

.. _test_vendor_extensions_independent:

Test: Vendor Extensions Without Coordination
---------------------------------------------

**Scenario**: Two compiler vendors independently add proprietary extensions.

**Setup**:
  - Vendor A: Adds stack trace field to contract violations
  - Vendor B: Adds performance counter field to contract violations
  - No coordination: Vendors develop features independently
  - Deployment: Code compiled by vendor A runs with vendor B's runtime (and vice versa)

**Expected behavior**:
  - Vendor A's extension works with vendor A's runtime
  - Vendor B's extension works with vendor B's runtime
  - Vendor A's object files work with vendor B's runtime (extension ignored)
  - Vendor B's object files work with vendor A's runtime (extension ignored)

**Success criteria**:
  - No namespace conflicts between vendor extensions
  - No coordination required (no central registry for field types)
  - Each vendor's extension accessible when both compiler and runtime from same vendor
  - Cross-vendor compatibility maintained (unrecognized extensions silently ignored)

Important Requirements Tests
=============================

.. _test_small_specification:

Test: Small Specification Surface
----------------------------------

**Validation approach**: Analyze the specification itself.

**Evaluation criteria**:
  - Count required entrypoint signatures
  - Count mandated data structure layouts
  - Count enumeration values that must be standardized
  - Count coordination points between vendors

**Success criteria**:
  - Single primary entrypoint function
  - Minimal required data structure formats
  - Small set of standardized enumeration values
  - Vendor extensions possible without central coordination

.. _test_deployment_flexibility:

Test: Deployment Flexibility
-----------------------------

**Scenario**: Platform updates high-level standard library without updating low-level ABI library.

**Setup**:
  - Compiler: Updated to support C++26 contracts
  - High-level library: Updated (libc++/libstdc++)
  - Low-level ABI library: Not updated (libc++abi/libsupc++)
  - Deployment: System has old ABI library, new compiler and high-level library

**Expected behavior**:
  - Contracts work if implementation can be provided by high-level library alone
  - Graceful failure or degraded functionality if low-level library required

**Success criteria**:
  - Implementation strategy clearly documents which library must provide entrypoint
  - If high-level library sufficient, contracts work without ABI library update
  - If low-level library required, specification documents this dependency

**Note**: This test's importance depends on real-world deployment patterns, which require validation.

Negative Tests
==============

.. _test_reject_user_constructors:

Test: Reject User Constructors
-------------------------------

**Scenario**: Attempt to use contract violation with fields requiring user-provided constructors.

**Setup**:
  - Code attempts to include user-defined type in violation data
  - User type has non-trivial constructor

**Expected behavior**:
  - Implementation refuses this configuration
  - Compiler error or runtime constraint prevents user constructor invocation

**Success criteria**:
  - No arbitrary user code executed during violation processing
  - Clear distinction between standard library constructors (allowed) and user constructors (forbidden)

.. _test_incompatible_versions:

Test: Incompatible Standard Versions
-------------------------------------

**Scenario**: Attempt to link C++26 contracts code with pre-C++26 runtime.

**Setup**:
  - Compiler: C++26 with contracts enabled
  - Runtime: Pre-C++26 standard library without contract support

**Expected behavior**:
  - Link-time error due to missing entrypoint symbol
  - Clear error message indicating version mismatch

**Success criteria**:
  - Failure occurs at link time, not runtime
  - Error message clearly indicates missing contract support
  - No silent undefined behavior

Version Evolution Tests
=======================

.. _test_std_contract_violation_evolution:

Test: std::contract_violation Evolution
----------------------------------------

**Scenario**: The C++ standard adds new fields to ``std::contract_violation`` in future standards.

**Setup**:
  - C++26: ``std::contract_violation`` has fields A, B, C
  - C++29: Standard adds field D to ``std::contract_violation``
  - Mixed deployment: C++26 compiler with C++29 runtime, or vice versa

**Expected behavior**:
  - C++26 code works with C++29 runtime (field D gets default value)
  - C++29 code works with C++26 runtime (field D ignored by old runtime)
  - ABI remains compatible across standard versions

**Success criteria**:
  - No recompilation required when standard library updates
  - Prefix compatibility maintained (existing fields at stable offsets)
  - New fields addable without breaking old code

.. _test_multiple_standards:

Test: Multiple Standards in Single Binary
------------------------------------------

**Scenario**: Single executable contains object files compiled for different C++ standard versions.

**Setup**:
  - C++ module or translation unit A: Compiled as C++26 with basic contract fields
  - C++ module or translation unit B: Compiled as C++29 with extended contract fields
  - C++ module or translation unit C: Compiled as C++32 with additional vendor extensions
  - Linked: All C++ modules/translation units in single executable with single runtime

**Expected behavior**:
  - All contract violations work correctly
  - Handler receives appropriate fields for each C++ module/translation unit's standard version
  - No conflicts between different versions' implementations

**Success criteria**:
  - Runtime dynamically handles different field sets
  - No version explosion in runtime library
  - Clear semantics for missing fields across versions
