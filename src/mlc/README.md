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
all functions are unary (single-valued) and multivalued functions are
simulated using currying.  This provides a simple, clean setting with
maximum function composability and a simple categorical semantics.
Most practical pure-functional programming languages also support a
native n-tuple construct, allowing multivalued functions to also be
simulated using a single n-tuple argument.

XXX more here...

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

What's In
---------
Updated syntax with ';'-based application and "[]"-style abstractions.
Placeholder symbols i.e. '_' arguments.
Call arity (native rather than curried multi-argument functions).
Indented & more-readable node listings.
Atomic numbers and arithmetic operations on them.
Native numeric test operations.
Native n-ary cells (2-cells are pairs, 0-cells are nil).
Native recursion.
Native strings.
Self-evaluating symbols.
Let expressions.
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
