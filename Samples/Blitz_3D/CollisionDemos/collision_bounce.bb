;==============================================================
; Demo 2: Bounce Collision (Reflection)
;==============================================================
; Demonstrates: EntityCollided, CollisionNX/NY/NZ, AlignToVector
;
; A ball bounces around inside a closed box. On each impact the
; collision normal is read and AlignToVector reflects the velocity.
; FlipMesh is used so the inside walls are collidable.
;
; Controls:
;   Space - Give the ball a random kick
;   R     - Reset ball position
;   ESC   - Quit
;
; Collision types:
;   type_ball = 1
;   type_box  = 2
;==============================================================

Graphics3D 800, 600
SetBuffer BackBuffer()

Const type_ball = 1
Const type_box  = 2

;--- Light ---
light = CreateLight()
RotateEntity light, 45, 45, 0

;--- Camera ---
camera = CreateCamera()
PositionEntity camera, 0, 8, -25
PointEntity camera, 0, 0, 0

;--- Build the box (inside visible) ---
box = CreateCube()
ScaleEntity box, 15, 10, 15
FlipMesh box               ; makes interior faces visible & collidable
EntityColor box, 80, 80, 120
EntityType box, type_box

;--- Build the ball ---
Global ball = CreateSphere(24)
sphere_radius# = 1.0
EntityRadius ball, sphere_radius
EntityType ball, type_ball
PositionEntity ball, 0, 0, 0
EntityColor ball, 255, 150, 50

;--- Register collisions ---
Collisions type_ball, type_box, 2, 2   ; sphere-to-polygon, slide

;--- Velocity ---
Global vx# = 0.2, vy# = 0.15, vz# = 0.25

;--- Main loop ---
While Not KeyDown(1)

    ; Input
    If KeyHit(57)  ; Space — random kick
        vx = Rnd(-0.4, 0.4)
        vy = Rnd( 0.1, 0.3)
        vz = Rnd(-0.4, 0.4)
    EndIf

    If KeyHit(19) ; R — reset
        PositionEntity ball, 0, 0, 0
        vx = 0.2 : vy = 0.15 : vz = 0.25
    EndIf

    ; Move the ball
    TranslateEntity ball, vx, vy, vz

    ; Apply mild gravity
    vy = vy - 0.005

    ; Process collisions
    UpdateWorld

    ; Check for collisions and reflect
    cnt = CountCollisions(ball)
    For c = 1 To cnt
        nx# = CollisionNX(ball, c)
        ny# = CollisionNY(ball, c)
        nz# = CollisionNZ(ball, c)

        ; Reflect velocity around the collision normal
        ; AlignToVector with factor -1 reflects the "direction"
        ; We do a manual reflection: v' = v - 2*(v.n)*n
        dot# = vx*nx + vy*ny + vz*nz
        vx = vx - 2.0 * dot * nx
        vy = vy - 2.0 * dot * ny
        vz = vz - 2.0 * dot * nz

        ; Slight energy loss on each bounce
        vx = vx * 0.95
        vy = vy * 0.95
        vz = vz * 0.95
    Next

    RenderWorld
    Text 10, 10, "Bounce Collision Demo"
    Text 10, 30, "Space: kick    R: reset    ESC: quit"
    Text 10, 50, "Velocity: " + Int(vx*100)/100.0 + ", " + Int(vy*100)/100.0 + ", " + Int(vz*100)/100.0
    Text 10, 70, "Collisions this frame: " + cnt
    Flip
Wend

End
