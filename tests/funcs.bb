; functions
Function Add(a, b)
	Return a + b
End Function

Function Fact(n)
	If n <= 1 Then Return 1
	Return n * Fact(n - 1)
End Function

Function Greet$(name$, punct$ = "!")
	Return "Hello " + name + punct
End Function

Function Half#(x#)
	Return x / 2
End Function

Function NoRet(x)
	Local y = x * 2
	Print "noret " + y
End Function

Function Fib(n)
	If n < 2 Then Return n
	Return Fib(n-1) + Fib(n-2)
End Function

Global counter = 0
Function Inc()
	counter = counter + 1
	Return counter
End Function

Print Add(2, 3)
Print Fact(10)
Print Greet("World")
Print Greet("Bob", "?")
Print Half(5)
NoRet 21
NoRet(22)
Print Fib(20)
Inc: Inc: Inc()
Print "counter=" + counter
Print Add(Add(1,2), Add(3,4))
Local z = Add(10, Fact(3))
Print z
Print NoRet(1)
