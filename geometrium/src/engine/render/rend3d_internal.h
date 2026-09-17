#ifndef REND3D_INTERNAL_H
#define REND3D_INTERNAL_H
#include "rend_internal.h"
enum { TEXTURE_SIZE=32,PALETTE_SIZE=64,MIP_TEXELS=1365,FOG_LEVELS=64,LIGHT_LEVELS=32 };
/* Gentle face orientation, independent of occlusion. No cast/contact shadows. */
static inline float rend_face_shade(float nx,float ny,float nz) {return .82f+.14f*ny+.04f*nx+.03f*nz;}
#define REND_DARK_FLOOR .045f

typedef struct { float x,y,z,u,v; } RendVertex;
typedef struct {
    uint32_t palette[PALETTE_SIZE];
    unsigned char mip[MIP_TEXELS];
    int colors;
    unsigned char alpha; /* 0 = opaque; water blends over the framebuffer */
    Image image;
    uint32_t shades[LIGHT_LEVELS][FOG_LEVELS*PALETTE_SIZE],fog_color;
    uint32_t ready;
} RendMaterial;
RendMaterial *rend_material(int block,int face);
const uint32_t *rend_material_shades(RendMaterial *material,int level,uint32_t fog);
void rend3d_polygon(const RendVertex *vertices,int n,float nx,float ny,float nz,uint32_t color,RendMaterial *material,const unsigned char *light);
/* Camera-space overlay: stable projection/lighting, no world fog, own depth.
 * Used by the first-person hand in src/geometrium/. */
void rend3d_viewmodel(int enabled);
int rend3d_project(float x,float y,float z,float *sx,float *sy);
void rend3d_depth_clear(float x0,float y0,float x1,float y1);
void rend3d_segment(float x,float y,float z,float x2,float y2,float z2,uint32_t color);
#endif
