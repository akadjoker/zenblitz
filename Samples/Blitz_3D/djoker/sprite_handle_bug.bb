; LoadSprite releases its Texture immediately after building the sprite's
; brush from it. Releasing the last reference destroys the GPU texture,
; but the brush kept only the handle - so from the very first RenderWorld
; the sprite binds a destroyed handle and the GPU layer reports
;   "gpu error 2 in operation 17 ... invalid resource handle"
; once per sprite per frame.
;
; castle.bb hits this with its bullet_hole / spark / tree sprites, which
; is why the errors appear as soon as those are on screen.
;
; Expect: OK on both lines once fixed.
Graphics3D 640,480
SetBuffer BackBuffer()

camera=CreateCamera()
PositionEntity camera,0,0,-10
AmbientLight 255,255,255

path$="../mak/castle/sprites/bullet_hole.bmp"
spr=LoadSprite(path$,1)
If spr=0 Then
	Print "FAIL: could not load "+path$
	End
EndIf
; NOTE: castle.bb uses EntityBlend 2 (multiply) on this sprite, which
; against a black background renders black by design - so this demo
; leaves the default blend in order to actually see the sprite.
SpriteViewMode spr,2
PositionEntity spr,0,0,0
ScaleSprite spr,3,3

For i=1 To 3
	RenderWorld
	Flip 0
Next

n=0
For py=0 To 479 Step 4
	For px=0 To 639 Step 4
		c=ReadPixel(px,py)
		If ((c Shr 16) And 255)>20 Or ((c Shr 8) And 255)>20 Or (c And 255)>20 Then n=n+1
	Next
Next
Print "sprite lit px = "+n
If n>0 Then
	Print "OK: sprite renders"
Else
	Print "FAIL: sprite not drawn (destroyed texture handle)"
EndIf
Print "Check the console above for 'invalid resource handle' - there must be none."
Print "DONE"
End
