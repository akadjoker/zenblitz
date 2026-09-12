Function Boom()
	RuntimeError "custom failure"
End Function
Print "a"
Boom()
Print "b"
