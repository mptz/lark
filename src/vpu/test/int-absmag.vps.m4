|* Integer absolute-value and magnitude.  These do the same thing, except
|* that ABS returns an int and MAG returns a nat.  Both are very simple,
|* not a lot of code-path complexity to test.
	LDI.w	W7, '\n'

m4_define(`header',
`	LDLs	RA, "$1:\n"
	PRINTs	RA
')
m4_define(`abs_testcase',
`	LDLz	R0, $1
	ABSz	R0
	PRINTz	R0
	PRN.c	W7
')
m4_define(`mag_testcase',
`	LDLz	R0, $1
	MAGz	R0
	PRINTn	R0
	PRN.c	W7
')

header(Absolute value/magnitude small integers)
abs_testcase(+0)
abs_testcase(-0)
abs_testcase(+1)
abs_testcase(-1)
abs_testcase(+99)
abs_testcase(-99)
abs_testcase(+999999999)
abs_testcase(-999999999)
mag_testcase(+0)
mag_testcase(-0)
mag_testcase(+1)
mag_testcase(-1)
mag_testcase(+99)
mag_testcase(-99)
mag_testcase(+999999999)
mag_testcase(-999999999)

header(Absolute value multilimb integers)
abs_testcase(+9094113031509526179378002974120766514793942590298)
abs_testcase(-9094113031509526179378002974120766514793942590298)
abs_testcase(+254370906979396122571429894671543578468788614445)
abs_testcase(-254370906979396122571429894671543578468788614445)
abs_testcase(+78820273420922224533985626476691490556284250391)
abs_testcase(-78820273420922224533985626476691490556284250391)
abs_testcase(+35231176006651012412006597558512761785838292042)
abs_testcase(-35231176006651012412006597558512761785838292042)
mag_testcase(+9094113031509526179378002974120766514793942590298)
mag_testcase(-9094113031509526179378002974120766514793942590298)
mag_testcase(+254370906979396122571429894671543578468788614445)
mag_testcase(-254370906979396122571429894671543578468788614445)
mag_testcase(+78820273420922224533985626476691490556284250391)
mag_testcase(-78820273420922224533985626476691490556284250391)
mag_testcase(+35231176006651012412006597558512761785838292042)
mag_testcase(-35231176006651012412006597558512761785838292042)

	HALT
