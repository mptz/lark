Multiple-Value Sharing Lambda Calculator
========================================

Whereas the calculator in 'slc' is a reference implementation of the SCAM
abstract machine varying only by bugfixes and compatible optimizations,
this calculator departs further in the direction of a practical programming
language and makes extensive changes to the SCAM abstract machine.  In fact
the purpose of this prototype was to explore adding production language
capabilities to SCAM-style reduction engine.  The resulting reduction is
extensively modified, though it still has the recognizable alternation of
right-to-left then left-to-right reduction passes.

In addition to core reduction changes, MLC also adds other production
features such as namespaces, a dependency-based library system which is
not based on textual inclusion, updated garbage collection, and more.
This document describes the various areas of new & improved functionality.

MLC Advances Vs SLC
===================

Multiple Arguments (Arity)
--------------------------

Perhaps the most significant change, and the reason for the 'M' in
the name, is the addition of arity to functions.  In Lambda Calculus,
all functions are unary (single-valued) and multivalued functions
are simulated using currying.  This provides a simple, clean setting
with maximum function composability and a simple categorical semantics.
Most practical pure-functional programming languages also support a native
n-tuple construct, allowing multivalued functions to also be simulated
using a single n-tuple argument.  Language implementation dictates
whether currying or tupling is the preferred/more-efficient approach.
In both cases, heap allocation is required for the general case but
stack allocation (or register allocation) can be viable for values which
a compiler can confirm are non-escaping.

MLC sacrifices the universal composability of unary functions in order
to access the potential performance improvements of n-ary functions,
especially potential future access to register-based calling and return
conventions.  Although MLC is an interpreted language, I'd confirmed that
I wanted to explore native n-ary functions so used MLC as a proving ground
for native arity in SCAM-derived, fully normalizing reduction engine.

MLC doesn't include n-ary return values--functions still evaluate to
single values.  Native n-ary returns are a syntactic can of worms, but
more importantly in this instance, they introduce many possibilities for
code errors when not coupled with a type system--which MLC lacks.  Even
though n-ary functions aren't implememented, MLC's arity implementation
validates the flexibiilty of our reduction strategy which we'll need to
add them to a later language.

Cell Data Structures
--------------------

The addition of arity requried an internal transition from pairs to
n-ary cells.  In addition to using these cells to represent application
and abstraction nodes, we also expose them directly as data structures.
The structure of cells is self-evaluating (evaluating a cell yields an
identically sized cell with evaluated contents); cells serve as a useful
compound data structure which avoids the need to encode composite data
structures via abstraction.

Cells can be used to represent linked lists (using 2-cells as pairs and
0-cells as nil) but of course can represent trees and many other data
structures as well.

Let Expressions
---------------

MLC adds a native 'let' expression allowing variable bindings without
abstraction.  This is a syntactic convenience and aids performance.
Let expressions are not syntactic sugar (they are not translated into
abstractions + applications) but are natively supported in reduction.

Native Recursion & Conditionals
-------------------------------

MLC, though untyped, is a stepping stone towards a typed language.
In typed Lambda Calculi, self-application cannot be typed, meaning
that recursion can't be implemented via the Y combinator or equivalent.
In preparation for typing MLC adds a native recursion mechanism, which can
be freely used in MLC to write nonterminating terms, but which in a typed
language will presumably be gated behind a termination checking algorithm.

Native recursion relies on arity: we implement recursion by always
providing the abtraction itself as the 0th variable binding during
beta reduction.  A syntax update allows a specified name, e.g. 'self',
to reference the corresponding 0th bound variable.

Coupled with native recursion is a mechanism for branching (conditional
expressions) which only evaluates the chosen branch.  Since MLC uses
strict (applicative order) reduction rather than lazy reduction, having
such a control structure spares us the need to wrap recursive cases in
administrative abstractions to avoid endless reduction.  Conditional
expressions test a number for zero/nonzero; their implementation is
similar to that of abstractions in that evaluation of their bodies is
performed during left-to-right reduction (and not performed at all by
surface reduction) but differs in that the bodies of a test term (the
consequent and alternative) are at the same abstraction depth as the
test term itself, since tests are not binding expressions.

Atomic Values & Primitive Operations
------------------------------------

While it's possible to represent arbitrary data numbers, strings, etc as
well as the operations on them by encoding those values and operations
in the pure Lambda Calculus (e.g. Church numerals), it's much more
performant and practical to be able to use "real" atomic values such as
numbers and strings.

MLC adds (floating point) numbers, strings, and self-evaluating symbols
as well as some primitive arithmetic operations (addition, subtraction,
etc; string concatenation).  Primitives allow type testing as well.
We've made no effort to provide a complete set of operations; this is
a proof-of-concept.

In addition to primitives on atomics, we support primitive operations on
cells, including construction, searching, concatenation, and determining
size (number of elements).

Heap & Garbage Collection
-------------------------

Previous prototype lambda calculators (LC, SLC) managed fixed-size
heaps of fixed-size nodes.  MLC adds arity, so reduction nodes are
variable-sized.  For this reason we use the C heap (malloc/free) for
nodes, but manage allocation and garbage collection centrally.  As with
SLC, garbage collection is integrated into the reduction algorithm; our
reduction strategy and absence of mutation avoids creation of cycles,
allowing us to use reference-counted garbage collection without cycle
detection.

Although SLC performs early frees in some special cases during
right-to-left traversal, it's handicapped by the fact that SCAM
garbage collection only takes place during left-to-right traversal,
after surface reduction is complete.  This limits the amount of work a
reduction can perform.

MLC, in contrast, can collect arbitrary garbage during right-to-left
traversal--we track heap utilization with a crude adaptive algorithm
and initiate garbage collection shortly after the heap crosses a
usage threshold.  Periodically updated thresholds provide hysteresis.
Garbage collection operates as in SLC--we walk left-to-right from the
current position, freeing objects with zero reference counts as we go.
When we encounter the rightmost node at the current abstraction depth,
we continue garbage collection one layer up (i.e. in the surrounding
abstraction).  Collection terminates at the rightmost node in the
outermost abstraction layer (at depth 0).

This approach to GC allows us to run lengthy calculations with many
GC cycles before completion.  As is typical in pure-functional, Lambda
Calculus based programming languages, most nodes are collected at each
cycle--allocation rates are high and most nodes are collected young.

Node Listings And Debugging Features
------------------------------------

As we add more types of nodes and reduction functionality, the
ability to debug at the node level rises in importance.  MLC adds
a formatted-listing option for nodes with indentation and improved
cross-references.  Whereas SLC used a hash table of pointers to symbols
to provide cross-references, MLC uses a pointer arithmetic approach
(pointer difference from baseline, encoded in alphanumeric base 62)
which doesn't require memory allocation, reducing overhead for large
code bases.  MLC also improves tracing features for debugging reduction,
unflattening, and other complex operations.

What's In
---------
Updated syntax with ';'-based application and "[]"-style abstractions.
Placeholder symbols i.e. '_' arguments.
Notebook syntax option (comments by default, demarcated code).
Libraries not files as a compilation unit.
Semantic 'require' rather than textual #include.
Namespaces based on HUIDs.
Less-redundant lifting/on-demand global variable expansion (opaque vars).

What's Coming
-------------

What's Out
----------
Keyword arities (arguments distinguished by keywords not position).
Return arity (functions evaluate to multiple values).
Type checking, dependent types: This calculus is untyped.
