#version 450
/* Сплошные примитивы (rect/roundrect/circle/ring/line): цвет берётся целиком
 * из вершины, смешивание делает фиксированный конвейер (src-alpha). */
layout(location = 1) in vec4 v_col;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = v_col;
}
