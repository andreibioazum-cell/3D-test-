/* World-aligned voxel surfaces; no legacy scene objects. */
#include "rend3d_internal.h"

static const unsigned char corners[6][4][3] = {
    {{0,1,0},{0,1,1},{1,1,1},{1,1,0}},
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}},
    {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},
    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}}
};
static const int normals[6][3] = {{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}};

void rend3d_surface(int sx,int sy,int sz,int u,int v,int face,int block,const unsigned char light[4]) {
    if (face<0 || face>=6 || block<=BLOCK_AIR || block>=BLOCK_COUNT || u<=0 || v<=0) return;
    int base[3]={sx,sy,sz},ua=face<4 ? 0 : 2,va=face<2 ? 2 : 1;
    float phase_u=((base[ua]%2+2)%2)*.5f,phase_v=((base[va]%2+2)%2)*.5f;
    RendVertex w[4];unsigned char vertex_light[4];
    for (int i=0;i<4;i++) {
        float p[3]={sx*.5f,sy*.5f,sz*.5f};
        float du=corners[face][i][ua]*u*.5f,dv=corners[face][i][va]*v*.5f;
        p[ua]+=du;p[va]+=dv;
        vertex_light[i]=light ? light[corners[face][i][ua]+2*corners[face][i][va]] : 255;
        /* World-aligned repeating UV: a half face uses a quarter of a PNG. */
        w[i]=(RendVertex){p[0],p[1],p[2],phase_u+du,face<2 ? phase_v+dv : -phase_v-dv};
    }
    rend3d_polygon(w,4,normals[face][0],normals[face][1],normals[face][2],0,rend_material(block,face),vertex_light);
}
