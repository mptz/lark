|* Integer increment & decrement.  The most critical cases are the transitions
|* through zero in both directions.  We need to ensure that no negative zeros
|* (-0) leak out of our sign-magnitude representation.
	LDLc	RE, ' '
	LDLc	RF, '\n'

m4_define(`header',
`	LDLs	RA, "$1:\n"
	PRINTs	RA
')
m4_define(`testcase',
`	LDLz	R0, $1
	PRINTz	R0
	PRINTc	RE

	INCz	R0
	PRINTz	R0
	PRINTc	RE
	INCz	R0
	PRINTz	R0
	PRINTc	RE
	INCz	R0
	PRINTz	R0
	PRINTc	RE
	INCz	R0
	PRINTz	R0
	PRINTc	RE

	DECz	R0
	PRINTz	R0
	PRINTc	RE
	DECz	R0
	PRINTz	R0
	PRINTc	RE
	DECz	R0
	PRINTz	R0
	PRINTc	RE
	DECz	R0
	PRINTz	R0
	PRINTc	RF
')

header(Increment/decrement through zero)
testcase(-2)

header(Increment/decrement single limb)
testcase(+998)
testcase(-779)

header(Increment/decrement across 1-2 limb boundary)
testcase(+4294967294)
testcase(-4294967298)

header(Increment/decrement multilimb integers)
testcase(+9094113031509526179378002974120766514793942590298)
testcase(-254370906979396122571429894671543578468788614445)
testcase(+78820273420922224533985626476691490556284250391)
testcase(-35231176006651012412006597558512761785838292042)

	HALT
