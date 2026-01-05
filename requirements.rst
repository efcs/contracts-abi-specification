.. _requirements:

============================================
C++ Contracts ABI: Design Requirements
============================================

.. contents::
   :local:
   :depth: 2

Purpose
*******

This document establishes the requirements for the Itanium C++ ABI specification for contracts, a feature arriving in C++26. These requirements will guide the evaluation and selection of design approaches.

The goal is straightforward: define a stable, extensible interface between compilers and runtime libraries for handling contract violations. When a contract fails, the compiler must arrange for the user's violation handler to be called with a ``std::contract_violation`` object containing information about what went wrong.

Background
**********

C++26 introduces contracts—preconditions, postconditions, and assertions that express program invariants directly in code. When a contract fails at runtime, the program needs to construct a ``std::contract_violation`` object and invoke the user's registered handler.

The challenge lies in the details. Compilers (GCC, Clang) and standard libraries (libstdc++, libc++) are developed independently. The ABI must work across all combinations of these tools, both today and as they evolve through future C++ standards. It must accommodate future extensions we can't yet anticipate. And it must do all this efficiently, without bloating binaries or slowing down optimized code.

Two main approaches are under consideration:

**Descriptor Table Approach**
  The compiler emits metadata tables describing contract data layout. At runtime, the library parses these descriptors to construct the violation object.

**Direct Construction Approach**
  The compiler generates code that directly builds the violation object on the stack, then calls the handler. No runtime parsing, no metadata tables.

This document captures what any viable approach must achieve.

Critical Requirements
*********************

ABI Stability
=============

The interface between compiler and runtime must remain stable as tools evolve. Specifically:

Entrypoint stability
  Function signatures that compilers call cannot change in incompatible ways. If an entrypoint exists, its calling convention must remain fixed.

Data structure stability
  Any data structures passed across the ABI boundary must support evolution. Existing fields must remain at stable offsets. New fields can only be added in ways that don't break existing code.

Standard type evolution
  The ``std::contract_violation`` type may gain new fields in future C++ standards (C++29, C++32...). The ABI must accommodate this growth without requiring recompilation of existing code or breaking old binaries.

The key constraint: once released, the ABI cannot break existing object files.

*Validation:* See test_abi_forward_compatibility and test_abi_backward_compatibility for validation scenarios.

Cross-Compiler Interoperability
================================

Object files compiled by any conforming compiler must work with any conforming standard library, provided both support the same C++ standard version.

For example:
  - Clang-compiled code using C++26 contracts must work with any libstdc++ that supports C++26 contracts
  - GCC-compiled code using C++26 contracts must work with any libc++ that supports C++26 contracts
  - Mixed compilation (different parts compiled by different compilers) must work

This requirement rules out solutions that tightly couple a specific compiler version to a specific library version.

*Validation:* See test_cross_compiler_interop for validation scenarios.

Forward Compatibility
=====================

New object files with old runtimes
  When a newer compiler emits object files with additional contract data fields, older runtime libraries must handle them gracefully. The old runtime won't understand the new fields—and that's fine. It should use the fields it recognizes and silently ignore what it doesn't understand.

Old object files with new runtimes
  New runtime libraries must correctly process contract violations from older object files, even when expected fields are missing. Missing fields should map to appropriate default values.

This allows gradual toolchain updates without requiring everything to move in lockstep.

Backward Compatibility
======================

New compiler with old runtime
  A C++26 compiler should be able to emit contract violations that work with standard library implementations that only support basic C++26 contracts, even if the compiler itself supports more advanced features from later standards.

Version skew tolerance
  Projects often use different compiler versions across components, or link against system libraries from earlier releases. The ABI must tolerate reasonable version differences.

Constructor Isolation
=====================

When a contract violation occurs, the program is in a potentially invalid state. Running arbitrary user code between detecting the violation and invoking the handler creates safety risks.

Must not invoke
  User-provided constructors for types in user code must not be called during contract violation processing.

May need to invoke
  Constructors for standard library types (``std::string_view``, ``std::source_location``, potentially ``std::exception_ptr``) may need to be invoked. Future C++ standards might add fields to ``std::contract_violation`` that require non-trivial construction.

The distinction matters because compilers don't see standard library headers at contract sites—they compile contracts without any ``#include`` directives. This makes it difficult for compilers to know how to construct standard library types, especially given implementation details like inline namespaces (``std::__libcpp::exception_ptr`` vs ``std::exception_ptr``).

An approach that requires the compiler to construct objects with standard library types faces challenges here. An approach that delegates construction to the runtime library avoids this problem.

*Validation:* See test_constructor_isolation for validation scenarios.

Efficient Field Omission
========================

Users must be able to omit certain contract data at compile time to save space:

- Source file locations (when stripped builds are required)
- Source text strings (can be large, especially for complex predicates)
- Vendor-specific diagnostic fields

True zero overhead
  When a field is omitted, it should cost nothing—no storage in object files, no initialization code generated, no runtime overhead.

No version explosion
  Supporting omission should not require creating exponentially many versions. If there are N potentially omittable fields, the design should not require 2^N variants to avoid wasted space.

This requirement is driven by large-scale C++ projects that already push linker limits. Adding contracts should not force these projects to bloat their binaries.

*Validation:* See test_efficient_field_omission for validation scenarios.

Minimal Code Generation Impact
===============================

Contracts should not make programs slower or harder to optimize.

Cold path isolation
  Contract failure code is a cold path—it rarely executes. This code should not inhibit optimization of the hot path. Specifically:

  - It shouldn't cause inlining budget exhaustion
  - It shouldn't pollute instruction caches
  - It shouldn't prevent tail calls or other optimizations

Compact per-contract code
  The code generated at each contract site should be minimal. For projects with thousands of contracts, per-contract overhead multiplies quickly.

Small object file growth
  The total size added to object files should scale reasonably. This includes both code size and data size (.rodata sections for strings, metadata, etc.).

The constraint: large projects with giant object files at linker limits should be able to adopt contracts without hitting new limits.

*Validation:* See test_minimal_code_generation for validation scenarios.

Important Requirements
**********************

Vendor Extensibility
====================

Compiler vendors need to add proprietary extensions:

- Additional diagnostic messages
- Stack traces or call context
- Performance counters
- Integration with sanitizers or debugging tools

Independent extension
  Vendors should be able to add extensions without requiring tight coordination with other vendors or waiting for ABI committee approval for every addition.

Cross-vendor visibility (optional)
  If two vendors independently implement the same extension (e.g., stack traces), it would be useful if they could interoperate. But this is a nice-to-have, not a must-have.

Future standard features
  When the C++ standard adds new contract features, vendors need to be able to implement them without breaking existing deployed code.

*Validation:* See test_vendor_extensions_independent for validation scenarios.

Small Specification Surface
============================

A smaller specification is easier to agree upon, implement correctly, and maintain over time. Minimize the number of:

- Required function signatures
- Data structure layouts
- Enumeration values
- Coordination points between vendors

Each piece of standardized interface is a commitment that must remain stable forever.

*Validation:* See test_small_specification for validation scenarios.

Minimal Governance Overhead
============================

Avoid requiring centralized registries or frequent coordination for routine extensions:

- Vendor IDs
- Field type enumerations
- Version number allocation

Some governance is acceptable when it provides clear benefits, but minimize the need for vendors to coordinate on day-to-day development.

Deployment Flexibility
======================

Different deployment scenarios have different constraints:

Platform deployment
  Some platforms update compilers and high-level standard libraries (libc++, libstdc++) more frequently than low-level ABI libraries (libc++abi, libsupc++). It would be valuable to support contracts by updating only the compiler and high-level library, without requiring changes to the low-level ABI library.

  However, this requirement needs validation. Who actually updates their standard library without updating their ABI library? Apple? Embedded systems? Linux distributions? The answer affects whether this requirement is critical or merely nice-to-have.

Long-lived deployments
  Enterprise systems may run for years with stable OS libraries. Contracts support should work with existing runtime libraries when possible.

*Validation:* See test_deployment_flexibility for validation scenarios.

Non-Goals
*********

User-Provided Constructors
===========================

The design explicitly does not support calling user-provided constructors during contract violation processing. This means:

Not supported
  Custom types with user-defined constructors cannot be fields in the violation object that the compiler constructs.

Rationale
  The program is already in an invalid state when a contract fails. Running arbitrary user code (constructors, operators, conversions) between violation detection and handler invocation creates safety and reentrancy hazards.

Clarification
  This restriction applies to user code, not standard library code. The runtime library may need to construct standard library types as part of building the ``std::contract_violation`` object. The distinction is that the standard library is part of the contracts implementation itself, not arbitrary user code running in an invalid state.

*Validation:* See test_reject_user_constructors for validation scenarios.

Design Questions
****************

These questions affect requirement priorities and should be resolved:

Optimization authority
  Should the specification mandate specific optimization strategies (for size, compile time, runtime), or leave these choices to vendors?

  Baking in optimization decisions ensures consistency across implementations but constrains vendor innovation. Leaving optimization to vendors allows flexibility but may lead to behavioral differences.

Platform deployment constraint
  How important is it to support contracts without updating low-level ABI libraries? This affects whether the entrypoint must live in libc++/libstdc++ (higher-level) versus libc++abi/libsupc++ (lower-level).

  The answer depends on real-world deployment patterns, which may vary across platforms.

Evaluation Criteria
*******************

Proposed approaches should be evaluated against these criteria:

Compatibility
  - Does it support all required interoperability scenarios?
  - How does it handle version skew?
  - Can old and new code coexist?

Extensibility
  - How easily can new fields be added?
  - What coordination is required for vendor extensions?
  - Does it accommodate unknown future requirements?

Efficiency
  - What is the per-contract code size?
  - How efficient is field omission?
  - What runtime overhead does it impose?

Specification complexity
  - How much must be standardized?
  - How many coordination points exist?
  - How easy is it to implement correctly?

Standard library flexibility
  - Does it lock down ``std::contract_violation`` layout?
  - Can standard libraries use different implementations?
  - How does it handle STL type construction?

Trade-Offs
**********

No design will optimize all requirements simultaneously. Some trade-offs are fundamental:

Simplicity vs. Flexibility
  Simpler specifications (e.g., mandating a fixed memory layout) are easier to understand but constrain future evolution. More flexible specifications (e.g., descriptor-based indirection) support richer evolution but add complexity.

Compiler work vs. Runtime work
  Work must happen somewhere. Either the compiler generates more code (construction thunks, data structures), or the runtime does more work (parsing, interpretation). The question is which approach scales better.

Code size vs. Runtime cost
  Smaller per-contract code may require more runtime interpretation. Larger per-contract code may reduce runtime overhead. The optimal balance depends on the ratio of contracts to executions.

Vendor independence vs. Coordination
  Allowing vendors to act independently simplifies their development process but may lead to incompatibilities. Requiring coordination ensures compatibility but slows evolution.

Understanding these trade-offs helps evaluate whether a proposed approach makes the right compromises.

Requirements Validation
************************

Concrete test scenarios for validating that an ABI implementation satisfies these requirements are documented in the *Test Cases* document. These tests are implementation-agnostic and should work regardless of which approach (descriptor table, runtime-constructed, or other) is adopted.
