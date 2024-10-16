Multiple-Value Sharing Lambda Calculator
========================================

Whereas the calculator in 'slc' is a reference implementation of the SCAM
abstract machine varying only by bugfixes and compatible optimizations,
this calculator departs further in the direction of a practical programming
language and makes extensive changes to the SCAM abstract machine.

What's In
---------
Updated syntax with ';'-based application and "[]"-style abstractions.
Placeholder symbols i.e. '_' arguments.
malloc/free-based heap to handle variable-sized nodes.
Call arity (native rather than curried multi-argument functions).
Indented & more-readable node listings.
Atomic numbers and arithmetic operations on them.
Native numeric test operations.
Native n-ary cells (2-cells are pairs, 0-cells are nil).
Native recursion.
Native strings.
Let expressions.
Reactive garbage collection under heap pressure (with hysteresis).

What's Coming
-------------
Less-redundant lifting/on-demand global variable expansion (opaque vars).

What's Out
----------
Keyword arities (arguments distinguished by keywords not position).
Return arity (functions evaluate to multiple values).
Type checking, dependent types: This calculus is untyped.
