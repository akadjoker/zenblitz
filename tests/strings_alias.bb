; string value semantics — aliasing check
a$ = "x" + "y"
b$ = a
a = a + "z"
Print "a=" + a + " b=" + b
c$ = ""
For i = 1 To 3
	c = c + "ab"
	d$ = c
	c = c + "!"
	Print c + " / " + d
Next
Type T
	Field s$
End Type
t.T = New T
t\s = "q" + "w"
e$ = t\s
t\s = t\s + "e"
Print t\s + " " + e
