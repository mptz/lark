|* Integer division
	LDI.w	W6, ' '
	LDI.w	W7, '\n'

m4_define(`header',
`	LDLs	RA, "$1:\n"
	PRINTs	RA
')
m4_define(`testcase',
`	LDLz	R0, $1
	LDLz	R1, $2
	DIVTz	R0, R1
	PRINTz	R0
	PRN.c	W7
')

header(Divisions with one cases)
testcase(+0, +1)
testcase(+1, +1)
testcase(+2, +1)
testcase(-1, +1)
testcase(-2, +1)
testcase(+0, -1)
testcase(+1, -1)
testcase(+2, -1)
testcase(-1, -1)
testcase(-2, -1)

header(Divisions yielding one cases)
testcase(+2, +2)
testcase(+2, -2)
testcase(-2, +2)
testcase(-2, -2)

header(Regression test cases)

	HALT
