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

C++26 Contracts (Generic Entrypoint)
------------------------------------

Using the 6-parameter generic entrypoint:

.. code-block:: asm

    ; Condition check
    cmp     edi, 0                        ; 3 bytes
    jg      .L_ok                         ; 2 bytes

    ; Violation path (6 arguments)
    lea     rdi, [rip + .L_descriptor]    ; 7 bytes
    lea     rsi, [rip + .L_static_data]   ; 7 bytes
    mov     edx, 0x01                     ; 5 bytes - mode
    mov     ecx, 0x01                     ; 5 bytes - semantic
    xor     r8d, r8d                      ; 3 bytes - dynamic_data
    xor     r9d, r9d                      ; 3 bytes - reserved
    call    __cxa_contract_violation_entrypoint  ; 5 bytes

    .L_ok:

**Call site size: ~40 bytes**

C++26 Contracts (Runtime Wrapper)
---------------------------------

Using the 2-parameter runtime wrapper:

.. code-block:: asm

    ; Condition check
    cmp     edi, 0                        ; 3 bytes
    jg      .L_ok                         ; 2 bytes

    ; Violation path (2 arguments)
    lea     rdi, [rip + .L_descriptor]    ; 7 bytes
    lea     rsi, [rip + .L_static_data]   ; 7 bytes
    call    __cxa_contract_violation_pf_se  ; 5 bytes

    .L_ok:

**Call site size: ~24 bytes**

C++26 Contracts (Compiler-Generated Wrapper)
--------------------------------------------

Using single-pointer compiler-generated wrapper:

.. code-block:: asm

    ; Condition check
    cmp     edi, 0                        ; 3 bytes
    jg      .L_ok                         ; 2 bytes

    ; Violation path (1 argument)
    lea     rdi, [rip + .L_static_data]   ; 7 bytes
    call    contract_violation_pf_se      ; 5 bytes

    .L_ok:

**Call site size: ~17 bytes**

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
   * - Contracts (generic, 6 params)
     - 40 bytes
     - +11%
   * - Contracts (runtime wrapper, 2 params)
     - 24 bytes
     - **-33%**
   * - Contracts (compiler wrapper, 1 param)
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

Each contract site requires a static_data blob:

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Data
     - Size (LP64)
     - Shared?
   * - ``__cxa_source_location`` (inline)
     - 24 bytes
     - No
   * - Source text pointer
     - 8 bytes
     - No
   * - Assertion kind
     - 1 byte
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

**Per-site static data: ~33 bytes (struct) + ~20 bytes (source text)**

The descriptor table is shared across all contracts in a translation unit,
adding negligible overhead (~50-100 bytes per TU).

Program-Wide Impact
===================

For a program with 10,000 contract sites:

.. list-table::
   :header-rows: 1
   :widths: 35 25 20 20

   * - Approach
     - Call Sites
     - Wrappers
     - Total Code
   * - assert()
     - 360 KB
     - 0
     - 360 KB
   * - Contracts (generic)
     - 400 KB
     - 0
     - 400 KB
   * - Contracts (runtime wrapper)
     - 240 KB
     - ~0.1 KB
     - 240 KB
   * - Contracts (compiler wrapper)
     - 170 KB
     - ~0.5 KB
     - **170 KB**

Using compiler-generated wrappers, contracts achieve **53% smaller code**
than traditional assert().

Additional Benefits
===================

Beyond code size, contracts provide:

``[[noreturn]]`` Optimization
    Enforced contract wrappers are marked ``[[noreturn]]``, allowing the
    compiler to eliminate dead code after violation calls.

Unified Handler
    All contract violations go through a single customizable handler,
    unlike assert() which always calls ``abort()``.

Rich Metadata
    Contracts carry structured metadata (assertion kind, evaluation semantic,
    detection mode) enabling sophisticated violation handling.

Deduplication
    Descriptor tables and string literals can be deduplicated by the linker,
    further reducing binary size.
