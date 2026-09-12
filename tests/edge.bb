; edge cases
Print -7 / 2
Print -7 Mod 3
Print 7 Mod -3
Print 2147483647 + 1
Print 10 / 4 * 4
Print 10.0 / 4 * 4
Print "a" + 1 + 2
Print 1 + 2 + "a"
If "abc" <= "abc" Then Print "le ok"
If "abd" >= "abc" Then Print "ge ok"
If "b" <> "a" Then Print "ne ok"
x# = 2.5
If x Then Print "float cond true"
y# = 0.0
If y Then Print "no" Else Print "float cond false"
z = 0
If z = 0 And z < 1 And z > -1 Then Print "chained and"
If (z Or 5) Then Print "or value"
Print (3 > 2) * 10
Print (3 > 2) + (2 > 1)
For i = 1 To 3
	For j = 1 To 3
		If j = 2 Then Exit
		Write i + "" + j + " "
	Next
Next
Print ""
; loop bound re-evaluated each iteration (Blitz semantics)
n = 3
c = 0
For i = 1 To n
	If i = 1 Then n = 5
	c = c + 1
Next
Print "iterations: " + c
; nested function calls with many args
Function Six(a, b, c, d, e, f)
	Return a + b * 10 + c * 100 + d * 1000 + e * 10000 + f * 100000
End Function
Print Six(1, 2, 3, 4, 5, 6)
Print Six(Six(1,0,0,0,0,0), Six(2,0,0,0,0,0), 0, 0, 0, Six(0,0,0,0,0,0)+1)
; string functions with ints
Print Len(12345)
Print Mid(12345, 2, 2)
Print Int("42abc") + Int("x")
Print Float("3.5x")
Print Str(3) + Str(2.0)
Print Hex(-1)
; Select with strings and expressions
w$ = "two"
Select w
	Case "one": Print 1
	Case "two", "deux": Print 2
	Default: Print 0
End Select
Select 5 * 2
	Case 10: Print "ten"
End Select
; Not / Xor on comparisons
Print Not 5
Print Not 0
Print (1 = 1) Xor (2 = 2)
; local vs global
Global gv = 1
Function T()
	Local gv = 99
	Return gv
End Function
Print T() + " " + gv
; Exit from For Each
Type Q
	Field id
End Type
For i = 1 To 5
	q.Q = New Q
	q\id = i
Next
For q.Q = Each Q
	If q\id = 3 Then Exit
	Write q\id + " "
Next
Print "exit at " + q\id
; Goto out of a loop
For i = 1 To 100
	If i = 4 Then Goto out
Next
.out
Print "out at " + i
; Return in nested loops inside function
Function Find(v)
	For i = 1 To 10
		For j = 1 To 10
			If i * j = v Then Return i * 100 + j
		Next
	Next
	Return -1
End Function
Print Find(42)
Print Find(101)
; While with complex conditions and side effects
Global cnt = 0
Function Tick()
	cnt = cnt + 1
	Return cnt
End Function
While Tick() < 5 And cnt < 100
Wend
Print "cnt=" + cnt
; Repeat Until with float
f# = 0
Repeat
	f = f + 0.5
Until f >= 2
Print f
; Data with Restore to label mid-way
Restore d2
Read a$: Print a
Data "first"
.d2
Data "second"
; negative step float
For f# = 1 To 0 Step -0.5
	Write f + " "
Next
Print ""
