.. _spec_answers:

=====================================================
Specification Answers to Open Questions
=====================================================

.. contents::
   :local:
   :depth: 2

Purpose
*******

What does p2900r14 say about the questions that matter for the ABI?

Question 1: User-Provided Violation Handlers
*********************************************

How Users Provide Handlers
===========================

**Answer: YES, users can provide their own handler by defining a specific function.**

From p2900r14 Section 3.5.9 (chunk 38):

    The contract-violation handler is a function named ``::handle_contract_violation`` that is attached to the global module and has C++ language linkage. This function will be invoked when a contract violation is identified at run time.

    This function:

    - shall take a single argument of type ``const std::contracts::contract_violation&``
    - shall return ``void``
    - may be ``noexcept``

Replacement Mechanism
=====================

From p2900r14 Section 3.5.9 (chunk 39):

    Whether ``::handle_contract_violation`` is replaceable is **implementation-defined**. When it is replaceable, that replacement is done in the same way it would be done for the global ``operator new`` and ``operator delete``, i.e., by defining a function that has the correct signature (function name and argument types), has the correct return type, and satisfies the requirements listed above. Such a function is called a **user-defined contract-violation handler**.

From the formal wording (chunk 69):

    It is implementation-defined whether the contract-violation handler is replaceable ([dcl.fct.def.replace]). If the contract-violation handler is not replaceable, a declaration of a replacement function for the contract-violation handler is ill-formed, no diagnostic required.

.. important::
   The replacement mechanism is **implementation-defined**. Platforms can choose NOT to support user-provided handlers.

No Standard Declaration Provided
=================================

From p2900r14 Section 3.5.9 (chunk 39):

    The Standard Library provides no user-accessible declaration of the default contract-violation handler, and users have no way to call it directly. No implicit declaration of this function occurs in any translation unit, even though the function might be directly or indirectly invoked from the evaluation of any contract assertion.

**Rationale** (chunk 39):

    Enabling this flexibility is a primary motivation for not providing any declaration of ``::handle_contract_violation`` in the Standard Library; whether that declaration was ``noexcept`` would force that decision on user-provided contract-violation handlers, like it does for the global ``operator new`` and ``operator delete``, which have declarations that are ``noexcept`` provided in the Standard Library.

This allows users to choose:

- Whether the handler is ``noexcept``
- Whether it is ``[[noreturn]]``
- Whether it has its own preconditions and postconditions

.. admonition:: Implication for ABI

   Since no standard declaration is provided, users must write their own declaration if they want to provide a custom handler. This gives implementations flexibility in how they handle the symbol resolution.

Question 6.1: Multiple Signatures Like main()?
***********************************************

**Answer: NO. Only one signature is supported.**

The specification is explicit that there is exactly **one** signature:

- Single parameter: ``const std::contracts::contract_violation&``
- Return type: ``void``
- May or may not be ``noexcept`` (user's choice)

From p2900r14 Section 3.5.9 (chunk 39):

    Such a declaration would also prevent users from choosing properties of their own replacement function, such as whether it is ``noexcept`` or ``[[noreturn]]`` or whether it has its own preconditions and postconditions.

Unlike ``main()``, ``handle_contract_violation`` does **NOT** support multiple signatures.

Replaceable Function Requirements
==================================

From the formal wording (chunk 83):

    A declaration of the replacement function:

    - shall not be inline
    - shall be attached to the global module
    - shall have C++ language linkage
    - shall have the same return type as the replaceable function

Users can add their own attributes like ``[[noreturn]]`` or preconditions/postconditions, but the core signature must match exactly.

.. admonition:: Implication for ABI

   No "magic" is needed to support multiple signatures. The handler is a straightforward replaceable function like ``operator new``. The ABI needs to define how the symbol is found and called, but there's no need for signature detection or thunking between different signatures.

Question 6.2: Different Standard Library Implementations
*********************************************************

**Answer: Acknowledged but NOT solved by the specification.**

Acknowledgment of Mixed Scenarios
==================================

From p2900r14 Section 3.8 (chunk 57):

    We propose a feature test macro for the proposed language feature, ``__cpp_contracts``, and a separate feature test macro for the proposed library API, ``__cpp_lib_contracts``. Two separate macros are provided as **library implementations and compiler implementations can, in some cases, come from different providers (such as when using libc++ along with GCC) and thus have different levels of support for Contracts.**

.. important::
   The specification explicitly acknowledges that libc++ can be used with GCC, but only addresses this for **feature detection**, NOT for ABI compatibility.

Compiler + Standard Library as Unified Platform
================================================

From p2900r14 Section 3.7.5 (chunk 57):

    Note that Standard Library implementers and compiler implementers must work together to make use of contract assertions on Standard Library functions. Currently, compilers, as part of the platform defined by the C++ Standard, take advantage of knowledge that certain Standard Library invocations are undefined behavior. Such optimizations must be skipped to meaningfully evaluate a contract assertion when that same contract has been violated. **This agreement between library implementers and compiler vendors is needed because, as far as the Standard is concerned, they are the same entity and provide a single interface to users.**

.. admonition:: Implication for ABI

   The specification treats compiler + standard library as a **unified platform**. Cross-STL mixing is acknowledged to exist in practice, but the specification does NOT address how ``std::contract_violation`` would work across different standard library implementations.

   This is explicitly left as an ABI implementation concern.

Implementation-Defined Layout
==============================

From p2900r14 Section 3.7.3 (chunk 55):

    Whether the object is polymorphic is implementation-defined; if it is polymorphic, the primary purpose in being so is to allow for the use of ``dynamic_cast`` to identify whether the provided object is an instance of an implementation-defined subclass of ``std::contracts::contract_violation``.

.. warning::
   The ``std::contract_violation`` type is **implementation-defined**, which means:

   - Different standard libraries will have different layouts
   - Different standard libraries may or may not use polymorphism
   - Cross-STL usage is NOT explicitly supported
   - The specification leaves this as a QoI (Quality of Implementation) issue

Questions 3.x: C Interoperability
**********************************

**Answer: NOT addressed. C interoperability is NOT supported.**

The specification explicitly requires:

- The handler has **"C++ language linkage"** (chunk 38, chunk 83)
- It takes a ``std::contracts::contract_violation&`` which is a C++ type

.. important::
   No mention of:

   - ``extern "C"`` signatures
   - C-compatible handlers
   - Type erasure for C code
   - Accessor APIs for C

**Conclusion:** The specification does NOT provide any mechanism for C handlers. The handler signature is purely C++.

.. admonition:: Implication for ABI

   If C interoperability is desired, it would need to be an implementation-specific extension beyond the standard specification.

Questions 2.x: Cross-STL Compatibility
***************************************

**Answer: NOT directly addressed in the specification.**

The specification:

1. Defines ``std::contracts::contract_violation`` as a type in the ``<contracts>`` header
2. States that the handler takes ``const std::contracts::contract_violation&``
3. Does **NOT** standardize the layout or ABI of ``std::contract_violation``
4. Acknowledges that different implementations exist (libc++ vs others)

.. admonition:: Implication for ABI

   Since the specification does not standardize the ``std::contract_violation`` layout:

   - Each standard library implementation can define its own layout
   - Cross-STL usage would require the ABI to handle different layouts
   - The Itanium ABI specification must decide whether to:

     a. Standardize the layout (going beyond the C++ standard)
     b. Keep layouts implementation-specific and prevent cross-STL mixing
     c. Use an abstraction layer (like descriptors) to isolate implementations

Additional Critical Findings
*****************************

Namespace: std::contracts (NOT just std)
=========================================

From p2900r14 (chunks 53 and 94):

    Everything in this header is declared in namespace ``std::contracts`` rather than namespace ``std``.

The full qualified name is: ``std::contracts::contract_violation``

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

This allows user handlers to:

.. code-block:: cpp

   void handle_contract_violation(const std::contracts::contract_violation& v) {
       log_violation(v);
       // Delegate to default handler
       std::contracts::invoke_default_contract_violation_handler(v);
   }

Summary Table
*************

.. list-table::
   :header-rows: 1
   :widths: 30 50 20

   * - Question
     - Answer
     - Source Chunks
   * - Q1.1-1.3: How to provide handlers?
     - Users define ``void handle_contract_violation(const std::contracts::contract_violation&)`` with C++ linkage, attached to global module. Replacement is implementation-defined.
     - 38, 39, 68, 69
   * - Q6.1: Multiple signatures like main()?
     - NO. Only one signature supported. No "magic" - it's a straightforward replaceable function.
     - 38, 39, 68, 83
   * - Q6.2: Different STL implementations?
     - Acknowledged but NOT solved by the standard. **Resolved by the ABI**: ``contract_violation`` is a thin wrapper around the chain pointer; both standard libraries define identical single-pointer layouts and chain-walking accessors.
     - 57
   * - Q3.x: C interoperability?
     - NOT ADDRESSED. Handler must have C++ linkage and take C++ type.
     - 38, 83
   * - Q2.x: Cross-STL compatibility?
     - NOT ADDRESSED by the standard. **Resolved by the ABI**: both libstdc++ and libc++ define ``contract_violation`` as a single ``const __cxa_contract_data_block*`` member with chain-walking accessors.
     - 55

Key Implications for Itanium ABI Work
**************************************

Based on the specification findings:

1. **The specification does NOT define an ABI for std::contract_violation**

   - Layout is implementation-defined
   - Each standard library can define its own layout
   - **ABI resolution**: ``contract_violation`` holds a single
     ``const __cxa_contract_data_block*`` chain pointer.  Both
     standard libraries use this layout with chain-walking accessors.

2. **The specification does NOT address cross-STL usage**

   - Each implementation is expected to provide its own complete stack
   - **ABI resolution**: Cross-STL usage works because both libstdc++
     and libc++ define identical ``contract_violation`` with the same
     single-pointer layout.  Demonstrated in 6 cross-compilation
     permutations.

3. **The specification does NOT support C handlers**

   - C++ linkage and C++ types only
   - If C support is desired, it must be an extension

4. **The handler replacement mechanism is implementation-defined**

   - Platforms can choose NOT to support user handlers
   - **ABI resolution**: Weak symbol pattern.  The runtime declares
     ``handle_contract_violation`` as ``__attribute__((weak))``.  Users
     define their handler with the same signature and it overrides at
     link time.  Same mechanism as ``operator new``.

5. **No declaration is provided in standard headers**

   - Users must provide their own declaration if they want to replace the handler
   - This gives implementations flexibility in symbol resolution

6. **The handler signature is fixed**

   - No multiple signatures like ``main()``
   - No signature detection or thunking needed
   - ABI can assume a single, well-defined signature

ABI Design Choices Made
*************************

The specification leaves many ABI details to implementations.  Here are
the choices made:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Design Question
     - Choice
   * - Standardize ``contract_violation`` layout?
     - Yes: single chain pointer (``const __cxa_contract_data_block*``).
       Enables cross-STL compatibility.
   * - How the compiler calls the handler
     - Via 34 ABI entry points.  Specialized entry points encode
       kind/semantic/mode in name; ``[[noreturn]]`` on enforce variants.
   * - Cross-compiler/cross-STL support?
     - Fully supported.  Byte-compatible ABI headers in both compilers.
       Verified with 6 cross-compilation permutations.
   * - Handler replacement mandatory?
     - Supported via weak symbols (same as ``operator new``).
   * - Data format
     - Linked list of self-describing data blocks with first-found-wins
       resolution.  Override blocks prepend fields without modifying
       existing data.
   * - C interoperability
     - Not addressed (matches specification).

.. seealso::
   - :ref:`requirements` - Design Requirements
   - :ref:`rationale` - Design Rationale
