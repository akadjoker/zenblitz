; value semantics must hold for long strings too (>128 chars)
a$ = String("abcdefghij", 20)
a = a + "X"
b$ = a
a = a + "Y"
Print Len(a) + " " + Len(b)
Print Right(a, 3) + " " + Right(b, 3)
Type T
	Field s$
End Type
t.T = New T
t\s = String("0123456789", 15)
t\s = t\s + "A"
c$ = t\s
t\s = t\s + "B"
Print Right(t\s, 2) + " " + Right(c, 2)
