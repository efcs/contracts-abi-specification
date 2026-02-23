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
  Function signatures that compilers call cannot change in incompatible
  ways.

Data structure stability
  Data structures passed across the ABI boundary must support evolution.
  Existing fields must remain at stable offsets.

Standard type evolution
  ``std::contracts::contract_violation`` may gain new fields in future C++
  standards. The ABI must accommodate this without recompilation of
  existing code.

Cross-Compiler Interoperability
================================

Object files compiled by any conforming compiler must work with any
conforming standard library, provided both target the same Itanium ABI
version.

- Clang-compiled code must work with libstdc++
- GCC-compiled code must work with libc++
- Mixed compilation (different compilers for different TUs) must work

This rules out solutions that couple a specific compiler version to a
specific library version.

Forward Compatibility
=====================

New object files with old runtimes
  Older runtimes must handle newer object files gracefully, using the
  fields they recognize and ignoring the rest.

Old object files with new runtimes
  Newer runtimes must process older object files correctly, supplying
  defaults for missing fields.

Backward Compatibility
======================

New compiler with old runtime
  A compiler targeting a newer C++ standard should emit contract data
  that older runtimes can still process.

Version skew tolerance
  The ABI must tolerate reasonable version differences across components
  in a linked program.

Constructor Isolation
=====================

The compiler does not know which standard library implementation it is
targeting---it only knows whether it targets the Itanium ABI. The
``std::contracts::contract_violation`` constructor is defined by the
standard library (libc++, libstdc++), and different implementations have
different layouts, constructor signatures, and inline namespace schemes.

The ABI must not require the compiler to construct standard library types
at contract sites. Construction of ``std::contracts::contract_violation``
is delegated to the runtime library.

Efficient Field Omission
========================

Users must be able to omit contract data at compile time:

- Source locations (stripped builds)
- Source text (can be large)
- Vendor-specific fields

True zero overhead
  Omitted fields cost nothing---no storage, no initialization, no
  runtime overhead.

No version explosion
  N omittable fields must not require 2^N entrypoint variants.

Minimal Code Generation Impact
===============================

Cold path isolation
  Contract failure is a cold path. It must not inhibit optimization of
  the hot path (inlining budgets, icache, tail calls).

Compact per-contract code
  Code generated at each contract site must be minimal. Per-site
  overhead multiplies across thousands of contracts.

Small object file growth
  Total size added (code + .rodata) must scale reasonably. Large
  projects at linker limits must be able to adopt contracts.

Important Requirements
**********************

Vendor Extensibility
====================

Vendors must be able to add extensions (diagnostics, stack traces,
sanitizer integration) without coordinating with other vendors or
waiting for ABI committee approval.

When the C++ standard adds new contract features, vendors must be able to
implement them without breaking existing deployed code.

Small Specification Surface
============================

Minimize the number of required function signatures, data structure
layouts, enumeration values, and coordination points. Each standardized
interface element is a permanent commitment.

Minimal Governance Overhead
============================

Avoid centralized registries or frequent coordination for routine
extensions (vendor IDs, field type enumerations, version numbers).

Non-Goals
*********

User-Provided Constructors
===========================

The compiler must not call user-provided constructors during contract
violation processing. The runtime library may construct standard library
types, but no user code runs between violation detection and handler
invocation.
