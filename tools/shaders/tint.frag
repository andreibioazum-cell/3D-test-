#version 450
/* Тонированная текстура (tex_tint): силуэт по альфа-каналу текстуры, залитый
 * цветом вершины. RGB текселя игнорируется — как в попиксельном софтрендере,
 * где тень персонажа рисовалась цветом tint с альфой текстуры. */
layout(binding = 0) uniform sampler2D u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 0) out vec4 out_color;
void main() {
    float a = texture(u_tex, v_uv).a * v_col.a;
    out_color = vec4(v_col.rgb, a);
}
