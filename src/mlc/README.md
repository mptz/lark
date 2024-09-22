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

TODO List
---------
Listing (vs printing) of test nodes.
Tests of primitive and primitives-in-tests.

What's Coming
-------------
Native recursion.
Native cons cells/arrays.
Let expressions.
Keyword arities (arguments distinguished by keywords not position).
Return arity (functions evaluate to multiple values).

What's Possible (Not In Yet)
----------------------------
Less-redundant lifting/on-demand global variable expansion.

What's Out
----------
Type checking: This calculus is untyped.
Dependent types: Ditto.
