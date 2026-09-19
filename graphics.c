/* Vulkan-рендер в одном трансляционном юните; части сгруппированы по ролям. */
#include "native/graphics/types.inc"
#include "native/graphics/geometry.inc"
#include "native/graphics/vulkan_backend.inc"
#include "native/graphics/lifecycle.inc"
/* 3D-слой (cam3d/cube3d/line3d/flush3d) после lifecycle: пользуется push(),
 * командами и pack_c, а push() в свою очередь зовёт его ds3d_flush_pending(). */
#include "native/graphics/render3d.inc"
