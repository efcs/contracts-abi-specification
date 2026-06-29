.. _rationale:

========================================
Contracts ABI: Design Rationale
========================================

.. contents::
   :local:
   :depth: 2

Purpose
*******

This document explains the reasoning behind key design decisions in the
Contracts ABI specification.  Where the specification says *what*, this
document explains *why*.

Data Block Chain
================

The ABI passes violation data as a linked list of self-describing data
blocks (``__cxa_contract_data_block``) rather than a single flat struct
with a version byte.  This is the fundamental architectural choice and
it drives most of the other decisions.

Composable Data Assembly
------------------------

A contract violation involves data from multiple sources assembled at
different times:

1. **Per-assertion constants** (source location, predicate text,
   message, handler trampolines) are known at compile time and emitted
   into ``.rodata``.
2. **Per-evaluation constants** (assertion kind, evaluation semantic)
   may differ for the same assertion depending on the evaluation path or
   TU configuration.
3. **Per-detection-site values** (detection mode) are known at the point
   where the violation is detected.

A flat struct requires all of this to be assembled into one contiguous
allocation, either at the call site (inflating code size) or in a
per-TU wrapper that constructs it.  A linked list lets each source
contribute its own block:

- The compiler emits a single static const block with per-assertion
  data.
- The entry point prepends a stack-allocated override block with
  kind/semantic/mode.
- The P3290 API constructs a complete block on the stack.

All three use the same chain-walking algorithm.  No special cases, no
versioning, no construction glue.

No Version Byte Needed
----------------------

A flat struct requires a version byte so the runtime can interpret the
layout.  A linked list of self-describing blocks does not: each block
carries its own descriptor table that says exactly which fields are
present and where they are.  There is nothing to version because there
is no fixed layout to evolve.

This eliminates an entire class of compatibility concerns.  An "old"
data block and a "new" data block can coexist in the same chain,
produced by different compilers or different compiler versions.  The
runtime just walks the chain and takes what it finds.

Signature Stability
-------------------

Every entry point takes a single ``void*`` argument.  This signature is
frozen forever: all evolution happens in the data, not the calling
convention.  Adding new fields, new kinds, new semantics---none of these
require new function signatures.

Multiple Entry Points
=====================

The ABI defines 34 entry points (2 universal + 32 specialized) rather
than a single generic entry point with per-TU compiler-generated
wrappers.

Optimizer-Visible Attributes
-----------------------------

The key benefit is that each specialized entry point carries its own
function attributes:

- **Enforce** entry points are ``[[noreturn]]``.  The compiler can
  eliminate fallthrough code after the call and assumes the function
  never returns.  This enables dead store elimination, register
  allocation improvements, and smaller function bodies at every
  contract site with enforced semantic.

- **Noexcept** entry points let the compiler omit exception handling
  bookkeeping (landing pads, EH tables) when the contract is in a
  noexcept context.

With a single entry point, these attributes cannot be applied: the
single function sometimes returns (observe) and sometimes doesn't
(enforce), so it cannot be ``[[noreturn]]``.

Same Code Density as Wrappers
-----------------------------

The original design achieved compact call sites (``lea`` + ``call``,
one pointer argument) by having the compiler emit per-TU wrappers that
encoded the descriptor table pointer, detection mode, and evaluation
semantic.

The multiple-entry-point design achieves the same code density without
compiler-generated wrappers: each call site is still ``lea`` + ``call``
with one pointer argument.  The wrapper's job---encoding semantic and
mode---is done by the entry point name instead.

This eliminates per-TU wrapper generation (typically 4-8 small functions
per TU), saving both compile time and object file size, while preserving
the 17-byte call site that is 53% smaller than ``assert()``.

Why the Compiler Does Not Generate Wrappers
-------------------------------------------

An earlier design had compilers emit TU-local wrappers that constructed
a versioned data struct and called a single generic entry point.  This
was replaced for two reasons:

1. **Attribute propagation.**  A TU-local wrapper with internal linkage
   can carry ``[[noreturn]]`` for the enforced case, but the optimizer
   must prove the wrapper is always called with the enforce semantic to
   apply interprocedural optimizations.  An entry point whose *declared
   signature* is ``[[noreturn]]`` gives the optimizer the guarantee
   directly, without alias analysis.

2. **No versioning needed.**  The wrapper's primary value was
   constructing the versioned data struct.  With a chain of
   self-describing data blocks, there is no versioned struct to
   construct.

``contract_violation`` as Chain Pointer
=========================================

``std::contracts::contract_violation`` holds a single pointer to the
head of a data block chain.  All accessors walk the chain to find their
field values.

Eliminating Construction
-------------------------

The original design delegated construction of ``contract_violation`` to
the runtime library, isolating the compiler from the standard library's
layout.  The chain pointer design goes further: it eliminates the
construction step entirely.  The object **is** the chain pointer.
Setting a single pointer field is all that is needed.

This means the compiler, the standard library, and the ABI runtime all
agree on a single fact: ``contract_violation`` holds a ``const
__cxa_contract_data_block*``.  No constructor signatures, no layout
negotiation, no inline namespace differences.

Cross-Standard-Library Compatibility
--------------------------------------

The p2900 specification treats the compiler and standard library as a
unified platform and does not address cross-STL usage.  However, real
toolchains mix compilers and standard libraries (GCC + libc++,
Clang + libstdc++).

Because ``contract_violation`` is a thin wrapper around the ABI chain
pointer, and the ABI types are identical across implementations, cross-
STL usage works naturally.  Both libstdc++ and libc++ define
``contract_violation`` with the same single-pointer layout and the same
chain-walking accessors.  A handler compiled against one standard
library can process a ``contract_violation`` constructed by the other.

Accessor Lifetime
-----------------

Accessors walk the chain on demand rather than caching field values.
This is correct because the chain's lifetime is guaranteed:

- Compiler-generated data blocks have static storage duration.
- Override blocks live on the entry point's stack frame.
- P3290 stack blocks live on the API function's stack frame.

The handler receives ``const contract_violation&``.  The referent and
its chain remain valid until the entry point returns or terminates.

First-Found-Wins Resolution
============================

When walking the chain, the runtime takes the first occurrence of each
field.  This is the mechanism that makes override blocks work:

1. A specialized entry point prepends a block with kind/semantic/mode.
2. The compiler-generated block further down the chain may also contain
   kind/semantic/mode (or may not---it doesn't matter).
3. The accessor finds the override block's values first and uses them.

This principle is simple, predictable, and requires no merging or
conflict resolution logic.  It also enables representation evolution
(a new field ID for the same logical field can be placed earlier in the
chain to take precedence; see specification |S| 7.3).

.. |S| unicode:: U+00A7

Override Block Mechanism
========================

Specialized entry points encode assertion kind, evaluation semantic, and
detection mode in their function names.  At runtime, they make these
values available to the chain-walking algorithm by constructing a small
override block on the stack:

.. code-block:: text

    Override Block (stack)        Data Block (.rodata)
    +------------------+         +------------------+
    | descriptor* -----+--+      | descriptor* -----+--+
    | next* -----------+--+----> | next*: nullptr    |  |
    | kind: 0x01       |  |      | file_name*       |  |
    | semantic: 0x03   |  |      | function_name*   |  |
    | mode: 0x01       |  |      | line, column     |  |
    +------------------+  |      | comment*          |  |
                          |      | message*          |  |
    Override Descriptor   |      +------------------+  |
    (static const)     <--+      Data Descriptor    <--+
    +------------------+         +------------------+
    | header: 0x10     |         | header: 0x11     |
    | num_entries: 3   |         | num_entries: 3   |
    | IDs: 07 08 09    |         | IDs: 01 02 03    |
    | data: [16,17,18] |         | data: [offsets]  |
    +------------------+         +------------------+

The override descriptor table is a single static constant shared by all
specialized entry points in the ABI runtime.  Only the three uint8_t
values (kind, semantic, mode) vary per call.

This design means the compiler does not need to encode kind, semantic, or
mode in the static data block.  The entry point name determines them.
This keeps the per-assertion data smaller and avoids redundancy between
the entry point name and the data.

ABI Type Representations
========================

The type mapping table (specification |S| 3.1) defines ``__cxa_`` types
as a translation layer between standard library types and the ABI wire
format.

The C++ standard deliberately leaves the layout of standard library types
implementation-defined.  Two conforming implementations of
``std::contracts::evaluation_semantic`` might differ in underlying
integer type (``int`` vs ``uint8_t`` vs ``uint32_t``), enumerator values,
and size/alignment.  This is perfectly legal C++, but makes cross-compiler
interoperability impossible if the ABI passes these types directly.

The ``__cxa_`` types define exact underlying types (all ``uint8_t``),
exact enumerator values, and exact size and alignment.  The compiler maps
from the standard library's representation to the ABI representation at
the contract site.  The runtime maps back when the
``contract_violation`` accessors are called.

P3400 Local Handler Dispatch
==============================

P3400 assertion-control objects allow per-assertion handler dispatch via
labels with ``handle_contract_violation`` and ``query`` facets.  The ABI
supports this through two fields in the data block:

- **Field 0x04 (local_handler)**: A type-erased function pointer
  (trampoline) that calls the label's handler method.
- **Field 0x06 (label_ptr)**: The address of the assertion-control
  object instance.

Trampoline Pattern
------------------

The handler and query functions are stored as type-erased function
pointers rather than virtual function calls or template instantiations.
This avoids:

- Pulling label types or vtables across the ABI boundary.
- Template instantiation of ABI runtime code for each label type.
- Runtime type information requirements.

The compiler generates a small internal-linkage trampoline per label type
that casts the ``void*`` arguments back to their typed counterparts.  The
trampoline function pointer is a link-time relocation in constant data---
no runtime initialization needed.

Return Value Convention
-----------------------

The local handler returns ``int``:

- ``1``: handled.  The global handler is skipped.  If the semantic is
  enforce, the program terminates.  If observe, execution continues.
- ``0``: not handled.  Dispatch continues to the global handler.

This allows a local handler to inspect the violation and decide whether
to handle it locally or defer to the global handler.

P3290 Manual Violation API
============================

The P3290 functions ``handle_enforced_contract_violation`` and
``handle_observed_contract_violation`` construct a data block on the stack
with kind ``CXA_AK_MANUAL``, the appropriate semantic, and
``CXA_DM_UNSPECIFIED`` detection mode.  They call the universal entry
point (``__cxa_contract_violation`` or ``__cxa_contract_violation_noexcept``)
because the semantic is encoded in the block data, not the entry point
name.

This works because the stack-constructed block has exactly the same format
as compiler-generated blocks.  The chain-walking algorithm processes them
identically.  No special API entry points are needed beyond the existing
universal entry points.

P3099 Message Field
====================

The ``message`` field (``0x03``) is distinct from ``comment`` (``0x02``).

- **Comment** contains the source text of the contract predicate
  (e.g., ``"x > 0"``).
- **Message** contains a computed diagnostic string provided by the
  P3099 ``message`` property of the contract assertion.

Both are ``const char*`` fields in the data block.  Both default to
``""`` when absent.  They have separate field IDs so either can be
omitted independently (e.g., stripping predicate text for security
while retaining diagnostic messages, or vice versa).

Descriptor Table Design
========================

The descriptor table uses parallel arrays of field IDs and data entries.
Each ``field_ids[i]`` identifies the kind of data, and ``data[i]``
provides either an offset (for standard fields) or a pointer (for
extended fields).

Field Omission
--------------

Users can omit contract data at compile time (source locations, source
text) to save space.  The descriptor table supports this naturally: if a
field is omitted, its entry simply does not appear in the table.  No
storage, no initialization, no runtime overhead.

Vendor Extensibility
--------------------

Values >= ``0x40`` indicate extended fields with pointer-based data.
Vendors can add proprietary extensions (stack traces, sanitizer
integration, etc.) without coordinating field IDs with other vendors.

Field ID Numbering
------------------

Standard field IDs start at ``0x01`` rather than a higher value.  This
keeps the ID space compact and avoids wasting the low range.  The
``0x00`` value is unused (not a valid field ID), maintaining the
invariant that ``0x00`` means "unspecified" throughout the ABI.

Single Header Byte
-------------------

The descriptor table packs the version (4 bits) and vendor ID (4 bits)
into a single ``uint8_t`` header byte, accessed via shift/mask operations
rather than C++ bitfields.  This avoids endian-dependent bit layout and
makes the binary format unambiguous across platforms.

Handler Symbol Resolution
==========================

The global violation handler uses the weak symbol pattern:

.. code-block:: cpp

    __attribute__((weak))
    void handle_contract_violation(
        const std::contracts::contract_violation&);

This matches the p2900 specification's description of handler replacement
"in the same way it would be done for the global ``operator new`` and
``operator delete``."  It requires no special compiler treatment:

- The runtime declares the handler as a weak symbol.
- Users define their handler with the same signature.
- The linker resolves to the user's definition if present.
- The runtime checks whether the weak symbol is defined before calling.
- ``invoke_default_contract_violation_handler`` is always available for
  handlers that want to delegate.

ABI Evolution Strategy
========================

The data block chain format is more evolvable than a versioned flat
struct:

**Adding a field** requires only assigning a new field ID.  No struct
layout changes, no version bump, no coordination.  Old runtimes ignore
the new ID.  New runtimes supply defaults when old compilers don't emit
it.

**Adding an entry point** is equally simple: define a new function.
Old compilers don't call it.  New runtimes provide it.  Old runtimes
don't need it.

**Changing a representation** uses the multi-ID mechanism
(specification |S| 7.3): assign a new field ID for the same logical field
with the new encoding.  A compiler targeting broad compatibility emits
both the new and old IDs in its data block (new ID in the block's
descriptor, old ID available in a chained block or as a second entry).
First-found-wins gives new runtimes the new encoding and old runtimes the
old encoding.

This contrasts with versioned-struct evolution, where adding a field
requires bumping the version number, ensuring all readers handle both old
and new versions, and potentially growing the on-wire size of every
violation even when the new field is unused.  The chain format pays only
for fields that are actually present.
