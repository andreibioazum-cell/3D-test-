/* Read real PNG assets once, then build palette mipmaps and cached lighting. */
#include "rend3d_internal.h"
#include <limits.h>
#include <stdio.h>
enum { GRASS_TOP,GRASS_SIDE,DIRT,STONE,SAND,WATER,BARK,RINGS,LEAVES,MATERIALS };
/* Water blends over whatever the opaque pass drew: see-through lakes and
 * waterfalls instead of the old opaque blue sheet. */
#define WATER_ALPHA 140
static RendMaterial materials[MATERIALS];
static int initialized;
static int color_index(RendMaterial *m,uint32_t color) {
    for (int i=0;i<m->colors;i++) if (m->palette[i]==color) return i;
    if (m->colors<PALETTE_SIZE) {m->palette[m->colors]=color;return m->colors++;}
    int best=0,distance=INT_MAX;
    for (int i=0;i<m->colors;i++) {
        int dr=(int)((color>>16)&255)-(int)((m->palette[i]>>16)&255);
        int dg=(int)((color>>8)&255)-(int)((m->palette[i]>>8)&255);
        int db=(int)(color&255)-(int)(m->palette[i]&255);
        int d=dr*dr+dg*dg+db*db;
        if (d<distance) {distance=d;best=i;}
    }
    return best;
}
static void build_mips(RendMaterial *m) {
    int source=0,dest=TEXTURE_SIZE*TEXTURE_SIZE;
    for (int size=TEXTURE_SIZE;size>1;size/=2) {
        int next=size/2;
        for (int y=0;y<next;y++) for (int x=0;x<next;x++) {
            int r=0,g=0,b=0;
            for (int dy=0;dy<2;dy++) for (int dx=0;dx<2;dx++) {
                uint32_t c=m->palette[m->mip[source+(y*2+dy)*size+x*2+dx]];
                r+=(c>>16)&255;g+=(c>>8)&255;b+=c&255;
            }
            uint32_t color=0xff000000u|((uint32_t)(r/4)<<16)|((uint32_t)(g/4)<<8)|(uint32_t)(b/4);
            m->mip[dest+y*next+x]=(unsigned char)color_index(m,color);
        }
        source=dest;dest+=next*next;
    }
}
int rend_materials_load(AAssetManager *assets) {
    if (initialized) return 1;
    static const char *names[MATERIALS]={"grass_top","grass_side","dirt","stone","sand","water","log_side","log_top","leaves"};
    for (int t=0;t<MATERIALS;t++) {
        char path[80];snprintf(path,sizeof(path),"textures/%s.png",names[t]);
        RendMaterial *m=&materials[t];
        image_free(&m->image);memset(m,0,sizeof(*m));
        if (!image_load(assets,path,&m->image)) {app_fail("Could not load %s",path);return 0;}
        if (m->image.width!=TEXTURE_SIZE || m->image.height!=TEXTURE_SIZE) {
            app_fail("%s: needs a %dx%d PNG",path,TEXTURE_SIZE,TEXTURE_SIZE);return 0;
        }
        for (int i=0;i<TEXTURE_SIZE*TEXTURE_SIZE;i++) {
            uint32_t c=m->image.pixels[i]; /* framebuffer RGBA to ARGB */
            uint32_t color=0xff000000u|((c&255)<<16)|(c&0xff00)|((c>>16)&255);
            m->mip[i]=(unsigned char)color_index(m,color);
        }
        build_mips(m);
    }
    materials[WATER].alpha=WATER_ALPHA;
    initialized=1;return 1;
}
static int texture(int block,int face) {
    switch (block) {
        case BLOCK_GRASS:return face==0 ? GRASS_TOP : face==1 ? DIRT : GRASS_SIDE;
        case BLOCK_DIRT:return DIRT;
        case BLOCK_SAND:return SAND;
        case BLOCK_WATER:return WATER;
        case BLOCK_LOG:return face<2 ? RINGS : BARK;
        case BLOCK_LEAVES:return LEAVES;
        default:return STONE;
    }
}
RendMaterial *rend_material(int block,int face) { return initialized ? &materials[texture(block,face)] : NULL; }
const Image *rend_material_icon(int block) { return initialized ? &materials[texture(block,block==BLOCK_LOG ? 2 : 0)].image : NULL; }
const uint32_t *rend_material_shades(RendMaterial *m,int shade,uint32_t fog) {
    if (shade<0) shade=0;
    if (shade>=LIGHT_LEVELS) shade=LIGHT_LEVELS-1;
    if (m->fog_color!=fog) {m->fog_color=fog;m->ready=0;}
    if (!(m->ready&(1u<<shade))) {
        float light=(float)shade/(LIGHT_LEVELS-1);
        /* Distant cave surfaces stay dark instead of fading to a glowing sky. */
        float fog_light=fminf(1,light*2);
        int fr=(int)((fog&255)*fog_light),fg=(int)(((fog>>8)&255)*fog_light),fb=(int)(((fog>>16)&255)*fog_light);
        for (int level=0;level<FOG_LEVELS;level++) for (int i=0;i<PALETTE_SIZE;i++) {
            uint32_t c=m->palette[i];
            int r=(int)(((c>>16)&255)*light),g=(int)(((c>>8)&255)*light),b=(int)((c&255)*light);
            r+=(fr-r)*level/(FOG_LEVELS-1);g+=(fg-g)*level/(FOG_LEVELS-1);b+=(fb-b)*level/(FOG_LEVELS-1);
            m->shades[shade][level*PALETTE_SIZE+i]=0xff000000u|(uint32_t)r|((uint32_t)g<<8)|(uint32_t)b<<16;
        }
        m->ready|=1u<<shade;
    }
    return m->shades[shade];
}
