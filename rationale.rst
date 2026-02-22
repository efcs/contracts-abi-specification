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
Contracts ABI specification. Where the specification says *what*, this
document explains *why*.

Versioned Data Pointer
======================

The generic entrypoint (``__cxa_contract_violation_entrypoint``) takes a
single ``void*`` pointer to a versioned data object rather than individual
parameters. This design has several motivations:

**Signature stability.** By passing a single pointer, the entrypoint's
function signature is frozen forever. Adding new data in the future
requires only bumping the version number and appending fields to the
data layout---no signature changes, no new entrypoints, no breaking
existing compiled code.

**Runtime-only data.** Some useful contract violation data can only be
known at runtime and cannot be encoded in the static descriptor table.
For example, a future ABI version might pass the program counter at the
point of violation, enabling integration with debuggers, crash reporters,
and profiling tools without requiring the runtime to capture a stack
trace itself. Another example: a future C++ standard could allow
postconditions to name their return value (``post(r: r > 0)``), and the
violation handler might benefit from access to a type-erased pointer to
that return value for diagnostic purposes. These are values that exist
only at the moment of violation and cannot be statically encoded. The
versioned data layout accommodates such future needs by appending fields
in a new version.

**Append-only versioning.** New versions may only append fields to the
end of the data layout. The meaning of existing bytes never changes when
the version is bumped. This ensures that an older runtime encountering a
newer version can still safely read the fields it knows about.

ABI Type Representations
========================

The type mapping table (§3.1) defines ``__cxa_`` types that serve as a
translation layer between standard library types and the ABI wire format.

The C++ standard deliberately leaves the layout of standard library types
implementation-defined. Two conforming implementations of
``std::contracts::contract_semantic`` might differ in underlying integer
type (``int`` vs ``uint8_t`` vs ``uint32_t``), enumerator values, and
size/alignment. This is perfectly legal C++, but it makes cross-compiler
interoperability impossible if the ABI passes these types directly.

The ``__cxa_`` types define exact underlying types (all ``uint8_t``),
exact enumerator values, and exact size and alignment. The compiler maps
from the standard library's representation to the ABI representation at
the contract site. The runtime maps back when constructing
``std::contracts::contract_violation``.

The "Standard Type" column in the table documents conceptual
correspondence, not type identity.

Compiler-Generated Wrappers
===========================

Compilers emit translation-unit-local wrappers (§4.2) that construct
the versioned data object and call the generic entrypoint. This serves
two purposes:

**Code size at contract sites.** Each contract site reduces to a single
``lea`` + ``call`` with one pointer argument. The wrapper captures the
descriptor table pointer (which is per-TU) and encodes the detection
mode and evaluation semantic, so none of this needs to be repeated at
every contract site.

**Beating assert().** The traditional ``assert(expr)`` macro expands to
a call to ``__assert_fail`` with four arguments (expression string, file
name, line number, function name) loaded at every call site. By
deferring data setup to the TU wrapper, ``contract_assert(expr)`` can
generate less code per site than ``assert(expr)``.

Why the Compiler Must Generate the Wrappers
-------------------------------------------

An earlier design had the runtime provide wrapper entrypoints (e.g.,
``__cxa_contract_violation_pf_se``) that compilers could call instead
of emitting TU-local wrappers. This was removed because it conflicts
with the versioning strategy.

The versioned data pointer gives the *compiler* control over which
version of the data layout to use. If the runtime provides wrappers
that construct the versioned struct internally, those wrappers are
locked to whatever version the runtime was compiled with. A newer
compiler that wants to pass v2 data cannot do so through an old
runtime's wrapper---the wrapper would construct a v1 struct and the
additional fields would be lost.

By requiring the compiler to always construct the versioned struct
itself, forward compatibility is preserved: a new compiler can pass
v2 data to an old runtime's ``__cxa_contract_violation_entrypoint``,
which reads the version byte and processes the fields it understands.

Wrappers for the ``enforced`` evaluation semantic are annotated
``[[noreturn]]``, which allows the compiler to omit fallthrough code
at the contract site and enables better optimization of the surrounding
code.

Descriptor Table Design
=======================

The descriptor table (§5) uses parallel arrays of field types and
data entries. Each ``field_types[i]`` identifies the kind of data, and
``data[i]`` provides either an offset (for standard fields) or a pointer
(for extended fields).

**Field omission.** Users can omit contract data at compile time (source
locations, source text) to save space. The descriptor table supports
this naturally: if a field is omitted, its entry simply does not appear
in the table. No storage, no initialization, no runtime overhead.

**Vendor extensibility.** Values >= ``0x40`` indicate extended fields
with pointer-based data. Vendors can add proprietary extensions
(stack traces, sanitizer integration, etc.) without coordinating field
IDs with other vendors.

Constructor Isolation
=====================

The ABI delegates construction of ``std::contracts::contract_violation``
to the runtime library rather than having the compiler generate
construction code. This is necessary because the compiler does not know
which standard library it is targeting when generating code for a
contract---it only knows whether it is targeting the Itanium ABI or not.
The ``std::contracts::contract_violation`` constructor is defined by a
particular standard library implementation (libc++, libstdc++), and
different implementations may have different layouts, different
constructor signatures, and different inline namespace schemes (e.g.,
``std::__1::`` vs ``std::__cxx11::``). By having the runtime library
construct the object, the compiler avoids needing to know any of these
details.
