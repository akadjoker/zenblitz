; native C++ codegen: Repeat, Exit, and forward function declarations
Function Later(n)
    If n <= 0 Then Return 0
    Return n + Earlier(n - 1)
End Function

Function Earlier(n)
    If n <= 0 Then Return 0
    Return n + Later(n - 1)
End Function

i = 0
Repeat
    i = i + 1
    If i = 3 Then Exit
Forever
Print i

j = 0
Repeat
    j = j + 1
Until j = 2
Print j

Later 1
Print Later(3)
