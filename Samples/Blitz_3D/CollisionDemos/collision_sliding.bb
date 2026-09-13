;==============================================================
; Demo 1: Sliding Collision (FPS-Style Movement)
;==============================================================
; Demonstrates: EntityType, Collisions, UpdateWorld
;
; The player is a sphere that slides along walls and floors.
; Gravity pulls the player down each frame. Collision method
; 2 (sphere-to-polygon) with response 2 (slide) prevents the
; player from passing through geometry.
;
; Controls:
;   W / Cursor Up    - Move forward
;   S / Cursor Down  - Move backward
;   A / Cursor Left  - Strafe left
;   D / Cursor Right - Strafe right
;   Mouse            - Look around
;   Space            - Jump
;   ESC              - Quit
;
; Collision types:
;   type_player   = 1   (sphere, the player)
;   type_scenery  = 2   (walls, floor, ramps)
;==============================================================

Graphics3D 800, 600
SetBuffer BackBuffer()

;--- Collision type constants ---
Const type_player  = 1
Const type_scenery = 2

;--- Create a light so we can see everything ---
Global light = CreateLight()
RotateEntity light, 45, 45, 0

;--- Camera ---
Global camera = CreateCamera()
CameraRange camera, 0.1, 500

;--- Build a floor ---
floor_mesh = CreatePlane()
grass_tex = LoadTexture("../textures/grass.bmp")
If grass_tex <> 0
    EntityTexture floor_mesh, grass_tex
EndIf
EntityType floor_mesh, type_scenery
EntityColor floor_mesh, 60, 120, 60

;--- Build a perimeter wall ring ---
Global Dim wall(8)
For i = 0 To 7
    ang# = i * 45.0
    wall(i) = CreateCube()
    ScaleEntity wall(i), 40, 5, 2
    PositionEntity wall(i), Cos(ang) * 38, 2.5, Sin(ang) * 38
    RotateEntity wall(i), 0, -ang, 0
    EntityColor wall(i), 150, 100, 60
    EntityType wall(i), type_scenery
Next

;--- Add a few ramp blocks to demonstrate sliding up slopes ---
ramp1 = CreateCube()
ScaleEntity ramp1, 8, 1, 12
RotateEntity ramp1, 20, 0, 0
PositionEntity ramp1, -15, 0, -10
EntityColor ramp1, 100, 100, 200
EntityType ramp1, type_scenery

ramp2 = CreateCube()
ScaleEntity ramp2, 8, 1, 12
RotateEntity ramp2, -20, 0, 0
PositionEntity ramp2, 15, 0, 10
EntityColor ramp2, 200, 100, 100
EntityType ramp2, type_scenery

;--- Create the player as an invisible sphere ---
Global player = CreateSphere()
ScaleEntity player, 1, 1, 1
EntityRadius player, 1.0
EntityType player, type_player
PositionEntity player, 0, 5, 20
EntityColor player, 255, 255, 0
; Hide the player mesh — we look through the camera instead
HideEntity player

;--- Register collisions: sphere-to-polygon, sliding response ---
Collisions type_player, type_scenery, 2, 2

;--- Movement variables ---
Global move_speed#  = 0.3
Global gravity#     = 0.04
Global jump_force#  = 0.0
Global on_ground    = 0

HidePointer()

;--- Main loop ---
While Not KeyDown(1)

    ; Mouselook
    TurnEntity camera, -MouseYSpeed() * 0.2, -MouseXSpeed() * 0.2, 0
    ; Clamp pitch
    If EntityPitch(camera) >  80 Then RotateEntity camera,  80, EntityYaw(camera), 0
    If EntityPitch(camera) < -80 Then RotateEntity camera, -80, EntityYaw(camera), 0

    ; Keep mouse centered
    MoveMouse GraphicsWidth()/2, GraphicsHeight()/2

    ; Copy camera yaw to player so MoveEntity uses the facing direction
    RotateEntity player, 0, EntityYaw(camera), 0

    ; Keyboard movement
    If KeyDown(17) Or KeyDown(200) ; W / Up
        MoveEntity player, 0, 0, move_speed
    EndIf
    If KeyDown(31) Or KeyDown(208) ; S / Down
        MoveEntity player, 0, 0, -move_speed
    EndIf
    If KeyDown(30) Or KeyDown(203) ; A / Left
        MoveEntity player, -move_speed, 0, 0
    EndIf
    If KeyDown(32) Or KeyDown(205) ; D / Right
        MoveEntity player, move_speed, 0, 0
    EndIf

    ; Jump
    If KeyDown(57) And on_ground
        jump_force = 0.6
        on_ground = 0
    EndIf

    ; Apply gravity / jump
    TranslateEntity player, 0, jump_force - gravity, 0
    jump_force = jump_force - 0.04
    If jump_force < -0.5 Then jump_force = -0.5

    ; Process collisions
    UpdateWorld

    ; Detect ground contact
    on_ground = 0
    cnt = CountCollisions(player)
    For c = 1 To cnt
        If CollisionNY(player, c) > 0.5 Then on_ground = 1
    Next

    ; Place camera at player eye level
    PositionEntity camera, EntityX(player), EntityY(player) + 1.2, EntityZ(player)

    ; HUD
    RenderWorld
    Text 10, 10, "Sliding Collision Demo"
    Text 10, 30, "WASD / Arrows: move    Mouse: look    Space: jump    ESC: quit"
    Text 10, 50, "On ground: " + on_ground + "   Collisions: " + cnt
    Flip
Wend

End
