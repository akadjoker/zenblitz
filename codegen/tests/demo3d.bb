; zenblitz native 3D demo
Graphics3D 640, 480
cam = CreateCamera()
PositionEntity cam, 0, 0, -5
light = CreateLight()
PositionEntity light, 5, 5, -5
cube = CreateCube()
PositionEntity cube, 0, 0, 0
EntityColor cube, 255, 100, 50
CameraRange cam, 1, 100
AmbientLight 50, 50, 50
UpdateWorld
RenderWorld
Flip
WaitKey
EndGraphics
