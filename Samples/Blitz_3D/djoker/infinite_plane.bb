; CreatePlane must be an INFINITE ground (per Blitz3D's own docs), so a
; camera looking at the horizon sees it fill the lower half of the view
; no matter how far it travels - not a small quad that runs out.
Graphics3D 640,480
SetBuffer BackBuffer()
camera=CreateCamera()
CameraRange camera,0.1,1000
AmbientLight 255,255,255

tex=CreateTexture(64,64)
SetBuffer TextureBuffer(tex)
Color 0,0,80:Rect 0,0,64,64
Color 0,180,255:Rect 0,0,64,64,False
SetBuffer BackBuffer()

plane=CreatePlane()
EntityTexture plane,tex
ScaleTexture tex,10,10

PositionEntity camera,0,10,0
RotateEntity camera,20,0,0     ; look down at the ground

Function CountGround()
 n=0
 For py=0 To 479 Step 4
  For px=0 To 639 Step 4
   c=ReadPixel(px,py)
   If ((c Shr 8) And 255)>10 Or (c And 255)>10 Then n=n+1
  Next
 Next
 Return n
End Function

RenderWorld : Flip 0
near=CountGround()
Print "ground px at origin   = "+near

; travel a long way; an infinite plane must look the same
PositionEntity camera,5000,10,5000
RenderWorld : Flip 0
far=CountGround()
Print "ground px at (5000,5000) = "+far

If near>0 And far>0 Then
 Print "OK: plane is infinite (visible near and far from origin)"
Else
 Print "FAIL: plane ran out (near="+near+" far="+far+")"
EndIf
Print "DONE"
End
