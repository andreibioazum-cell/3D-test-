#version 450
/* Текстурированные примитивы (tex/tex_tint/текст): цвет вершины умножается на
 * тексель. Сэмплер nearest — совпадает с попиксельным софтрендером. Шрифтовой
 * атлас заливается белым с альфой из покрытия, поэтому текст идёт через тот же
 * шейдер: RGB текселя = 1, альфа = покрытие глифа. */
layout(binding = 0) uniform sampler2D u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = v_col * texture(u_tex, v_uv);
}
