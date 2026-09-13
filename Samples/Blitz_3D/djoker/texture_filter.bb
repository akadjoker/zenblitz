; TextureFilter: how to get mipmaps (and other flags) onto textures a
; MESH loads from inside its own materials.
;
; A .x file's materials are loaded with flags 0, exactly as the original
; Blitz3D loader did, so those textures get no mipmaps and look harsh /
; pixelated when the surface is far away or at a grazing angle. The
; script never names those files, so it cannot pass flags to LoadTexture
; itself - TextureFilter is the hook Blitz3D provides for it.
;
; Any texture whose filename contains the match text gets the flags added.
Graphics3D 640,480
SetBuffer BackBuffer()

Const tex_color   = 1
Const tex_alpha   = 2
Const tex_mask    = 4
Const tex_mipmap  = 8
Const tex_clampu  = 16
Const tex_clampv  = 32

; must be registered BEFORE the mesh is loaded
TextureFilter ".jpg", tex_mipmap
TextureFilter ".bmp", tex_mipmap

camera=CreateCamera()
CameraRange camera,0.1,10000
AmbientLight 200,200,200
light=CreateLight()
RotateEntity light,45,45,0

mesh=LoadMesh("../mak/castle/castle/CASTLE1.X")
If mesh=0 Then
	Print "FAIL: could not load CASTLE1.X"
	End
EndIf
Print "castle surfaces = "+CountSurfaces(mesh)

PositionEntity camera,0,100,-400

While Not KeyHit(1)
	TurnEntity mesh,0,0.3,0
	RenderWorld
	Text 5,5,"TextureFilter .jpg -> mipmapped   (Esc to quit)"
	Text 5,25,"surfaces="+CountSurfaces(mesh)
	Flip
Wend

ClearTextureFilters
End
