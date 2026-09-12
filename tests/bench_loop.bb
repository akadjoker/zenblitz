t = MilliSecs()
s = 0
For i = 1 To 10000000
	s = s + i Mod 7
Next
Print s
Print "loop ms: " + (MilliSecs() - t)
t = MilliSecs()
f# = 0
For i = 1 To 5000000
	f = f + Sqr(i) * 0.5
Next
Print Int(f)
Print "float ms: " + (MilliSecs() - t)
Type P
	Field x#, y#
End Type
For i = 1 To 1000
	p.P = New P
	p\x = i
Next
t = MilliSecs()
acc# = 0
For k = 1 To 2000
	For p.P = Each P
		p\y = p\y + p\x * 0.001
		acc = acc + p\y
	Next
Next
Print Int(acc)
Print "types ms: " + (MilliSecs() - t)
