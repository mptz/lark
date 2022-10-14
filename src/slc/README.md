Sharing Lambda Calculator
=========================

A strong (reducing under abstractions) lambda calculator with sharing,
implementing the SCAM abstract machine described in Accattoli, Condoluci,
& Sacerdoti Coen _Strong Call-by-Value is Reasonable, Implosively_ (2021).

Bug Fixes
---------

This implementation corrects several bugs present in the OCaml reference
code which accompanies the paper.  In making changes I was careful to
preserve the O(1) performance of state transitions.

* Environment copying (copy_env) doesn't alpha-convert bound variables.
  Although "crumbling variables" (sharing nodes introduced by the
  "crumbling transformation" and during reduction) are identified by
  their memory addresses and are thus automatically alpha-converted by
  copying, the same is not true for lambda binders and their bound
  variables.  This can be reproduced by attempting to evaluate
	`S (K S) K`
  where, as usual, `K := \x y. x` and `S := \x y z. (x z) (y z)`.

  This issue led to the most significant departure betwen SLC and the
  reference SCAM machine implementation: I adopted a locally-nameless
  representation using pointers (memory addresses) for free variables and
  De Bruijn indexes for bound variables.  De Bruijn indexes are naturally
  alpha-invariant, though this change required the maintenance of depth
  information in the virtual machine so as to allow index shifting
  during beta-reduction.

* The pattern match for the "ren" (rename) rule only matches targets
  with constructors **Shared** and **Var**, which prevents it from
  eliminating some administrative renames.  This causes the
  beta-reduction rules to miss some abstractions, leading to 
  unevaluted redexes in the right-to-left traversal (**eval_RL**).
  This can be reproduced by attempting to evaluate
	`(\x. x) (\y. y) v`
  which fails to reach normal form.

* Implementation of garbage collection is incomplete.  Environment
  copying doesn't set reference counts, and the garbage collection rule
  in **eval_LR** only matches abstractions; as a result non-abstractions
  are not garbage-collected.

Optimizations
-------------

SLC also implements a few optimizations not mentioned in the paper or its
reference implementation.  One of the SCAM's drawbacks is its deferral of
all garbage collection until completion of the R-to-L traversal, i.e.
until weak (not under abstractions) has completed.  I've added three
additional GC sites (in **reduce**, `reduce.c`).

* In **rule_beta_inert**, if the reference count of **y** (the sharing
  node allocated to reference the application's argument) after beta
  reduction is 0, there's no need to add **y** to the right-hand-side
  of the environment in progress; simply delete it.  This happens when
  a lambda body doesn't reference its bound variable.

- Similarly, in **rule_beta_value**, where **y** is the abstraction
  argument to the application, if **y**'s reference count after beta
  reduction is 0 we can garbage-collect its body, leaving a placeholder.
  We can't garbage-collect **y** itself since it appears at an arbitrary
  position to our right either in the current or in an enclosing
  environment.

- In **rule_beta_value**, if **x** (the function being applied) will
  become unreferenced, we destructively modify its abstraction body
  rather than copying (alpha-converting) it since we know it won't be
  referenced in future.  As in the prior optimization, we replace the
  body of **x** with a placeholder.  We still need to perform variable
  substitution and bound variable index adjustment as necessary, but
  do these in-place.  We skip this optimization when `x = y`
  (self-application) since in that case beta-reduction can create new
  references to the abstraction body, rendering destructive evaluation
  unsafe.
