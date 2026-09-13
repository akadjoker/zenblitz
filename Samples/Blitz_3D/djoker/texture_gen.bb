; Minimal demo: generate a texture with 2D commands, put it on a cube AND a plane.
Graphics3D 640,480
SetBuffer BackBuffer()

camera=CreateCamera()
PositionEntity camera,0,3,-8
AmbientLight 255,255,255
light=CreateLight()

; --- generate a checker/grid texture procedurally ---
tex=CreateTexture(64,64)
SetBuffer TextureBuffer(tex)
Color 0,0,64
Rect 0,0,64,64
Color 0,160,255
Rect 0,0,64,64,False
Color 255,200,0
Rect 8,8,48,48,False
Line 0,0,63,63
Line 63,0,0,63
SetBuffer BackBuffer()

SaveBuffer TextureBuffer(tex),"demo_tex.bmp"

cube=CreateCube()
EntityTexture cube,tex
PositionEntity cube,-2,1,0

plane=CreatePlane()
EntityTexture plane,tex
PositionEntity plane,0,0,0

; verify on screen
RenderWorld
Flip 0

lit=0 : yellow=0
For py=0 To 479 Step 4
 For px=0 To 639 Step 4
  c=ReadPixel(px,py)
  r=(c Shr 16) And 255 : g=(c Shr 8) And 255 : b=c And 255
  If r>15 Or g>15 Or b>15 Then lit=lit+1
  If r>120 And g>90 And b<120 Then yellow=yellow+1
 Next
Next
Print "lit px="+lit+"  yellow(inner rect) px="+yellow
If lit=0 Then
 Print "FAIL: nothing rendered"
Else If yellow=0 Then
 Print "FAIL: texture detail missing (flat colour only)"
Else
 Print "OK: generated texture visible on geometry"
EndIf

While Not KeyHit(1)
 TurnEntity cube,0,1,0
 RenderWorld
 Flip
Wend
End
