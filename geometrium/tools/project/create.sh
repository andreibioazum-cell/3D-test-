#!/bin/sh
# Scaffold a new Enjoer engine project.
# Usage: sh tools/project/create.sh path/to/project
set -eu
cd "$(dirname "$0")/../.."
if [ $# -lt 1 ]; then
    echo "usage: sh tools/project/create.sh <project-dir>" >&2
    exit 1
fi
dir="$1"
if [ -e "$dir/project.eng" ]; then
    echo "project already exists: $dir/project.eng" >&2
    exit 1
fi
mkdir -p "$dir/scenes" "$dir/scripts"
name=$(basename "$dir")
cat > "$dir/project.eng" <<EOF
# Enjoer engine project manifest.
name = $name
main_scene = scenes/main.escn
EOF
cat > "$dir/scenes/main.escn" <<EOF
[node name="Main" type="Node"]

[node name="Sun" type="DirectionalLight3D" parent="."]
yaw = 0.5
pitch = -0.9

[node name="Camera" type="Camera3D" parent="."]
position = 0 4 9
pitch = -0.2
current = true

[node name="Cube" type="MeshInstance3D" parent="."]
mesh = cube
color = 0.85 0.25 0.25
size = 2
position = 0 1 0
script = "spin"
EOF
cat > "$dir/scripts/spin.c" <<EOF
#include "eng_api.h"

void eng_script_process(EngNode *self, float dt) {
    float yaw, pitch, roll;
    eng_node3d_get_rotation(self, &yaw, &pitch, &roll);
    eng_node3d_set_rotation(self, yaw + dt, pitch, roll);
}
EOF
echo "Created engine project '$name' in $dir"
echo "Run it with:  tools/preview/build.sh && ./preview --project $dir"
