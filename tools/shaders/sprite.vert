#version 450
/* Общий вершинный шейдер рендера Cubic Battle: принимает вершину в пикселях
 * виртуального экрана (origin — левый верхний угол, y вниз) и переводит её в
 * NDC Vulkan'а через push-константу (2/w, 2/h, -1, -1). UV и цвет уходят
 * фрагментному шейдеру без изменений: у сплошных примитивов uv не используется. */
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_col; /* R8G8B8A8_UNORM, normalized */
layout(push_constant) uniform Push {
    vec4 u_screen; /* (2/w, 2/h, -1, -1) */
} pc;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_col;
void main() {
    gl_Position = vec4(in_pos.x * pc.u_screen.x + pc.u_screen.z,
                       in_pos.y * pc.u_screen.y + pc.u_screen.w, 0.0, 1.0);
    v_uv = in_uv;
    v_col = in_col;
}
