======================================
Code Size Comparison: Contracts vs Assert
======================================

.. contents::
   :local:
   :depth: 2

Overview
========

This document compares the code size impact of C++26 Contracts using this ABI
specification versus the traditional ``assert()`` macro.

Call Site Comparison
====================

Classic assert()
----------------

The ``assert(x > 0)`` macro typically expands to:

.. code-block:: cpp

    if (!(x > 0)) {
        __assert_fail("x > 0", __FILE__, __LINE__, __func__);
    }

Generated assembly (x86-64):

.. code-block:: asm

    ; Condition check
    cmp     edi, 0                        ; 3 bytes
    jg      .L_ok                         ; 2 bytes

    ; Violation path (4 arguments)
    lea     rdi, [rip + .L_expr]          ; 7 bytes - "x > 0"
    lea     rsi, [rip + .L_file]          ; 7 bytes - "file.cpp"
    mov     edx, 42                       ; 5 bytes - line number
    lea     rcx, [rip + .L_func]          ; 7 bytes - "func_name"
    call    __assert_fail                 ; 5 bytes

    .L_ok:

**Call site size: ~36 bytes**

C++26 Contracts (ABI Entry Point)
-----------------------------------

Each call site passes a single pointer to a ``.rodata`` data block and
calls the appropriate entry point directly.  No per-TU wrappers are
needed---the entry point name encodes the assertion kind, evaluation
semantic, and detection mode:

.. code-block:: asm

    ; Condition check
    cmp     edi, 0                        ; 3 bytes
    jg      .L_ok                         ; 2 bytes

    ; Violation path (1 argument)
    lea     rdi, [rip + .L_data_block]    ; 7 bytes
    call    __cxa_contract_violation_pre_enforce_pf  ; 5 bytes

    .L_ok:

**Call site size: ~17 bytes**

This is **53% smaller** than ``assert()`` per call site.

Summary
-------

.. list-table::
   :header-rows: 1
   :widths: 50 25 25

   * - Approach
     - Call Site
     - vs assert()
   * - ``assert()``
     - 36 bytes
     - baseline
   * - Contracts (ABI entry point, 1 argument)
     - 17 bytes
     - **-53%**

Static Data Comparison
======================

assert()
--------

Each assert site requires string literals:

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Data
     - Typical Size
     - Shared?
   * - Expression string
     - ~20 bytes
     - No (unique per site)
   * - File name string
     - ~30 bytes
     - Yes (per file)
   * - Function name string
     - ~20 bytes
     - Partial (per function)

**Per-site static data: ~20-70 bytes**

Contracts
---------

Each contract site requires a ``__cxa_contract_data_block`` in
``.rodata``:

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Data
     - Size (LP64)
     - Shared?
   * - Block header (descriptor + next pointers)
     - 16 bytes
     - No
   * - ``__cxa_source_location`` (inline)
     - 24 bytes
     - No
   * - Comment pointer (predicate text)
     - 8 bytes
     - No
   * - Message pointer (P3099 diagnostic)
     - 8 bytes
     - No
   * - File name string
     - ~30 bytes
     - Yes (per file)
   * - Function name string
     - ~20 bytes
     - Partial
   * - Source text string
     - ~20 bytes
     - No

**Per-site static data: ~56 bytes (struct) + ~20 bytes (source text)**

The descriptor table is shared across all data blocks of the same
variant within a translation unit, adding negligible overhead (~64-128
bytes per TU for 1-4 descriptor variants).

Program-Wide Impact
===================

For a program with 10,000 contract sites:

.. list-table::
   :header-rows: 1
   :widths: 35 25 20 20

   * - Approach
     - Call Sites
     - Overhead
     - Total Code
   * - assert()
     - 360 KB
     - 0
     - 360 KB
   * - Contracts (ABI entry point)
     - 170 KB
     - ~0.1 KB (descriptors)
     - **170 KB**

Using ABI-level entry points, contracts achieve **53% smaller code**
than traditional ``assert()``.

Additional Benefits
===================

Beyond code size, the ABI provides:

``[[noreturn]]`` Optimization
    Enforce entry points are ``[[noreturn]]``, allowing the compiler to
    eliminate dead code after the call.  This is a declared function
    attribute, not a TU-local annotation---the optimizer gets the
    guarantee without alias analysis.

``noexcept`` Optimization
    Entry points called in noexcept contexts use the ``_noexcept``
    variant, letting the compiler omit exception handling bookkeeping
    (landing pads, EH tables) around contract checks.

Unified Handler
    All contract violations go through a single customizable handler,
    unlike assert() which always calls ``abort()``.

Rich Metadata
    Contracts carry structured metadata (assertion kind, evaluation
    semantic, detection mode, P3099 message, P3400 local handlers)
    enabling sophisticated violation handling.

Deduplication
    Descriptor tables and string literals can be deduplicated by the
    linker, further reducing binary size.

Zero Wrapper Overhead
    No per-TU wrapper functions are emitted.  The 34 entry points
    are provided by the ABI runtime once, shared by all translation
    units.
