|* Integer multiplication
	LDI.w	W6, ' '
	LDI.w	W7, '\n'

m4_define(`header',
`	LDLs	RA, "$1:\n"
	PRINTs	RA
')
m4_define(`testcase',
`	LDLz	R0, $1
	LDLz	R1, $2
	MULz	R0, R1
	PRINTz	R0
	PRN.c	W7
')

header(Multiplication by zero cases)
testcase(+0, +0)
testcase(+0, +1)
testcase(+0, -1)
testcase(+1, +0)
testcase(-1, +0)

header(Multiplication by one cases)
testcase(+1, +1)
testcase(+1, +2)
testcase(+2, +1)
testcase(+1, -1)
testcase(+1, -2)
testcase(-1, +1)
testcase(-2, +1)

header(Regression test cases)

	HALT
