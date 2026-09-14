Graphics 64,64

mode=CountGfxModes3D()
If mode>0 Then
	If GfxMode3DExists(GfxModeWidth(1),GfxModeHeight(1),GfxModeDepth(1))=0 Then RuntimeError "Graphics mode enumeration is inconsistent"
End If

image=CreateImage(4,4)
buffer=ImageBuffer(image)
SetBuffer buffer
WritePixel 1,2,$ff123456

If ReadPixel(1,2)<>$ff123456 Then RuntimeError "ImageBuffer pixel access failed"
Origin 1,1
WritePixel 2,0,$ff654321
If ReadPixel(2,0)<>$ff654321 Then RuntimeError "Origin did not offset buffer pixels"
Viewport 0,0,1,1
WritePixel 1,0,$ffabcdef
If ReadPixelFast(2,1)<>0 Then RuntimeError "Viewport did not clip buffer pixels"
SetBuffer buffer
If ReadPixel(3,1)<>$ff654321 Then RuntimeError "SetBuffer did not reset Origin and Viewport"
Origin 1,1
Color $22,$33,$44
Plot 0,0
If ReadPixel(0,0)<>$ff223344 Then RuntimeError "Origin did not offset Plot"
Viewport 0,0,2,2
Rect 0,0,4,4
If ReadPixelFast(3,3)<>0 Then RuntimeError "Viewport did not clip Rect"
SetBuffer buffer

copy=CopyImage(image)
ResizeImage copy,2,2
SetBuffer ImageBuffer(copy)
WritePixel 1,1,$ffabcdef
If ReadPixel(1,1)<>$ffabcdef Then RuntimeError "Resized ImageBuffer pixel access failed"

SetBuffer ImageBuffer(image)
If ReadPixel(0,0)<>0 Then RuntimeError "CopyImage did not create an independent frame"

canvas=CreateImage(4,4)
SetBuffer ImageBuffer(canvas)
DrawImage image,0,0
If ReadPixel(1,2)<>$ff123456 Then RuntimeError "DrawImage did not copy pixels to ImageBuffer"
DrawImageRect image,2,0,1,2,1,1
If ReadPixel(2,0)<>$ff123456 Then RuntimeError "DrawImageRect did not copy source rectangle"
DrawBlock image,0,0
If ReadPixel(1,2)<>$ff123456 Then RuntimeError "DrawBlock did not copy pixels to ImageBuffer"
DrawBlockRect image,2,1,1,2,1,1
If ReadPixel(2,1)<>$ff123456 Then RuntimeError "DrawBlockRect did not copy source rectangle"

grabbed=CreateImage(1,1)
GrabImage grabbed,1,2
If ReadPixel(0,0,ImageBuffer(grabbed))<>$ff123456 Then RuntimeError "GrabImage did not capture from ImageBuffer"

tile=CreateImage(2,2)
SetBuffer ImageBuffer(tile)
WritePixel 1,0,$ff123456
tiled=CreateImage(4,4)
SetBuffer ImageBuffer(tiled)
TileBlock tile
If ReadPixel(1,0)<>$ff123456 Or ReadPixel(3,0)<>$ff123456 Then RuntimeError "TileBlock did not repeat image frames"

SetBuffer ImageBuffer(canvas)
CopyPixel 1,2,ImageBuffer(image),3,3,ImageBuffer(canvas)
If ReadPixel(3,3)<>$ff123456 Then RuntimeError "CopyPixel did not copy between ImageBuffers"
CopyRect 1,2,1,1,0,3,ImageBuffer(image),ImageBuffer(canvas)
If ReadPixel(0,3)<>$ff123456 Then RuntimeError "CopyRect did not copy between ImageBuffers"
GetColor 0,3
If ColorRed()<>$12 Or ColorGreen()<>$34 Or ColorBlue()<>$56 Then RuntimeError "GetColor did not update canvas color"

If SaveBuffer(ImageBuffer(image),"/tmp/zenblitz_native_image_buffer.bmp")=0 Then RuntimeError "SaveBuffer failed"
loaded=CreateImage(4,4)
If LoadBuffer(ImageBuffer(loaded),"/tmp/zenblitz_native_image_buffer.bmp")=0 Then RuntimeError "LoadBuffer failed"
If ReadPixel(1,2,ImageBuffer(loaded))<>$ff123456 Then RuntimeError "LoadBuffer did not populate ImageBuffer"

sheet=CreateImage(4,2)
SetBuffer ImageBuffer(sheet)
WritePixel 0,0,$ff010203
WritePixel 2,0,$ff040506
SaveBuffer ImageBuffer(sheet),"/tmp/zenblitz_native_anim_sheet.bmp"
anim=LoadAnimImage("/tmp/zenblitz_native_anim_sheet.bmp",2,2,0,2)
If ReadPixel(0,0,ImageBuffer(anim,0))<>$ff010203 Then RuntimeError "LoadAnimImage did not extract the first frame"
If ReadPixel(0,0,ImageBuffer(anim,1))<>$ff040506 Then RuntimeError "LoadAnimImage did not extract the second frame"

font=LoadFont("ignored",24)
SetFont font
If FontHeight()<>24 Or StringHeight("text")<>24 Then RuntimeError "SetFont did not apply font size"
FreeFont font

SetBuffer BackBuffer()
End