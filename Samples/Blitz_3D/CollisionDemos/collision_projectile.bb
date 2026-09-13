;==============================================================
; Demo 3: Projectile Hit Detection (MeshesIntersect)
;==============================================================
; Demonstrates: MeshesIntersect, Types, Fields, For/Each
;
; The player shoots projectiles. When a projectile overlaps an
; enemy mesh, MeshesIntersect returns True and the enemy is
; destroyed. This is the approach used in Blitz3D shooters for
; hit detection that needs mesh-accurate results.
;
; Controls:
;   Mouse Left / Space - Shoot
;   Mouse Move         - Aim
;   ESC                - Quit
;
; No collision types are needed — MeshesIntersect works on any
; two entities directly.
;==============================================================

Graphics3D 800, 600
SetBuffer BackBuffer()

;--- Light ---
light = CreateLight()
RotateEntity light, 45, 45, 0

;--- Camera (first person) ---
camera = CreateCamera()
PositionEntity camera, 0, 5, -30
CameraRange camera, 0.1, 500

;--- Ground for visual reference ---
ground = CreatePlane()
EntityColor ground, 40, 80, 40

;--- Types ---
Type enemy
    Field entity
End Type

Type bullet
    Field entity
    Field life     ; frames remaining before auto-cleanup
End Type

;--- Spawn some enemies ---
Global enemy_count = 0
SpawnEnemy(  0, 5,   5)
SpawnEnemy(-10, 5,  15)
SpawnEnemy( 10, 5,  15)
SpawnEnemy( -5, 5, -10)
SpawnEnemy(  5, 5, -10)

;--- Muzzle flash light (off by default) ---
muzzle = CreateLight(2)
LightRange muzzle, 10
PositionEntity muzzle, 0, 5, -28
EntityColor muzzle, 255, 200, 100
HideEntity muzzle

HidePointer()
shot_cooldown = 0

;--- Main loop ---
While Not KeyDown(1)

    ; Mouselook
    TurnEntity camera, -MouseYSpeed() * 0.2, -MouseXSpeed() * 0.2, 0
    MoveMouse GraphicsWidth()/2, GraphicsHeight()/2

    ; Shoot
    firing = MouseDown(1) Or KeyDown(57)
    If firing And shot_cooldown = 0
        FireBullet()
        shot_cooldown = 10
        ShowEntity muzzle
    EndIf
    If shot_cooldown > 0
        shot_cooldown = shot_cooldown - 1
        If shot_cooldown = 0 Then HideEntity muzzle
    EndIf

    ; Update bullets — move forward, check hits, expire
    For b.bullet = Each bullet
        MoveEntity b\entity, 0, 0, 2.0
        b\life = b\life - 1

        hit = 0
        For e.enemy = Each enemy
            If MeshesIntersect(b\entity, e\entity)
                ; Hit! Destroy enemy and bullet
                FreeEntity e\entity
                Delete e
                FreeEntity b\entity
                Delete b
                hit = 1
                enemy_count = enemy_count - 1
                Exit
            EndIf
        Next

        If hit = 0
            ; Expire bullet after 90 frames
            If b\life <= 0
                FreeEntity b\entity
                Delete b
            EndIf
        EndIf
    Next

    ; Respawn wave when all cleared
    If enemy_count = 0
        SpawnEnemy(Rnd(-12, 12), 5, Rnd(0, 20))
        SpawnEnemy(Rnd(-12, 12), 5, Rnd(0, 20))
        SpawnEnemy(Rnd(-12, 12), 5, Rnd(0, 20))
    EndIf

    RenderWorld
    Text 10, 10, "Projectile Hit Detection Demo (MeshesIntersect)"
    Text 10, 30, "Mouse Left / Space: shoot    Mouse: aim    ESC: quit"
    Text 10, 50, "Enemies remaining: " + enemy_count
    Flip
Wend

End

;--- Functions ---

Function SpawnEnemy(x#, y#, z#)
    e.enemy = New enemy
    e\entity = CreateCube()
    ScaleEntity e\entity, 1.5, 1.5, 1.5
    PositionEntity e\entity, x, y, z
    EntityColor e\entity, 200, 50, 50
    enemy_count = enemy_count + 1
End Function

Function FireBullet()
    b.bullet = New bullet
    b\entity = CreateSphere(8)
    ScaleEntity b\entity, 0.2, 0.2, 0.2
    PositionEntity b\entity, EntityX(camera), EntityY(camera), EntityZ(camera)
    RotateEntity b\entity, EntityPitch(camera), EntityYaw(camera), 0
    MoveEntity b\entity, 0, 0, 1.5  ; spawn in front of camera
    EntityColor b\entity, 255, 255, 100
    b\life = 90
End Function
