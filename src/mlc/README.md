Multiple-Value Sharing Lambda Calculator
========================================

Whereas the calculator in 'slc' is a reference implementation of the
SCAM abstract machine with a few bugfixes and compatible optimizations,
this calculator departs further in the direction of a practical
programming language and makes extensive changes to the SCAM abstract
machine.  In fact the purpose of this prototype was to explore adding
production language capabilities to SCAM-style reduction engines.
The reduction state machine is extensively modified, though it still
has the recognizable alternation of right-to-left then left-to-right
reduction passes over flattened terms (the literature refers to these
terms as 'crumbled' but I find 'flattened' more useful).

In addition to core reduction changes, MLC adds other production
features such as namespaces, a dependency-based library system which is
not based on textual inclusion, updated garbage collection, and more.
This document describes various areas of new & improved functionality.

MLC Advances vs SLC
===================

Note than "advances," for the purpose of this document, are advances
towards my objective of a full-featured dependently-typed programming
environment suitable for production software engineering.  Advances
towards this goal may validly be perceived as retreats from a clear and
simple embodiment of an algorithm for research or pedagogical purposes.

Multiple Arguments (Arity)
--------------------------

Perhaps the most significant change, and the reason for the 'M' in
the name, is the addition of arity to functions.  In Lambda Calculus,
all functions are unary (single-valued) and multivalued functions
are simulated using currying.  This provides a simple, clean setting
with maximum function composability and a simple categorical semantics.
Most practical pure-functional programming languages also support native
n-tuples, allowing multivalued functions to alternatively be simulated
using single n-tuple arguments.  Language implementation dictates
whether currying or tupling is the preferred/more-efficient approach.
In both cases, heap allocation is required for the general case but
stack allocation (or register allocation) can be viable for values which
a compiler can confirm are non-escaping.

MLC sacrifices the universal composability of unary functions in order
to access the potential performance improvements of true n-ary functions,
especially potential future access to register-based calling and return
conventions.  Although MLC is an interpreted language, I'd confirmed that
I wanted to explore native n-ary functions so used MLC as a proving ground
for native arity in a SCAM-derived, fully normalizing reduction engine.

MLC doesn't include n-ary return values---functions still evaluate to
single values.  Native n-ary returns are a syntactic can of worms, but
more importantly in this instance, they introduce many possibilities for
code errors when not coupled with a type system---which MLC lacks.  Even
though n-ary functions aren't implememented, MLC's arity implementation
validates the flexibiilty of our reduction strategy, which we'll need to
add them to a later language.

Cell Data Structures
--------------------

The addition of arity required an internal transition from pairs to
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
standalone abstraction.  This is a syntactic convenience and aids
performance.  Let expressions are not syntactic sugar (they are not
translated into abstractions + applications) but are natively supported
in reduction.

Native Recursion & Conditionals
-------------------------------

MLC, though untyped, is a stepping stone towards a typed language.
In typed Lambda Calculi, self-application cannot be typed, meaning
that recursion can't be implemented via fixed-point combinators or
ad hoc self-application.  In preparation for typing, MLC adds a
native recursion mechanism; this can be freely used in MLC to write
nonterminating terms, but in a typed language will presumably be gated
behind a termination-checking algorithm such as enforced structural
recursion.

Native recursion relies on arity: we implement recursion by always
providing the abstraction itself as the 0th parameter during beta
reduction.  A syntax update allows a specified name, e.g. 'self', to
reference the corresponding 0th bound variable.

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
well as the operations on them by encoding those values and operations in
the pure Lambda Calculus (e.g. Church numerals), it's much more performant
and practical to be able to use "real" atomic values such as numbers
and strings.  This is a scenario in which simplicity and theoretical
elegance are nearly diametrically opposed to practical considerations
like runtime and memory usage.

MLC adds (floating point) numbers, strings, and self-evaluating symbols
as well as some primitive arithmetic operations (addition, subtraction,
etc; string concatenation).  Primitives allow type testing as well.
We've made no effort to provide a complete set of operations; this is
a proof-of-concept.

In addition to primitives on atomics, we support primitive operations on
cells, including construction, searching, concatenation, and determining
size (number of elements).

Core Syntax Updates
-------------------

MLC's addition of new native language features called for an updated
syntax for anonymous functions and n-ary cells.  Square brackets are
used to define abstractions as well as cells and tests.  In addition to
standard function application syntax, we support semicolon for postfix
function application, making chained applications easier to read.
We've also added infix binary operators with precedence and a '_'
placeholder for nameless arguments.

We also support a 'notebook' syntax in which ordinary text need not be
commented, and special comments mark lines containing code.  It's also
possible to toggle between 'listing' (ordinary) and notebook modes.

Pre-Evaluated Environment
-------------------------

In SLC, the global environment exists entirely outside of (and prior
to) evaluation.  When a term references a global constant, we wrap
that term in an abstraction which we use to substitute the constant.
If a term references N globals, we wrap it in N abstractions in order
to be able to substitute-in those N constants.  Before installing any
defined term in the environment, we apply this lifting transformation
to ensure that all terms in the environment were closed.  Reduction is
triggered only by evaluation statements, not by definitions.

In MLC, we support installing already-evaluated terms in the environment.
This allows us to perform the flattening (compilation) operation ahead
of time.  By default, definitions perform weak/"surface" reduction rather
than descend into abstractions and tests---though full normalization is
an option, as is no normalization at all.

We still support textual global values; such values are still handled
via lifting, though we now use an N-ary **let** expression rather than
N nested abstractions to perform the substitution.  Per-constant flags
in the environment control whether values are preevaluated (and just
referenced), textually substituted via lifting, or not expandable at
all---so-called "opaque" constants.

Opaque constants serve the role of free variables in MLC.  SLC supports
definition-by-mention: any variable which doesn't reference a surrounding
abstraction or global constant is assumed to be a free variable.  Since
this is error-prone in an production setting, MLC requires variables
and constants be explicitly declared before use.

Heap & Garbage Collection
-------------------------

Previous prototype lambda calculators (LC, SLC) manage fixed-size
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
traversal---we track heap utilization with a crude adaptive algorithm
and initiate garbage collection shortly after the heap crosses a
usage threshold.  Periodically updated thresholds provide hysteresis.
Garbage collection operates as in SLC---we walk left-to-right from the
current position, freeing objects with zero references as we go.  When we
encounter the rightmost node at the current abstraction depth, we continue
garbage collection one layer up (i.e. in the surrounding abstraction).
Collection terminates at the rightmost node in the outermost abstraction
layer (at depth 0).

This approach to GC allows us to run lengthy calculations with many
GC cycles before completion.  As is typical in pure-functional, Lambda
Calculus based programming languages, most nodes are collected at each
cycle---allocation rates are high and most nodes are collected young.

Controlled Unsharing Expansion
------------------------------

Expanding from a shared graph representation to a non-shared term
("unflattening" in MLC) has the potential for exponential space increase.
Worse, this scenario can easily be realized in practice when a "stuck"
(opaque or non-reducing) term creates a pile-up of shared, irreducible
references.  During development of MLC, this led to occasional crashes
during extraction as the unflattening operation consumed many gigabytes
of memory without an end in sight.

To address this issue, we added accounting to unflattening.  Superlinear
growth from nodes to terms activates "pruning" or truncation of output
terms.  We currently permit O(N log N) growth with a constant factor,
though could switch to (or provide an option for) polynomial expansion.

Node Listings And Debugging Features
------------------------------------

As we add more types of nodes and reduction functionality, the
ability to debug at the node level rises in importance.  MLC adds
a formatted-listing option for nodes with indentation and improved
cross-references.  Whereas SLC uses a hash table of pointers to symbols
to provide cross-references, MLC uses a pointer-arithmetic approach
(pointer difference from baseline, encoded in alphanumeric base 62)
which doesn't require memory allocation, reducing overhead for large
code bases.  MLC also improves tracing features for debugging reduction,
unflattening, and other complex operations.

Libraries and Namespaces
------------------------

SLC has file-level source code processing (you specify a file to the
interpreter) and file-level, textual #include statements like C.  All
symbols occupy a single global namespace.  These attributes aren't
compatible with programming in the large.

Instead of files, MLC's compilation unit is the library; a library is a
directory containing source files, all of which are processed together.
Source code is divided into sections numbered with HUIDs (which are just
a convenient format of random Universally Unique IDs); each section is
its own namespace, and constant names don't collide unless within the
same section.  Otherwise, collisions are checked at time of mention:
an ambiguous variable usage (due to that variable's being referenced in
multiple in-scope namespaces) is an error, but mere presence of the same
name in multiple in-scope namespaces is not.  Sections can be 'required'
to bring their symbols into scope.  Libraries can freely require other
sections in the same library, but from outside a library one can only
access sections which are 'published'.

Libraries themselves are identified with HUIDs---in general our emphasis
is on flat, global constructs with no hierarchy or privileged portions of
the namespace.  The trick is achieve readability under these constraints.

What's Left Out of MLC
======================

I've intentionally excluded a cluster of features which I believe need
to be co-designed with one another, and which represent such a departure
from SLC that MLC would lose its identity as an "industrial strength SLC".
These are:

1. A type system.
2. Return arity (functions evaluating to multiple values).
3. Keyword arities (arguments distinguished by keywords not position).

These are listed in descending order of significance---the best argument
for leaving out return & keyword arities is that juggling complex
function signatures becomes error-prone in the absence of type checking.

Even keyword arities, which may seem to be a simple feature on the
surface, is tightly coupled with multiple-value constructs if we insist on
a coherent semantics.  We might think we'd limit to only a single instance
of each keyword in a function's call or return arity (indicating an error
otherwise), but if we focus on the algebra of multiple-values (arities),
we want value combination to be associative---meaning we might combine
two different values lists, each of which includes a certain keyword,
and end up with two instances of that keyword.  Thus it leads to a
more robust categorical semantics of arities if we can refer to the kth
instance of a keyword---treating ordinary non-keyword arguments as the
N instances of the empty keyword.
