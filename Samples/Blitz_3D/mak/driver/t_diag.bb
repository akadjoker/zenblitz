Graphics3D 320,240,0,2
cam = CreateCamera()
CameraClsColor cam,0,0,80
CameraRange cam,0.1,1000
light = CreateLight()
TurnEntity light,45,45,0

car = LoadMesh("car.x")
Print "LoadMesh handle=" + car
Print "CountSurfaces=" + CountSurfaces(car)
surf = GetSurface(car,1)
Print "surf1 handle=" + surf
If surf
 Print "CountVertices(surf1)=" + CountVertices(surf)
 Print "CountTriangles(surf1)=" + CountTriangles(surf)
 If CountVertices(surf) > 0
  Print "vertex0 = " + VertexX(surf,0) + "," + VertexY(surf,0) + "," + VertexZ(surf,0)
 EndIf
EndIf

ScaleMesh car,1,1,-1
FlipMesh car
FitMesh car,-1.5,-1,-3,3,2,6
surf2 = GetSurface(car,1)
If surf2 And CountVertices(surf2) > 0
 Print "after fit vertex0 = " + VertexX(surf2,0) + "," + VertexY(surf2,0) + "," + VertexZ(surf2,0)
EndIf

PositionEntity car,0,70,0
PositionEntity cam,0,70,-15
PointEntity cam,car

RenderWorld : Flip 0
p = ReadPixel(160,120) And $FFFFFF
Print "centre pixel = " + Hex(p) + " (0 means only background)"

nonBgCount = 0
For y = 0 To 239 Step 4
 For x = 0 To 319 Step 4
  px = ReadPixel(x,y) And $FFFFFF
  If px <> 0 nonBgCount = nonBgCount + 1
 Next
Next
Print "non-background sampled pixels = " + nonBgCount + " / 4800"
End
