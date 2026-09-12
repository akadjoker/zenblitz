; control flow
For i = 1 To 5
	Write i + " "
Next
Print ""
For i = 10 To 1 Step -3
	Write i + " "
Next
Print ""
For f# = 0 To 1 Step 0.25
	Write f + " "
Next
Print ""
i = 0
While i < 3
	Print "while " + i
	i = i + 1
Wend
Repeat
	i = i - 1
	Print "repeat " + i
Until i = 0
n = 0
Repeat
	n = n + 1
	If n = 4 Then Exit
Forever
Print "n=" + n
For i = 1 To 10
	If i Mod 2 = 0 Then Print "even " + i Else Print "odd " + i
	If i = 5 Exit
Next
x = 7
If x > 5 And x < 10 Then Print "in range"
If x < 5 Or x > 6 Then Print "or ok"
If x > 5
	Print "block if"
ElseIf x > 2
	Print "elseif"
Else
	Print "else"
EndIf
If x = 3
	Print "no"
Else If x = 7
	Print "else if 7"
End If
Select x
	Case 1, 2
		Print "one or two"
	Case 7
		Print "seven"
	Default
		Print "other"
End Select
s$ = "b"
Select s
	Case "a": Print "A"
	Case "b": Print "B"
End Select
Select 3.0
	Case 3: Print "three float"
End Select
If "abc" < "abd" Then Print "str lt"
If "abc" = "abc" Then Print "str eq"
If "b" > "a" Then Print "str gt"
If Not (1 = 2) Then Print "not ok"
a = 0: b = 0
If a = 0 Then b = 1: Print "b=" + b
Goto skip
Print "never"
.skip
Print "after goto"
Gosub sub1
Print "back from gosub"
Gosub sub2
Print "done"
End

.sub1
Print "in sub1"
Return

.sub2
Print "in sub2"
Gosub sub1
Print "sub2 after nested"
Return
