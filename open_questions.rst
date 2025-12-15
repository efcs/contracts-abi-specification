.. _open_questions:

========================================
Open Questions: ABI Compatibility Issues
========================================

.. contents::
   :local:
   :depth: 2

Purpose
*******

This document collects questions about the compatibility and interoperability aspects of the C++ Contracts ABI, particularly concerning user-provided entrypoints, cross-standard-library usage, and C/C++ interoperability.

Questions are presented along with answers from the C++ Contracts specification (p2900r14) where available. Unanswered questions remain open for ABI design decisions.

.. important::
   The answers to these questions will significantly impact the ABI specification, particularly regarding:

   - Whether ``std::contract_violation`` layout must be standardized
   - Whether C entrypoints must be supported
   - Whether cross-STL usage should be allowed or prevented
   - What symbols and calling conventions must be specified

Context
*******

The current design specifies ``__cxa_contract_violation_entrypoint`` as the runtime-provided entrypoint that compilers call. However, there are questions about:

1. User-provided ``handle_contract_violation`` functions
2. Mixing different standard library implementations (libc++ vs libstdc++)
3. C/C++ interoperability for contract handlers

This document explores these issues systematically and provides answers from the specification where available.

User-Provided Violation Handlers
*********************************

Question 1.1: Can Users Provide Their Own Handler?
===================================================

**ANSWER: YES** (from p2900r14 Section 3.5.9, chunk 38)

    The contract-violation handler is a function named ``::handle_contract_violation`` that is attached to the global module and has C++ language linkage. This function will be invoked when a contract violation is identified at run time.

    This function:

    - shall take a single argument of type ``const std::contracts::contract_violation&``
    - shall return ``void``
    - may be ``noexcept``

Question 1.2: What Is the Exact Signature and Mangling?
========================================================

**ANSWER:** (from p2900r14 Section 3.5.9, formal wording chunk 68)

.. code-block:: cpp

   // Full signature
   void ::handle_contract_violation(const std::contracts::contract_violation&);

   // Or with namespace
   namespace std::contracts {
       void handle_contract_violation(const contract_violation&);
   }

**Key properties:**

- **C++ language linkage** (NOT ``extern "C"``)
- **Attached to the global module**
- **Mangled according to C++ name mangling rules**
- **May optionally be** ``noexcept``

From p2900r14 chunk 69:

    It is implementation-defined whether the contract-violation handler is replaceable ([dcl.fct.def.replace]). If the contract-violation handler is not replaceable, a declaration of a replacement function for the contract-violation handler is ill-formed, no diagnostic required.

.. important::
   **Replacement is implementation-defined**, like ``operator new``/``operator delete``.

Question 1.3: How Does This Relate to __cxa_contract_violation_entrypoint?
===========================================================================

.. admonition:: Open Question (ABI Design Decision)

   The specification says users provide ``handle_contract_violation``, but doesn't specify the low-level calling mechanism.

   Should the Itanium ABI define:

   **Option A: Indirect call through runtime entrypoint**

   .. code-block:: text

      Compiler → __cxa_contract_violation_entrypoint →
          → Construct std::contract_violation from descriptors →
              → user's handle_contract_violation(cv)

   **Option B: Direct call to user function**

   .. code-block:: text

      Compiler → user's handle_contract_violation(cv) directly
      (no __cxa_contract_violation_entrypoint)

   **Option C: Runtime-constructed approach**

   Compiler generates thunk that constructs ``std::contract_violation`` and calls user's handler directly (see :ref:`runtime_constructed_objects`)

.. note::
   From p2900r14 chunk 39:

       Whether ``::handle_contract_violation`` is replaceable is implementation-defined. When it is replaceable, that replacement is done in the same way it would be done for the global ``operator new`` and ``operator delete``.

No Declaration Provided in Standard Headers
============================================

From p2900r14 Section 3.5.9 (chunk 39):

    The Standard Library provides no user-accessible declaration of the default contract-violation handler, and users have no way to call it directly. No implicit declaration of this function occurs in any translation unit.

**Rationale:**

    Enabling this flexibility is a primary motivation for not providing any declaration of ``::handle_contract_violation`` in the Standard Library; whether that declaration was ``noexcept`` would force that decision on user-provided contract-violation handlers.

This allows users to choose whether their handler is ``noexcept``, ``[[noreturn]]``, or has preconditions/postconditions.

.. admonition:: Implication for ABI

   Since no standard declaration is provided, users must write their own declaration. The ABI must specify how the symbol is resolved (weak linkage? strong linkage? implementation-defined?)

Magic Involved in Compiling User Handlers
******************************************

Question 6.1: Multiple Signatures Like main()?
===============================================

**ANSWER: NO** (from p2900r14 Section 3.5.9)

Unlike ``main()``, ``handle_contract_violation`` supports **exactly one signature**:

.. code-block:: cpp

   void handle_contract_violation(const std::contract_violation& cv);

From the formal wording (chunk 83):

    A declaration of the replacement function:

    - shall not be inline
    - shall be attached to the global module
    - shall have C++ language linkage
    - shall have the same return type as the replaceable function

Users can add attributes like ``[[noreturn]]`` or make it ``noexcept``, but the core signature is fixed.

.. note::
   **No "magic" is needed.** It's a straightforward replaceable function like ``operator new``. The ABI needs to define how the symbol is found and called, but there's no need for signature detection or thunking between different signatures.

Question 6.2: Different Standard Library Implementations?
==========================================================

**PARTIAL ANSWER from specification:**

From p2900r14 Section 3.8 (chunk 57):

    We propose a feature test macro for the proposed language feature, ``__cpp_contracts``, and a separate feature test macro for the proposed library API, ``__cpp_lib_contracts``. Two separate macros are provided as **library implementations and compiler implementations can, in some cases, come from different providers (such as when using libc++ along with GCC) and thus have different levels of support for Contracts.**

The specification **acknowledges** mixing (libc++ with GCC) but only addresses it for **feature detection**, NOT for ABI compatibility.

From p2900r14 Section 3.7.5 (chunk 57):

    Note that Standard Library implementers and compiler implementers must work together to make use of contract assertions on Standard Library functions. [...] **This agreement between library implementers and compiler vendors is needed because, as far as the Standard is concerned, they are the same entity and provide a single interface to users.**

.. admonition:: Open Question (ABI Design Decision)

   If a user compiles their handler against libc++'s ``std::contract_violation``:

   .. code-block:: cpp

      // user_handler.cpp - compiled with libc++
      void handle_contract_violation(const std::contracts::contract_violation& cv) {
          // cv is libc++::contract_violation
      }

   But then links against code using libstdc++, what happens when a contract fails in the libstdc++ code?

   The specification treats compiler + standard library as a **unified platform** and does NOT address cross-STL usage.

Implementation-Defined Layout
==============================

From p2900r14 Section 3.7.3 (chunk 55):

    Whether the object is polymorphic is implementation-defined; if it is polymorphic, the primary purpose in being so is to allow for the use of ``dynamic_cast`` to identify whether the provided object is an instance of an implementation-defined subclass of ``std::contracts::contract_violation``.

.. warning::
   The ``std::contract_violation`` type is **implementation-defined**:

   - Different standard libraries will have different layouts
   - Different standard libraries may or may not use polymorphism
   - Cross-STL usage is NOT explicitly supported by the specification
   - The specification leaves this as a QoI (Quality of Implementation) issue

Cross-Standard-Library Compatibility
*************************************

The Core Question
=================

.. pull-quote::
   "Can I compile it with libc++ and then use it with libstdc++?"

Possible Scenarios
==================

.. admonition:: Open Question (Needs Clarification)

   Which scenario are you asking about?

**Scenario A: Mixed Compilation**
   Compile application with Clang+libc++, link against a library compiled with GCC+libstdc++

**Scenario B: Runtime Substitution**
   Compile application with Clang+libc++, but at runtime use libstdc++'s ``std::contract_violation`` implementation

**Scenario C: Something else?**
   Please clarify

The std::contract_violation Type Problem
=========================================

The ``std::contract_violation`` type itself is defined by the standard library. Different implementations (libc++ vs libstdc++) will have different:

- Inline namespaces (e.g., ``std::__libcpp`` vs ``std::__cxx11``)
- Member layouts
- Vtable layouts (if virtual)
- Mangled names

.. admonition:: Open Question (ABI Design Decision)

   Given these differences, what does "mixing" standard libraries mean in the context of contracts?

   Are you concerned about:

   - **Binary compatibility:** Object files with different ``std::contract_violation`` types linking together?
   - **Source compatibility:** Code compiled against one STL being used with another?
   - **Runtime compatibility:** The runtime library understanding ``std::contract_violation`` from different STL implementations?

Should the ABI Standardize the Layout?
=======================================

.. admonition:: Open Question (ABI Design Decision)

   Is the actual concern that different standard libraries might have different ``std::contract_violation`` layouts, and the ABI needs to handle this?

   The Itanium ABI could:

   1. **Standardize the layout** - All implementations must use the same memory layout (goes beyond the C++ standard)
   2. **Keep layouts implementation-specific** - Prevent cross-STL mixing, require matching implementations
   3. **Use abstraction layer** - Descriptor approach isolates implementations from each other

.. note::
   The current descriptor approach deliberately does NOT standardize ``std::contract_violation`` layout. Each standard library implementation can define it however they want, because:

   - The compiler calls ``__cxa_contract_violation_entrypoint`` (ABI-stable)
   - The runtime constructs ``std::contract_violation`` internally (implementation-specific)
   - No ``std::contract_violation`` objects cross ABI boundaries (in descriptor approach)

C/C++ Interoperability
***********************

Question 3.1: Can C Code Provide Handlers?
===========================================

**ANSWER: NO** (from specification)

The specification explicitly requires:

- The handler has **"C++ language linkage"** (chunk 38, chunk 83)
- It takes a ``std::contracts::contract_violation&`` which is a C++ type

.. important::
   The specification does NOT provide any mechanism for C handlers. The handler signature is purely C++.

   **No mention in specification of:**

   - ``extern "C"`` signatures
   - C-compatible handlers
   - Type erasure for C code
   - Accessor APIs for C

.. admonition:: Open Question (ABI Extension)

   If C interoperability is desired, should the Itanium ABI provide an extension?

   Possible approaches:

   **Type-erasure approach**
      Pass ``std::contract_violation`` as a ``void*`` in C signatures, with a separate C API to extract fields

   **Thunk approach**
      Have a C++-mangled version that forwards to a C-mangled version with transformed arguments

   **ABI-level compatibility**
      Make ``std::contract_violation`` layout compatible with a C struct (constrains layout significantly)

   **Dual symbols approach**
      Provide both ``extern "C"`` and ``extern "C++"`` versions, with the runtime choosing which to call

Question 3.2: Use Cases for C/C++ Interoperability?
====================================================

.. admonition:: Open Question (Motivating Use Cases)

   What is the concrete use case for C/C++ interoperability with contract handlers?

   Examples:

   - C code that wants to handle contracts from C++ libraries?
   - C++ code that wants to use C logging libraries in handlers?
   - Mixed C/C++ codebases with a unified contract handling strategy?
   - Embedded systems where the handler is in C for simplicity?
   - Something else?

.. warning::
   Supporting C handlers may significantly complicate the ABI design and constrain future evolution of ``std::contract_violation``.

ABI Stability and Layout Evolution
***********************************

The Layout Lock-In Problem
===========================

.. admonition:: Open Question (ABI Design Decision)

   The descriptor table approach currently lets the runtime construct ``std::contract_violation`` however it wants, since the runtime *owns* that type.

   But if user code provides ``handle_contract_violation(const std::contract_violation&)``, then:

   - Does this **lock down** the ``std::contract_violation`` layout across standard library versions?
   - Does each standard library implementation need its own version?
   - How do we handle ABI evolution of ``std::contract_violation`` itself?

.. admonition:: Open Question (ABI Design Decision)

   If libc++ and libstdc++ have different ``std::contract_violation`` layouts, how can a user-provided ``handle_contract_violation`` work with both?

   Options:

   1. **It can't** - User handler must be recompiled for each standard library
   2. **ABI standardizes layout** - All standard libraries use the same layout
   3. **Abstraction layer** - Handler doesn't receive ``std::contract_violation`` directly

.. admonition:: Current Design Rationale

   The descriptor approach deliberately keeps ``std::contract_violation`` as an implementation detail of the standard library to avoid:

   - Standardizing layout in the Itanium ABI
   - Creating dependencies between compiler and STL versions
   - Constraining future evolution of the standard library type

   Allowing user-provided ``handle_contract_violation`` may undermine these goals, since the user handler must understand the ``std::contract_violation`` type from whichever standard library is in use.

Deployment and Linkage Questions
*********************************

Where Does the User Function Live?
===================================

.. admonition:: Open Question (ABI Design Decision)

   Where would the user-provided ``handle_contract_violation`` live?

   - In the user's application code?
   - In a user-provided library?
   - Weak-linked, so the runtime provides a default?

From p2900r14 chunk 39:

    Whether ``::handle_contract_violation`` is replaceable is implementation-defined. When it is replaceable, that replacement is done in the same way it would be done for the global ``operator new`` and ``operator delete``.

.. note::
   The specification allows implementations to choose whether replacement is supported at all.

Multiple Definitions
=====================

.. admonition:: Open Question (ABI Design Decision)

   If it's a weak symbol that users can override, what happens when:

   - Multiple translation units provide different implementations?
   - A library provides one implementation and the application provides another?
   - Multiple libraries each provide their own implementation?

   Should the behavior match ``operator new`` replacement?

Call Chain with Descriptor Approach
====================================

.. admonition:: Open Question (ABI Design Decision)

   How does this interact with the descriptor-based approach?

   Would the call chain be:

   .. code-block:: text

      Compiler → __cxa_contract_violation_entrypoint →
          → Construct std::contract_violation from descriptors →
              → user's handle_contract_violation(cv)?

   Or something different?

.. seealso::
   The runtime-constructed thunk approach (§runtime_constructed_objects.rst) has the compiler directly construct the object and call the handler, avoiding the runtime entrypoint entirely.

Additional Findings from Specification
***************************************

User Handler Can Throw Exceptions
==================================

From p2900r14 Section 3.6.6 (chunk 50):

    No restrictions are placed on what a user-defined contract-violation handler is allowed to do. In particular, a user-defined contract-violation handler is allowed to exit other than by returning, e.g., terminating, calling ``longjmp``, and so on.

Example provided:

.. code-block:: cpp

   void handle_contract_violation(const std::contracts::contract_violation& v) {
       throw my_contract_violation_exception(v);
   }

User Handler Must Handle Concurrency and Recursion
===================================================

From p2900r14 Section 3.6.4 (chunk 49):

    A user-defined contract-violation handler is responsible for handling recursive violations explicitly if the user wishes to avoid overflowing the call stack.

From p2900r14 Section 3.6.5 (chunk 49):

    Any user-provided contract-violation handler is responsible for being similarly safe when invoked concurrently.

.. warning::
   User handlers must be:

   - Thread-safe if called concurrently
   - Reentrant-safe to avoid stack overflow from recursive violations

invoke_default_contract_violation_handler() Function
=====================================================

From p2900r14 Section 3.7.4 (chunk 56):

    ``invoke_default_contract_violation_handler`` takes a single argument of type lvalue reference to const ``contract_violation``. Since such an object cannot be constructed or copied by the user and is provided only by the implementation during contract-violation handling, this function can be called only during the execution of a user-defined contract-violation handler.

This allows user handlers to delegate back to the default handler:

.. code-block:: cpp

   void handle_contract_violation(const std::contracts::contract_violation& v) {
       log_violation(v);
       // Delegate to default handler
       std::contracts::invoke_default_contract_violation_handler(v);
   }

Edge Cases and Failure Modes
*****************************

Scenario 1: Mismatched STLs at Link Time
=========================================

.. code-block:: text

   library.so:  compiled with libstdc++, provides handle_contract_violation
                taking libstdc++::contract_violation
   app:         compiled with libc++, contract fails, constructs
                libc++::contract_violation

.. admonition:: Open Question (ABI Design Decision)

   Should this:

   - Be detected at link time (ODR violation)?
   - Fail at runtime?
   - Be explicitly supported (somehow)?
   - Be explicitly forbidden by the ABI?

.. danger::
   If both symbols are present with different manglings, the linker will happily include both, but only one will ever be called. This creates confusion and potential runtime errors.

Scenario 2: C Handler with C++ Contracts
=========================================

.. code-block:: c

   // user.c
   extern "C" void handle_contract_violation(void* cv_opaque) {
       // C code handling C++ contracts
       // How does it access the fields?
   }

.. admonition:: Open Question (ABI Extension)

   Should this:

   - Work transparently with type erasure?
   - Require explicit opt-in and additional C accessor APIs?
   - Be unsupported (per specification)?

Scenario 3: Multiple Standard Library Versions
===============================================

.. code-block:: text

   libold.so:    compiled with libstdc++ 13, old std::contract_violation layout
   libnew.so:    compiled with libstdc++ 15, new std::contract_violation layout
   application:  links both, which handle_contract_violation is called?

.. admonition:: Open Question (ABI Design Decision)

   How is this scenario handled?

   - The standard library version used by the application wins?
   - Runtime dispatch based on version tags?
   - This scenario is explicitly not supported?

.. warning::
   The descriptor approach handles this gracefully because ``std::contract_violation`` is always constructed by the same runtime library that defines it. User-provided handlers may break this.

Scenario 4: Weak Symbol Override Conflicts
===========================================

.. code-block:: cpp

   // libfoo.so
   void handle_contract_violation(const std::contract_violation& cv) {
       log_to_file(cv);
   }

   // libbar.so
   void handle_contract_violation(const std::contract_violation& cv) {
       log_to_database(cv);
   }

   // app links both libfoo and libbar
   // Which handler is used?

.. admonition:: Open Question (ABI Design Decision)

   What is the resolution behavior?

   - Linker picks one arbitrarily (typical weak symbol behavior)?
   - Link error due to multiple definitions?
   - Both are called somehow?
   - This scenario is prohibited?

Practical Implications for Requirements Doc
********************************************

Where Does This Discussion Belong?
===================================

.. admonition:: Open Question (Documentation)

   Which requirement sections would this discussion belong in?

   - "Cross-Compiler Interoperability" (§requirements.rst:56)?
   - "ABI Stability" (§requirements.rst:38)?
   - A new section on "Cross-Standard-Library Compatibility"?
   - A new section on "User-Provided Entrypoints"?
   - A new section on "C Interoperability"?

What ABI Decisions Are Affected?
=================================

.. admonition:: Open Question (ABI Design Scope)

   What specific ABI decisions depend on the answers to these questions?

   - Whether ``std::contract_violation`` layout must be standardized in the Itanium ABI?
   - Whether the ABI must support C entrypoints?
   - Whether the ABI must prevent or allow cross-STL usage?
   - What symbols and calling conventions must be specified?
   - Whether vendor extensions are constrained by C compatibility?

Design Philosophy Questions
****************************

Goals and Priorities
====================

.. admonition:: Open Question (Design Goals)

   Is the goal to:

**Maximize flexibility**
   Allow users to provide handlers in C or C++, mix STLs, override at various levels, etc.

**Maximize safety**
   Prevent mixing, require strict matching, detect conflicts at link time

**Maximize portability**
   Define a minimal, portable subset that works everywhere

**Defer to implementation**
   Let each vendor decide how to handle these scenarios (per specification)

.. tip::
   Different goals lead to very different ABI designs. The descriptor approach prioritizes safety and portability over maximum flexibility.

What Should the Itanium ABI Specify?
=====================================

.. admonition:: Open Question (ABI Specification Scope)

   Should the Itanium ABI specification:

   - Standardize ``std::contract_violation`` layout (specification leaves it implementation-defined)?
   - Only standardize ``__cxa_contract_violation_entrypoint`` (current approach)?
   - Also standardize ``handle_contract_violation`` user entrypoint?
   - Standardize C-compatible interfaces (not in specification)?
   - Provide guidelines but leave details to implementations?

Missing Context
***************

Use Cases and Motivation
=========================

.. admonition:: Open Question (Motivating Use Cases)

   Is there a specific use case or deployment scenario that motivates these questions?

   For example:

   - A specific project trying to mix Clang and GCC builds?
   - Embedded systems wanting C handlers for simplicity?
   - Platform vendors concerned about ABI lock-in?
   - Framework developers wanting to intercept all contract violations?
   - Test harnesses wanting to override contract handling?

.. admonition:: Open Question (Prior Art)

   Are there examples from other language/runtime ABIs that handle this well (or poorly) that we should learn from?

   Examples might include:

   - How different C++ standard libraries handle exceptions
   - How sanitizers intercept runtime events
   - How signal handlers work across C/C++ boundaries
   - How assertion handlers work in C (``assert.h``)

Summary: What We Know from the Specification
*********************************************

.. list-table::
   :header-rows: 1
   :widths: 30 50 20

   * - Question
     - Answer from p2900r14
     - Status
   * - Q1.1: Can users provide handlers?
     - YES. Define ``::handle_contract_violation`` with C++ linkage, attached to global module. Replacement is implementation-defined.
     - ✓ Answered
   * - Q1.2: What is the signature?
     - ``void handle_contract_violation(const std::contracts::contract_violation&)`` with C++ linkage. May be ``noexcept``.
     - ✓ Answered
   * - Q1.3: How does it relate to ABI entrypoint?
     - Specification doesn't say. ABI design decision.
     - ✗ Open
   * - Q6.1: Multiple signatures like main()?
     - NO. Only one signature supported.
     - ✓ Answered
   * - Q6.2: Different STL implementations?
     - Acknowledged (libc++ with GCC) but NOT solved. Compiler+STL treated as unified platform.
     - ⚠ Partial
   * - Q3.x: C interoperability?
     - NOT ADDRESSED. Handler must have C++ linkage and take C++ type.
     - ✓ Answered (No)
   * - Q2.x: Cross-STL compatibility?
     - NOT ADDRESSED. ``contract_violation`` layout is implementation-defined.
     - ⚠ Partial

Key Implications for Itanium ABI Work
**************************************

Based on the specification findings:

1. **The specification does NOT define an ABI for std::contract_violation**

   - Layout is implementation-defined
   - Each standard library can define its own layout
   - The Itanium ABI must decide how to handle this

2. **The specification does NOT address cross-STL usage**

   - Each implementation is expected to provide its own complete stack
   - The Itanium ABI must decide whether to support or prevent cross-STL mixing

3. **The specification does NOT support C handlers**

   - C++ linkage and C++ types only
   - If C support is desired, it must be an ABI extension

4. **The handler replacement mechanism is implementation-defined**

   - Platforms can choose NOT to support user handlers
   - The Itanium ABI should define how replacement works on platforms that support it

5. **No declaration is provided in standard headers**

   - Users must provide their own declaration if they want to replace the handler
   - This gives implementations flexibility in symbol resolution

6. **The handler signature is fixed**

   - No multiple signatures like ``main()``
   - No signature detection or thunking needed
   - ABI can assume a single, well-defined signature

Design Freedom for Itanium ABI
*******************************

The specification deliberately leaves many ABI details to implementations:

.. admonition:: ABI Design Choices

   The Itanium ABI specification has freedom to decide:

   1. **Whether to standardize std::contract_violation layout**

      - Pro: Enables cross-STL compatibility
      - Con: Constrains standard library implementations

   2. **How the compiler calls the handler**

      - Directly (runtime-constructed approach)
      - Via entrypoint (descriptor approach)
      - Hybrid approach

   3. **Whether to support cross-compiler/cross-STL usage**

      - Support it (requires standardizing layout)
      - Prevent it (keep layouts implementation-specific)
      - Abstract it (descriptor-based indirection)

   4. **Whether replacement is mandatory or optional**

      - Mandatory: All platforms must support user handlers
      - Optional: Implementation-defined (matches specification)

   5. **C interoperability extensions**

      - Not in the standard, but could be added as an extension
      - Would require additional ABI surface

Next Steps
**********

Once the open questions are resolved:

1. Update ``requirements.rst`` with new compatibility requirements
2. Update ``descriptor_table_approach.rst`` or ``runtime_constructed_objects.rst`` as appropriate
3. Add test scenarios to ``test_cases.rst`` for the new requirements
4. Update ``specification-proposal.rst`` with any new ABI interfaces

.. seealso::
   - :ref:`requirements` - Design Requirements document
   - :ref:`descriptor_table_approach` - Descriptor Table approach
   - :ref:`runtime_constructed_objects` - Runtime-Constructed Thunks approach
