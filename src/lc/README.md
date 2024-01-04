Lambda Calculator
=================

A strong (reducing under abstractions) lambda calculator using normal-
order (leftmost outermost) reduction.  Terms are first converted to De
Bruijn indexing; beta reduction proceeds via copying with index shifting.
Position on spine is tracked via an explicit stack rather than C-language
stack or pointer reversal/zipper.  Terms are reclaimed via stop-the-world
pointer-reversing mark & sweep garbage collection.  This is intended as a
"plain vanilla" reference strong lambda calculator against which others
can be compared.
