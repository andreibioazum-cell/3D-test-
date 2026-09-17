#include "geometrium_internal.h"
void geometrium_scene_draw(Buffer *buffer) {
    float x,y,z,yaw,pitch;
    geometrium_player_pos(&x,&y,&z);geometrium_camera_angles(&yaw,&pitch);
    int scale=rend_render_scale(screen_w,screen_h);
    double start=app_now();
    if (!rend3d_begin(buffer,scale,x,y+geometrium_player_eye(),z,yaw,pitch,66)) return;
    rend3d_sky(0xff78b8e8u,0xffc7e5f5u);
    float distance=voxel_world_distance();rend3d_fog(distance*.5f,distance);
    voxel_world_draw();geometrium_target_draw();geometrium_hand_draw();rend3d_end();
    rend_render_time(app_now()-start);
}
