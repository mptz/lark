#ifndef LARK_MLC_SUBST_H
#define LARK_MLC_SUBST_H
/*
 * Copyright (c) 2009-2024 Michael P. Touloumtzis.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Perform substitution of values (or rather, pointers to shared values)
 * for bound variables.  This is the core of beta-reduction and reduction
 * of other binding terms.  We combine the following operations in a
 * single linear-time pass:
 *
 * a) Substitute values from the 'vals' for corresponding bound variables
 *    in the given body (the now locally-free variables with abstraction
 *    depth 0).
 *
 * b) Shift bound variable indexes.  We downshift by -1 to reflect the
 *    abstraction elimination; however, the body we're copying may come
 *    from a lower abtraction depth than the site we're copying it to,
 *    so we also need to apply an up-shift to compensate for the increase
 *    in abstraction depth.
 *
 *    Luckily, we don't have to apply shifting to the values being
 *    substituted, since the merely point to shared values rather than
 *    containing them.  We'll need to resolve all this deferred shifting
 *    at readback time.
 *
 * c) Copy all substitution nodes along the way, since substitution
 *    'variable names' are implicit in their memory locations; thus
 *    copying in memory implicitly alpha-converts an environment of
 *    nodes.  Since nodes form a DAG, we use a pointer-forwarding copy
 *    algorithm to maintain sharing.
 *
 * The variant subst_edit is used when reducing the last copy of an
 * abstraction body (reference count has gone to 0 & absence of
 * self-application), in which case we still need to do a) and b),
 * but not c)--substitution modifies the last copy destructively since
 * it won't be used again.
 */

struct node;

struct subst {
	const struct node *vals;  /* values being substituted */
	struct node *next, *prev; /* output copy (both ends) */
	int basedepth,		  /* starting depth of beta-reduction */
	    shift;		  /* amount to shift free variables */
};

extern void subst_copy(const struct node *body, struct subst *subst);
extern void subst_edit(struct node *body, struct subst *subst);

#endif /* LARK_MLC_SUBST_H */
