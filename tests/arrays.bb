; arrays, data, vectors, globals
Dim a(5)
For i = 0 To 5
	a(i) = i * i
Next
For i = 0 To 5
	Write a(i) + " "
Next
Print ""

Dim grid#(2, 3)
For y = 0 To 3
	For x = 0 To 2
		grid(x, y) = x + y * 0.5
	Next
Next
Print grid(2, 3) + " " + grid(1, 1)

Dim names$(2)
names(0) = "a": names(1) = "b": names(2) = "c"
Print names(0) + names(1) + names(2)

Dim a(2)
Print "redim: " + a(0) + " " + a(2)

Global total = 0
Local vec[4]
For i = 0 To 4
	vec[i] = i + 100
	total = total + vec[i]
Next
Print vec[2] + " total=" + total

Function Sum()
	Local s = 0
	For i = 0 To 2
		s = s + a(i)
	Next
	Return s
End Function
a(0) = 1: a(1) = 2: a(2) = 3
Print "sum=" + Sum()

Data 1, 2.5, "three"
Data 4
.moredata
Data 10, 20, 30

Read i, f#, s$
Print i + " " + f + " " + s
Read i
Print i
Restore moredata
Read i: Print i
Read i: Print i
Restore
Read s$: Print "restored: " + s
Const K = 10, KS$ = "const"
Print K * 2 + " " + KS

Dim objs.Vec(3)
Type Vec
	Field x
End Type
objs(1) = New Vec
objs(1)\x = 99
Print objs(1)\x
Print "nil elem null? " + (objs(0) = Null)
