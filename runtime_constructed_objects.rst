.. _runtime_constructed_objects:

=====================================
Runtime-Constructed Ephemeral Objects
=====================================


.. contents::
  :depth: 1
  :local:


Overview
========

This document analyzes an alternative ABI design where **the compiler emits construction thunks** that directly build
``std::contract_violation`` objects on the stack and invoke the user's violation handler.

**There is no runtime entrypoint. There are no descriptors.**

Instead, the compiler generates per-contract construction code that knows its ABI version and constructs the
appropriate object layout, then directly calls the user's handler.



Core Concept
============

Key Insights
------------

1. **Ephemeral objects**: Since ``std::contract_violation`` objects exist only during handler invocation on the stack, they don't require persistent storage. However, the object layout remains fixed per version, so omitted fields still occupy stack space (initialized to defaults)

2. **Compiler-generated construction**: The compiler can emit code that constructs the object with the correct layout for its ABI version

3. **No runtime intermediary**: The thunk calls ``handle_contract_violation()`` directly, no parsing needed

4. **Version coordination**: All parties must agree on what each version number means (which fields, in what order)

The Proposal
------------

Instead of:
- Compiler → Descriptor + Data → Entrypoint → Parse → Construct → Handler

This approach:
- Compiler → **Thunk** → Construct on stack → Handler (directly)

**Benefits:**
- No descriptors.
- No runtime entrypoint to specify
- No parsing overhead

**Trade-offs:**
- Version coordination required (governance)
- More code per contract (thunk per site)
- Cannot share construction logic

ABI Surface
===========

No Runtime Entrypoint Function
-------------------------------

**There is no ``__cxa_contract_violation_entrypoint``.**

The only ABI surface is the handler interface:

.. code-block:: cpp

    namespace std::contracts {
        // User provides their own definition of this function
        // (or uses default provided by runtime)
        void handle_contract_violation(const contract_violation& cv);
    }

Version Coordination Protocol
------------------------------

All compilers must agree on version meanings:

.. code-block:: cpp

    // Version 1 (C++26): location + text + kind + mode + semantic
    struct __contract_violation_layout_v1 {
        uint8_t __abi_version;  // = 1
        const __cxa_source_location* location;
        const char* source_text;
        uint8_t assertion_kind;
        __cxa_detection_mode_t mode;
        __cxa_evaluation_semantic_t semantic;
    };
    // sizeof: ~32 bytes (platform-dependent)

    // Version 2 (C++29): adds label field
    struct __contract_violation_layout_v2 {
        uint8_t __abi_version;  // = 2
        const __cxa_source_location* location;
        const char* source_text;
        uint8_t assertion_kind;
        const char* label;      // ← NEW
        __cxa_detection_mode_t mode;
        __cxa_evaluation_semantic_t semantic;
    };
    // sizeof: ~40 bytes

**Critical requirement:** GCC, Clang, MSVC must all agree:
- Version 1 = these 6 fields in this order
- Version 2 = these 7 fields in this order
- New fields require new version (coordinated increment)

**Important ABI Constraint: Prefix Compatibility**
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Even with the incrementing version number, **the prefix of each version's layout can never change**. This is because old runtimes must be able to process object files emitted by newer compilers.

Consider this scenario:

.. code-block:: text

    Library A (compiled with GCC 14, 2024):
        - Runtime expects: __abi_version at offset 0
        - Reads version tag to dispatch to appropriate accessor logic

    Application (compiled with GCC 16, 2026):
        - Emits v2 objects with version tag at offset 0
        - Old runtime from Library A loads this object

**If version 2 changed the location of ``__abi_version`` (e.g., moved it to offset 8), the old runtime would:**

1. Read garbage from offset 0 (expecting version tag)
2. Misinterpret the version number
3. Access fields at wrong offsets
4. **Crash or invoke undefined behavior**

Therefore, **all future versions must maintain:**

.. code-block:: cpp

    // Version N (any future version)
    struct __contract_violation_layout_vN {
        uint8_t __abi_version;  // ← MUST always be at offset 0
        // ... all fields from vN-1 must remain at same offsets
        // New fields can ONLY be appended to the end
    };

This represents an important constraint of this approach:

- The position of ``__abi_version`` is permanently fixed at offset 0
- Any fields that appear in version 1 establish precedents that constrain all future versions
- This inflexibility compounds over time as more versions are added
- Unlike descriptors, which only coordinate on field type IDs (not layout), this approach must coordinate on exact byte-level layout

**Example of problematic evolution:**

.. code-block:: cpp

    // Version 1: establishes precedent
    struct __contract_violation_layout_v1 {
        uint8_t __abi_version;                  // offset 0 (fixed forever)
        const __cxa_source_location* location;  // offset 8 (de facto fixed)
        // ...
    };

    // Version 3: wants to add a field BEFORE location
    struct __contract_violation_layout_v3 {
        uint8_t __abi_version;                  // offset 0 (required)
        uint16_t new_flags;                     // offset 1 (NEW)
        const __cxa_source_location* location;  // ← Now at offset 16! (PROBLEM!)
        // ...
    };

This version 3 layout is **problematic** because old runtimes that recognize v3's version tag would still need to understand the new layout. While technically the version tag allows dispatching to version-specific code, the practical reality is that:

1. **Old runtimes cannot process new versions they don't recognize** - they must terminate or ignore the violation
2. **Field ordering becomes ossified** - once ``location`` appears at offset 8 in v1, all subsequent versions that include ``location`` face pressure to maintain similar layouts for conceptual consistency
3. **Layout variations explode** - different field orderings for different subsets multiply the number of distinct versions needed

The descriptor approach avoids this problem entirely by never fixing field positions—only field type identifiers require coordination.

Code Generation
===============

Version 1 Example (C++26)
--------------------------

.. code-block:: cpp

    // Source
    void withdraw(int amount)
        pre(amount > 0)
    {
        balance -= amount;
    }

**Compiler generates:**

.. code-block:: cpp

    // Static data
    static const __cxa_source_location __loc_withdraw_pre1 = {
        .file_name = "bank.cpp",
        .function_name = "withdraw",
        .line = 42,
        .column = 8
    };
    static const char __text_withdraw_pre1[] = "amount > 0";

    // Construction thunk (compiler-generated, per-contract)
    [[noreturn]]
    static void __contract_thunk_withdraw_pre1_v1() {
        // Stack-allocate v1 object
        struct __contract_violation_layout_v1 {
            uint8_t __abi_version;
            const __cxa_source_location* location;
            const char* source_text;
            uint8_t assertion_kind;
            __cxa_detection_mode_t mode;
            __cxa_evaluation_semantic_t semantic;
        } cv;

        // Populate fields (compiler knows v1 layout)
        cv.__abi_version = 1;
        cv.location = &__loc_withdraw_pre1;
        cv.source_text = __text_withdraw_pre1;
        cv.assertion_kind = 0x01;  // precondition
        cv.mode = predicate_false;
        cv.semantic = enforced;

        // Call handler directly
        std::contracts::handle_contract_violation(
            reinterpret_cast<const std::contract_violation&>(cv));

        std::terminate();
    }

    // User function
    void withdraw(int amount) {
        if (!(amount > 0)) {
            __contract_thunk_withdraw_pre1_v1();  // Call thunk
        }
        balance -= amount;
    }

**Generated Assembly (x86-64):**

.. code-block:: asm

    withdraw:
        cmp     edi, 0
        jg      .L_passed
    .L_failed:
        call    __contract_thunk_withdraw_pre1_v1  # Just call thunk
        # Never returns

    .L_passed:
        # ... function body ...
        ret

    # Construction thunk
    __contract_thunk_withdraw_pre1_v1:
        sub     rsp, 32              # Allocate stack (32-byte v1 object)

        # Store version tag
        mov     byte ptr [rsp], 1    # version = 1

        # Store location pointer
        lea     rax, [rip + __loc_withdraw_pre1]
        mov     [rsp+8], rax

        # Store text pointer  
        lea     rax, [rip + __text_withdraw_pre1]
        mov     [rsp+16], rax

        # Store kind
        mov     byte ptr [rsp+24], 0x01

        # Store mode
        mov     byte ptr [rsp+25], 0x00  # predicate_false

        # Store semantic
        mov     byte ptr [rsp+26], 0x00  # enforced

        # Call handler with stack pointer
        mov     rdi, rsp
        call    std::contracts::handle_contract_violation

        # Should never return
        call    std::terminate
        ud2

    .rodata:
    __loc_withdraw_pre1:
        .quad   .L_file
        .quad   .L_func
        .long   42
        .long   8

    __text_withdraw_pre1:
        .asciz  "amount > 0"

**Code size observations:**

Each contract requires:
- A thunk function (construction code with embedded addresses)
- A call site (invocation)
- Static data (location, text strings)

The thunk function cannot be shared across contracts since each embeds unique data addresses, leading to code duplication.

Version 2 Example (C++29 with Labels)
--------------------------------------

.. code-block:: cpp

    // Source
    void api_call(int x)
        pre [[label("public_api")]](x > 0);

**Compiler generates v2 thunk:**

.. code-block:: cpp

    static const char __label_api_call_pre1[] = "public_api";

    [[noreturn]]
    static void __contract_thunk_api_call_pre1_v2() {
        // Stack-allocate v2 object (larger)
        struct __contract_violation_layout_v2 {
            uint8_t __abi_version;
            const __cxa_source_location* location;
            const char* source_text;
            uint8_t assertion_kind;
            const char* label;  // ← NEW
            __cxa_detection_mode_t mode;
            __cxa_evaluation_semantic_t semantic;
        } cv;

        // Populate v2 fields
        cv.__abi_version = 2;
        cv.location = &__loc_api_call_pre1;
        cv.source_text = __text_api_call_pre1;
        cv.assertion_kind = 0x01;
        cv.label = __label_api_call_pre1;  // ← NEW
        cv.mode = predicate_false;
        cv.semantic = enforced;

        // Handler receives v2 object
        std::contracts::handle_contract_violation(
            reinterpret_cast<const std::contract_violation&>(cv));

        std::terminate();
    }

**Object size: 40 bytes (v2) vs 32 bytes (v1)**

Doesn't matter - ephemeral, cold path only.

std::contract_violation Implementation
=======================================

Version Dispatch in Accessors
------------------------------

**Two-Format Principle**

Due to prefix compatibility, each accessor only needs to handle two cases:

1. **Version < Y** (where Y is the version that introduced field X): Field doesn't exist, return default value
2. **Version >= Y**: Field exists at fixed offset (same location in all versions >= Y)

This means accessors are **O(1)** complexity, not O(versions).

**Why this works:** Prefix compatibility guarantees that once a field appears at offset N in version Y,
it remains at offset N in all future versions (Y+1, Y+2, ...). New fields can only be appended,
never inserted or reordered.

.. code-block:: cpp

    namespace std::contracts {

    class contract_violation {
    public:
        string_view label() const {
            // Read version tag from object
            auto* base = reinterpret_cast<const uint8_t*>(this);
            uint8_t version = *base;

            // Two-format principle: label introduced in v2
            // Case 1: version < 2 → doesn't exist
            // Case 2: version >= 2 → exists at fixed offset
            if (version < 2) {
                return "";  // v1 has no label
            }

            // All versions >= 2 have label at same offset (prefix compatibility)
            auto* v2 = reinterpret_cast<
                const __contract_violation_layout_v2*>(this);
            return v2->label ? string_view(v2->label) : "";
        }

        // Other accessors follow same pattern
        source_location location() const {
            uint8_t version = *reinterpret_cast<const uint8_t*>(this);

            // location exists in all versions (v1+), always at same offset
            auto* v1 = reinterpret_cast<
                const __contract_violation_layout_v1*>(this);
            return /* construct from v1->location */;
        }
    };

    }  // namespace std::contracts

**Cost:** Each accessor performs simple version comparison (O(1)).

Mixed Binaries
--------------

.. code-block:: text

    Library A (GCC 14, 2024):
        - Emits v1 thunks
        - Constructs 32-byte v1 objects

    Application (GCC 16, 2026):
        - Emits v2 thunks  
        - Constructs 40-byte v2 objects

    User's handler:
        void my_handler(const contract_violation& cv) {
            // cv.label() checks version tag internally
            // Returns "" for v1, actual label for v2
        }

**This works!** Version tag embedded in object enables dispatch.

Advantages
==========

1. No Descriptors
-----------------

Eliminates descriptor table metadata entirely.

**Trade-off:** Replaced with per-contract thunk functions that perform construction inline.

2. No Runtime Parsing
----------------------

Eliminates descriptor parsing on contract failure path.

**Significance:** Contract failure is already expensive (handler execution, logging, termination), so parsing overhead is negligible in context.

3. Simpler ABI Specification
-----------------------------

**No entrypoint function to specify.**

ABI only needs to specify:
- Version number meanings (v1 = these fields, v2 = these fields)
- Handler interface (already in std)

The specification is conceptually simpler since it avoids defining descriptor formats and parsing logic.

4. Direct Handler Invocation
-----------------------------

.. code-block:: text

    Descriptors: User code → Entrypoint → Construct → Handler
    This:        User code → Thunk → Construct → Handler

One fewer function call.

5. Constrained Layout Control
------------------------------

Compiler controls layout within version constraints:
- **Cannot reorder fields** (prefix compatibility requirement)
- Can add padding if needed for alignment
- **Cannot move version tag** (must remain at offset 0)

While the compiler generates the construction code, the layout is tightly constrained by prefix compatibility requirements to ensure old runtimes can process new object files

Trade-offs and Challenges
=========================

1. **Larger Code Size Per Contract**
-------------------------------------

Each contract requires a unique thunk function that cannot be shared.

**Impact on large codebases:**

The per-contract overhead multiplies linearly with the number of contracts. Since each thunk embeds unique addresses and cannot be deduplicated by the linker, codebases with many contracts will experience significant code size growth compared to the descriptor approach where construction logic is shared.

This affects:
- Binary size (larger executables)
- Instruction cache behavior (more code to cache)
- Memory pages (more pages needed)
- Embedded systems (limited storage)

2. **Version Coordination Requirements**
-----------------------------------------

**Who decides version numbers?**

.. code-block:: text

    Scenario: GCC and Clang both want to extend in 2025

    GCC 15 (May 2025): Wants to add "optimization_hint"
    Clang 18 (Sept 2025): Wants to add "source_range"

    Problem: Both want version 2, but different fields!

    Solution options:
    A. Pre-coordinate through consortium
       - GCC gets v2, Clang gets v3
       - Requires governance body
       - Slows feature development

    B. Vendor-specific versions
       - Embed vendor ID: v2_gcc vs v2_clang
       - Accessors need vendor × version matrix
       - O(vendors × versions) complexity

**Alternative with descriptors:** Field-level versioning enables independent vendor extensions without coordination.

3. Fixed Field Layout Constraints
----------------------------------

With ``-fno-contract-source-text``, object still has source_text field:

.. code-block:: cpp

    struct __contract_violation_layout_v1 {
        uint8_t __abi_version;
        const __cxa_source_location* location;
        const char* source_text;  // ← Set to nullptr, occupies 8 bytes
        uint8_t assertion_kind;
        __cxa_detection_mode_t mode;
        __cxa_evaluation_semantic_t semantic;
    };

**Object size remains fixed** without creating new version.

**Potential version proliferation:**

- v1: loc + text + kind
- v2: loc + text + kind + label
- v3: loc + kind (no text) ← New version needed
- v4: loc + kind + label (no text) ← Another version needed
- v5: text + kind (no loc) ← Yet another needed

For N optional fields, this could theoretically require up to **2^N versions** if all combinations are supported.

**Alternative with descriptors:** Fields can be truly omitted (0 bytes) without version changes.

4. No Linker Deduplication
---------------------------

Each thunk is unique (different data pointers):

.. code-block:: cpp

    // Contract A thunk
    cv.location = &__loc_A;  // Unique
    cv.source_text = __text_A;  // Unique

    // Contract B thunk  
    cv.location = &__loc_B;  // Different
    cv.source_text = __text_B;  // Different

    // Linker cannot deduplicate thunks

**Alternative with descriptors:** Shared descriptor table enables linker deduplication.

5. Accessor Implementation Complexity
--------------------------------------

Due to prefix compatibility, accessors use simple version comparison (O(1)):

.. code-block:: cpp

    string_view label() const {
        uint8_t version = *reinterpret_cast<const uint8_t*>(this);
        // Two cases: version < 2 (doesn't exist) or >= 2 (exists)
        if (version < 2) return "";
        return reinterpret_cast<const __contract_violation_layout_v2*>(this)->label;
    }

**Complexity remains O(1)** regardless of how many versions exist, because:
- Fields introduced in version Y remain at the same offset in all versions >= Y
- Each accessor only checks: "Does this version have my field?" (version >= Y)

**Alternative with descriptors:** Single parsing loop, also O(1) per field access.

6. Trivially Destructible Requirement
--------------------------------------

Stack-allocated object destroyed after handler:

.. code-block:: cpp

    void __contract_thunk_v1() {
        __contract_violation_layout_v1 cv;  // Stack
        // ... construct ...
        handler(cv);
        // cv destroyed here
        std::terminate();
    }

**Future versions must remain trivially destructible** due to stack allocation model.

**Alternative with descriptors:** ``std::contract_violation`` controls lifetime, enabling destructors if needed.

7. Exception Handling Same as Descriptors
------------------------------------------

Exception remains active during thunk:

.. code-block:: cpp

    void __contract_thunk_v1() {
        // Exception still active
        __contract_violation_layout_v1 cv;
        handler(cv);
    }

User accesses via ``std::current_exception()``.

**Same solution as descriptors.**

8. Vendor Extension Conflicts
------------------------------

Vendors need separate version namespaces:

.. code-block:: cpp

    // Embed vendor in version
    struct __contract_violation_layout_v1 {
        uint16_t __version;  // high byte: vendor, low byte: version
        // ...
    };

    // GCC v2 = 0x0102
    // Clang v2 = 0x0202

    // Accessors need nested switches
    switch (vendor) {
        case GCC:
            switch (version) { case 2: /* gcc v2 */ }
        case CLANG:
            switch (version) { case 2: /* clang v2 */ }
    }

**Maintenance burden: O(vendors × versions)**

While runtime complexity per accessor remains O(1) due to prefix compatibility (each accessor only checks "version >= X" within vendor namespace), the maintenance and specification burden grows with vendors × versions as each vendor-version combination must be explicitly documented.

**Alternative with descriptors:** Vendor field types (0x40-0xFF) enable independent extensions without conflicts.

Binary Size Comparison
======================

Conceptual Analysis
-------------------

**Runtime-Thunks approach:**

=================== ========================================================
Component           Size Impact
=================== ========================================================
Thunk code          One construction function per contract (cannot share)
Call sites          Simple call instruction
Static data         Location info and strings (similar to descriptors)
Descriptor tables   None
=================== ========================================================

**Descriptor approach:**

=================== ========================================================
Component           Size Impact
=================== ========================================================
Thunk code          None (shared entrypoint handles all contracts)
Entrypoint code     Single shared construction function
Call sites          Call with descriptor + data arguments
Static data         Location info and strings
Descriptor tables   Compact metadata describing field layout
=================== ========================================================

**Key difference:** Runtime-Thunks trades descriptor metadata for per-contract thunk functions.

**Field omission impact:** When compiler flags omit optional fields (e.g., ``-fno-contract-source-text``):
- **Descriptors:** Can truly omit the field (saves rodata space for the pointer, the string data, and the metadata entry)
- **Runtime-Thunks:** Object layout remains fixed; field set to nullptr (saves rodata space for the string data but still requires stack space for the nullptr pointer and initialization code to set it)

Feature Comparison
==================

============================= =========== ==================
Feature                       Descriptors Runtime-Thunks
============================= =========== ==================
ABI stable evolution          Yes         Yes (with coordination)
Field omission                True omission  Fixed layout
Vendor extensions             Isolated    Requires vendor ID
Version coordination          Not needed  Required (governance)
Code size scaling             Shared construction  Per-contract thunks
Linker deduplication          Yes         No
Runtime parsing overhead      Minimal     None
Specification complexity      Detailed    Simpler
Exception handling            Clean       Clean (same)
Destructor support            Full        Must be trivial
Accessor complexity           O(1)        O(1) per accessor
============================= =========== ==================

Analysis Summary
================

**Technically Viable Alternative**

The runtime-constructed thunk approach is **technically viable** and offers genuine advantages in specification simplicity and elimination of runtime parsing. However, several important considerations emerge:

Key Considerations
------------------

1. **Code size scaling** - Each contract requires unique thunk function; linear scaling with contract count

2. **Version coordination governance** - Requires consortium to manage version namespace across vendors

3. **Fixed field layouts** - Object layout fixed per version; cannot dynamically adjust for omitted fields

4. **No linker deduplication** - Each thunk unique due to embedded addresses

When This Might Be Preferable
------------------------------

This approach could be considered if:

- **Specification simplicity is paramount** (conceptually simpler than descriptor parsing)
- **Single vendor ecosystem** (eliminates coordination problem)
- **Very small number of contracts** (minimizes thunk code duplication impact)
- **Avoiding parsing overhead is critical** (though significance is minimal)

Key Trade-Off
-------------

This approach trades:
- **Descriptor metadata** (eliminated)
- **Descriptor parsing** (eliminated)
- **Simpler specification** (no descriptor format)

For:
- **Per-contract thunk code** (cannot be shared)
- **Version governance** (new coordination requirement)
- **Fixed field layouts** (cannot omit at granular level)

**Net assessment:** Simpler specification and no parsing overhead, but with linear code size growth, governance coordination requirements, and constraints on field layout evolution.

Conclusion
==========

The runtime-constructed thunk approach is innovative and successfully eliminates descriptors and parsing overhead. The **linear code size scaling** and **cross-vendor version coordination requirements** represent significant considerations for standardization in multi-vendor environments with many contracts.

The descriptor approach offers complementary advantages:

- **Binary size efficiency** (shared construction logic across all contracts)
- **Dynamic field omission** (true field-level omission at compile time)
- **Vendor extensibility** (independent vendor extensions without coordination)
- **Linker optimization** (shared descriptor tables)
- **Long-term evolution** (no destructor restrictions)

Each approach presents distinct trade-offs. For the Itanium C++ ABI specification, the descriptor approach may be better suited for codebases with many contracts and multi-vendor interoperability requirements, while the runtime-thunk approach offers advantages where specification simplicity and elimination of parsing overhead are prioritized.
