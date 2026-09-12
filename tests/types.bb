; custom types
Type Vec
	Field x#, y#
	Field name$
	Field tag
End Type

Type Node
	Field v.Vec
	Field arr[3]
End Type

For i = 1 To 5
	v.Vec = New Vec
	v\x = i
	v\y = i * 10
	v\name = "v" + i
Next
For v.Vec = Each Vec
	Write v\name + "(" + v\x + "," + v\y + ") "
Next
Print ""
f.Vec = First Vec: l.Vec = Last Vec
Print "first=" + f\name + " last=" + l\name
v.Vec = First Vec
v = After v
Print "after first = " + v\name
v = Before v
Print "before = " + v\name
Print "before first null? " + (Before First Vec = Null)
Print "after last null? " + (After Last Vec = Null)

; delete inside Each
For v.Vec = Each Vec
	If v\x = 2 Or v\x = 4 Then Delete v
Next
For v.Vec = Each Vec
	Write v\name + " "
Next
Print ""

; deleted object compares to Null
a.Vec = First Vec
b.Vec = a
Delete a
Print "a null? " + (a = Null) + " b null? " + (b = Null)
Print "a<>Null? " + (a <> Null)
If a = Null Then Print "a is null (if)"
If b <> Null Then Print "b not null" Else Print "b null (if)"

; insert
v1.Vec = First Vec
v2.Vec = Last Vec
Insert v2 Before v1
For v.Vec = Each Vec
	Write v\name + " "
Next
Print ""

; object in field, vector field
n.Node = New Node
n\v = New Vec
n\v\name = "inner"
n\arr[0] = 5
n\arr[3] = 7
Print n\v\name + " " + n\arr[0] + " " + n\arr[3] + " " + n\arr[1]
Print "vec str: " + Str(First Vec)
Print "null str: " + Str(n\v\tag) + " " + (n\v = Null)

; handle / object
h = Handle(First Vec)
Print "handle=" + h
o.Vec = Object.Vec(h)
Print "object name=" + o\name
Print "bad handle null? " + (Object.Vec(9999) = Null)

Delete Each Vec
Print "after delete each: " + (First Vec = Null)
c = 0
For v.Vec = Each Vec
	c = c + 1
Next
Print "count=" + c

; Null assignment and default fields
w.Vec = New Vec
Print "defaults: " + w\x + " " + w\name + "|" + w\tag
w = Null
Print "w null " + (w = Null)
