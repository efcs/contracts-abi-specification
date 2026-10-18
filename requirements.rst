.. _requirements:

============================================
C++ Contracts ABI: Design Requirements
============================================

.. contents::
   :local:
   :depth: 2

Purpose
*******

Requirements for the Itanium C++ ABI specification for C++26 contracts.
The ABI defines the interface between compilers and runtime libraries for
handling contract violations.

Critical Requirements
*********************

ABI Stability
=============

Once released, the ABI cannot break existing object files.

Entrypoint stability
  Entry point signatures (single ``void*`` argument) cannot change.

Data structure stability
  The ``__cxa_contract_data_block`` header (``descriptor*``, ``next*``)
  must remain at fixed offsets.  Existing field IDs and enum values must
  retain their meaning.

Standard type evolution
  ``std::contracts::contract_violation`` may gain new fields in future C++
  standards.  The ABI must accommodate this without recompilation of
  existing code (achieved via new field IDs in the data chain).

Cross-Compiler Interoperability
================================

Object files compiled by any conforming compiler must work with any
conforming standard library, provided both target the same Itanium ABI
version.

- Clang-compiled code must work with libstdc++
- GCC-compiled code must work with libc++
- Mixed compilation (different compilers for different TUs) must work

This is **demonstrated**: both GCC and Clang ship byte-compatible ABI
headers and have been tested in all six cross-compilation permutations
(pure-gcc, pure-clang, gcc-link-clang-g, gcc-link-clang-f,
clang-link-gcc-g, clang-link-gcc-f).

Forward Compatibility
=====================

New object files with old runtimes
  Older runtimes ignore field IDs they do not recognize when walking the
  data block chain.  New specialized entry points cause link errors against
  old runtimes; the compiler falls back to the universal entry point.

Old object files with new runtimes
  Newer runtimes supply default values for missing fields via
  ``__cxa_find_field_value``'s default parameter.

Backward Compatibility
======================

New compiler with old runtime
  The compiler can target the universal entry point (always available)
  and emit only field IDs the old runtime recognizes.

Version skew tolerance
  The data block chain's self-describing format tolerates reasonable
  version differences: each block carries its own descriptor table.
  Blocks from different compiler versions can coexist in the same chain.

Constructor Isolation
=====================

The compiler does not know which standard library implementation it is
targeting---it only knows whether it targets the Itanium ABI.

The ABI eliminates the construction problem entirely:
``std::contracts::contract_violation`` is a thin wrapper around the
chain pointer (``const __cxa_contract_data_block*``).  Setting a single
pointer field is all that is needed.  No constructor signatures, no
layout negotiation, no inline namespace differences.

Efficient Field Omission
========================

Users must be able to omit contract data at compile time:

- Source locations (stripped builds)
- Source text (can be large)
- Message text (P3099)
- Vendor-specific fields

True zero overhead
  Omitted fields cost nothing---the descriptor table simply does not
  include them.  No storage, no initialization, no runtime overhead.

No version explosion
  The 34 entry points are the product of 4 kinds x 2 semantics x 2
  modes x 2 noexcept = 32 specialized plus 2 universal.  This is a
  fixed, small set---not 2\ :sup:`N` of omittable fields.  Entry point
  selection is orthogonal to field omission.

Minimal Code Generation Impact
===============================

Cold path isolation
  Contract failure is a cold path.  It must not inhibit optimization of
  the hot path (inlining budgets, icache, tail calls).

Compact per-contract code
  Code generated at each contract site must be minimal.  Per-site
  overhead multiplies across thousands of contracts.  Each call site is
  ``lea`` + ``call`` with one pointer argument (17 bytes on x86-64).

Small object file growth
  Total size added (code + .rodata) must scale reasonably.  No per-TU
  wrapper functions are emitted.

P3400, P3290, and P3099 Support
================================

The ABI must support:

P3400 assertion-control objects
  Per-assertion local handler dispatch and query functions through
  type-erased trampolines stored in the data block (fields ``0x04``,
  ``0x05``, ``0x06``).

P3290 manual violation API
  ``handle_enforced_contract_violation`` and
  ``handle_observed_contract_violation`` must be implementable using
  the same data block format and entry points.  Stack-constructed
  blocks use ``CXA_AK_MANUAL`` assertion kind and the universal entry
  point.

P3099 message field
  A computed diagnostic string (field ``0x03``) distinct from the
  predicate source text (field ``0x02``), independently omittable.

Important Requirements
**********************

Vendor Extensibility
====================

Vendors must be able to add extensions (diagnostics, stack traces,
sanitizer integration) without coordinating with other vendors or
waiting for ABI committee approval.

Extended field IDs (>= ``0x40``) use pointer-based data and do not
require offset calculations, making them suitable for dynamically
allocated vendor extensions.

When the C++ standard adds new contract features, vendors must be able to
implement them without breaking existing deployed code (achieved via
new field IDs and new entry points).

Small Specification Surface
============================

Minimize the number of required function signatures, data structure
layouts, enumeration values, and coordination points.  Each standardized
interface element is a permanent commitment.

Minimal Governance Overhead
============================

Avoid centralized registries or frequent coordination for routine
extensions (vendor IDs, field ID enumerations).

Non-Goals
*********

User-Provided Constructors
===========================

The compiler must not call user-provided constructors during contract
violation processing.  The ``contract_violation`` object is constructed
by setting a single pointer field; no user code runs between violation
detection and handler invocation.
