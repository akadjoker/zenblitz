; Loading and playing an animated .x (DirectX retained-mode) mesh.
; mariorun.x is a binary .x with a 25-bone skeleton.
Graphics3D 640,480
SetBuffer BackBuffer()

camera=CreateCamera()
CameraRange camera,0.1,1000
PositionEntity camera,0,2,-8
AmbientLight 160,160,160
light=CreateLight()
RotateEntity light,45,45,0

path$="../mak/castle/markio/"
mesh=LoadAnimMesh(path$+"mariorun.x")
If mesh=0 Then
	Print "FAIL: could not load mariorun.x"
	End
EndIf

Print "loaded mariorun.x"
Print "  AnimLength = "+AnimLength(mesh)

ScaleEntity mesh,0.05,0.05,0.05
PositionEntity mesh,0,-1,0

; play the whole sequence, looping, at 0.5 frames per update
Animate mesh,1,0.5

While Not KeyHit(1)
	TurnEntity mesh,0,1,0
	UpdateWorld
	RenderWorld
	Text 5,5,"mariorun.x  AnimLength="+AnimLength(mesh)+"  AnimTime="+Int(AnimTime(mesh))
	Text 5,25,"Esc to quit"
	Flip
Wend
End
