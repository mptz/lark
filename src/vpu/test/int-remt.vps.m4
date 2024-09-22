|* Integer remainder
	LDI.w	W6, ' '
	LDI.w	W7, '\n'

m4_define(`header',
`	LDLs	RA, "$1:\n"
	PRINTs	RA
')
m4_define(`testcase',
`	LDLz	R0, $1
	LDLz	R1, $2
	REMTz	R0, R1
	PRINTz	R0
	PRN.c	W7
')

header(Remainder dividend smaller than divisor)
testcase(+0, +1)
testcase(+0, +2)
testcase(+0, -1)
testcase(+0, -2)
testcase(+1, +3)
testcase(+1, +2)
testcase(-1, +3)
testcase(-1, +2)
testcase(+1, -3)
testcase(+1, -2)
testcase(-1, -3)
testcase(-1, -2)

header(Remainder small divisors)
testcase(+55555555555555555555, +2)
testcase(-55555555555555555555, +2)
testcase(+55555555555555555555, -2)
testcase(-55555555555555555555, -2)

header(Remainder sign check)
testcase(+1000000000000000000000, +99999999999999999)
testcase(+1000000000000000000000, -99999999999999999)
testcase(-1000000000000000000000, +99999999999999999)
testcase(-1000000000000000000000, -99999999999999999)

header(Regression test cases)
	|* This case had a -0 leak.
testcase(-153090103458041951154101635597691113521505692223971916144667680177732546048659517722394381521690894717236694676152323755811205194350929240552970426007709996180286951213831417049209227037611330746974208, +1)

	HALT
