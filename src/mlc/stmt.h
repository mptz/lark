#ifndef LARK_MLC_STMT_H
#define LARK_MLC_STMT_H
/*
 * Copyright (c) 2009-2025 Michael P. Touloumtzis.
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

#include <util/symtab.h>

struct form;

enum stmt_variety {
	STMT_INVALID,
	STMT_DEF,
	STMT_ECHO,
	STMT_MARKER,
	STMT_VAL,
} __attribute__ ((packed));

enum marker_variety {
	MARKER_DISCARD,
	MARKER_INSPECT,
	MARKER_PUBLISH,
	MARKER_REQUIRE,
	MARKER_RETRACT,
	MARKER_SECTION,
} __attribute__ ((packed));

struct stmt {
	enum stmt_variety variety;
	int line0, line1;
	union {
		struct { struct form *var, *val; unsigned flags; } def;
		struct { struct form	   *val; unsigned flags; } val;
		struct { enum marker_variety variety; symbol_mt huid; } marker;
		struct form *form;
	};
};

extern struct stmt *StmtDef(struct form *var, struct form *val, unsigned flags);
extern struct stmt *StmtEcho(struct form *str);
extern struct stmt *StmtMarker(enum marker_variety variety, symbol_mt huid);
extern struct stmt *StmtVal(struct form *val, unsigned flags);

static inline struct stmt *StmtDiscard(symbol_mt huid)
	{ return StmtMarker(MARKER_DISCARD, huid); }
static inline struct stmt *StmtInspect(symbol_mt huid)
	{ return StmtMarker(MARKER_INSPECT, huid); }
static inline struct stmt *StmtPublish(symbol_mt huid)
	{ return StmtMarker(MARKER_PUBLISH, huid); }
static inline struct stmt *StmtRequire(symbol_mt huid)
	{ return StmtMarker(MARKER_REQUIRE, huid); }
static inline struct stmt *StmtRetract(symbol_mt huid)
	{ return StmtMarker(MARKER_RETRACT, huid); }
static inline struct stmt *StmtSection(symbol_mt huid)
	{ return StmtMarker(MARKER_SECTION, huid); }

extern void stmt_free(struct stmt *stmt);
extern int stmt_eval(const struct stmt *stmt);

#endif /* LARK_MLC_STMT_H */
