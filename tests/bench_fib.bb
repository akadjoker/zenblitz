Function Fib(n)
	If n < 2 Then Return n
	Return Fib(n-1) + Fib(n-2)
End Function
t = MilliSecs()
Print Fib(30)
Print "fib ms: " + (MilliSecs() - t)
