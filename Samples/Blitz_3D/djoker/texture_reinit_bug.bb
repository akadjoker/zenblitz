; Regression test for the stale-GPU-handle bug tron.bb hit through
; start.bb's mode-select menu: EndGraphics followed by a fresh
; Graphics3D left the engine holding handles issued by the destroyed
; device, so anything created afterwards rendered untextured, or not at
; all, with "invalid resource handle" errors.
;
; Fixed by having Image::ensureUploaded and Surface::ensureGpu notice
; the device changed and rebuild instead of updating through a dead
; handle. This must print OK.
Graphics3D 640,480
SetBuffer BackBuffer()

EndGraphics
Graphics3D 640,480
SetBuffer BackBuffer()

camera=CreateCamera()
PositionEntity camera,0,3,-8
AmbientLight 255,255,255

tex=CreateTexture(64,64)
SetBuffer TextureBuffer(tex)
Color 0,0,64
Rect 0,0,64,64
Color 255,200,0
Rect 8,8,48,48,False
SetBuffer BackBuffer()

cube=CreateCube()
EntityTexture cube,tex

RenderWorld
Flip 0

lit=0
For py=0 To 479 Step 4
 For px=0 To 639 Step 4
  c=ReadPixel(px,py)
  If ((c Shr 16) And 255)>15 Or ((c Shr 8) And 255)>15 Or (c And 255)>15 Then lit=lit+1
 Next
Next
Print "lit px="+lit
If lit=0 Then
 Print "FAIL: nothing rendered after Graphics3D re-init"
Else
 Print "OK: renders after re-init"
EndIf
Print "DONE"
End
