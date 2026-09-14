#include "zen_codegen_support.hpp"

#include "zen_native_runtime.hpp"

#include "zenblitzsdk.h"

#include "zen_engine_sdk.h"

static zen_native::Heap z_heap;

static ZenDataValue z_data_values[1] = {{0,0,0.0,std::string()}};



static int64_t z_camera = 0;
static int64_t z_light = 0;
static int64_t z_man = 0;
static int64_t z_dist = 0;


int main()
{
    z_data = z_data_values;
    (void)z_camera;
    (void)z_light;
    (void)z_man;
    (void)z_dist;
    zen_graphics3d_open(800, 600, 0, 0);
    zen_set_buffer(static_cast<int64_t>(zen_back_buffer()));
    z_camera = static_cast<int64_t>(zen_create_camera(0));
    zen_camera_viewport(static_cast<int64_t>(z_camera), 0, 0, 800, 600);
    z_light = static_cast<int64_t>(zen_create_light(1));
    z_man = static_cast<int64_t>(zen_load_md2("gargoyle.md2"));
    zen_position_entity(static_cast<int64_t>(z_man), 0, -35, 600, 0);
    zen_rotate_entity(static_cast<int64_t>(z_man), 0, 180, 0, 0);
    zen_animate_md2(static_cast<int64_t>(z_man), 1, 0.10000000000000001, 32, 46, 0);
    while (static_cast<int64_t>((static_cast<int64_t>(zen_key_hit(1)) == 0))) {
        if (static_cast<int64_t>((static_cast<int64_t>(z_dist) < 970))) {
            zen_move_entity(static_cast<int64_t>(z_man), 0, 0, 0.5);
        }
        if (static_cast<int64_t>((static_cast<int64_t>(z_dist) == 970))) {
            zen_animate_md2(static_cast<int64_t>(z_man), 1, 0.050000000000000003, 0, 31, 0);
        }
        z_dist = static_cast<int64_t>((static_cast<int64_t>(z_dist) + 1));
        zen_update_world();
        zen_render_world(1);
        zen_text(320, 500, "An Animated MD2 Demo", 0, 0);
        zen_flip(1);
    }
    zen_end();
    return 0;
}
