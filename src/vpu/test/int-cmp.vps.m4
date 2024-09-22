|* Integer comparisons
	LDI.w	W6, ' '
	LDI.w	W7, '\n'

m4_define(`header',
`	LDLs	RA, "$1:\n"
	PRINTs	RA
')
m4_define(`testcase',
`	LDLz	R0, $1
	MOV	R1, R0
	INCz	R1
	DECz	R1		|* bypass literal pooling
	CMPz	R0, R1
	PRINTrr
	PRN.c	W6
	LDLz	R2, $2
	CMPz	R0, R2
	PRINTrr
	PRN.c	W7
')

header(Comparison zero cases)
testcase(+0, +0)
testcase(+0, +1)
testcase(+0, -1)
testcase(+1, +0)
testcase(-1, +0)

header(Comparison same-sign cases)
testcase(+1, +2)
testcase(+2, +1)
testcase(-1, -2)
testcase(-2, -1)

header(Comparison opposite-sign cases)
testcase(-1, +1)
testcase(+1, -1)

header(Regression test cases)

	HALT
