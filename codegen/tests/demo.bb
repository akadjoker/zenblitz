; zenblitz native demo - draws a moving pattern
Graphics 640, 480
ClsColor 20, 20, 60
Color 255, 255, 0
Rect 10, 10, 620, 460, 0
Color 0, 255, 100
Line 0, 0, 640, 480
Line 640, 0, 0, 480
Color 100, 200, 255
Oval 270, 190, 100, 100
Color 255, 100, 100
Oval 220, 140, 60, 60, 0
Oval 360, 280, 60, 60, 0
Color 255, 255, 255
Text 240, 50, "zenblitz native C++", 1
Text 280, 420, "press any key", 1
Flip
WaitKey
EndGraphics
