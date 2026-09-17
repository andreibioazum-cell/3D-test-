/* Software 3D: perspective cubes with a z-buffer, clipping and fog. */
#include "rend3d_internal.h"
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NEAR_Z 0.08f
#define FAR_Z REND_FAR_Z

typedef struct {float x,y,z,u,v,light;} V3;

static Buffer *dst;
static uint32_t *pix;
/* Screen space is linear in 1/z, not in the depth z itself. */
static float *zbuf, *ray_length;
static int ray_w, ray_h;
static float ray_foc;
static int rw, rh, scale;
static size_t cap;
typedef struct { int lo, hi; uint32_t weight; } Sample;
static Sample *xsample;
static uint32_t *horizontal_rows;
static int horizontal_cap;
static int sample_cap, sample_w, sample_rw;
static float camx, camy, camz;
static float yaw_s, yaw_c, pitch_s, pitch_c;
static float foc, view_x, view_y, side_x, side_y;
static uint32_t fog_rgb;
static float fog_a, fog_b;
static int viewmodel; /* camera-space overlay (the hand): no world transform */

static uint32_t pack(uint32_t c) {
    uint32_t a = (c >> 24) & 0xff, r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    if (!a) a = 255;
    return r | (g << 8) | (b << 16) | (a << 24);
}

static uint32_t shade_fog(uint32_t packed, float z, float shade) {
    uint32_t r = packed & 0xff, g = (packed >> 8) & 0xff, b = (packed >> 16) & 0xff;
    int ir = (int)(r * shade), ig = (int)(g * shade), ib = (int)(b * shade);
    if (ir > 255) ir = 255;
    if (ig > 255) ig = 255;
    if (ib > 255) ib = 255;
    float t = viewmodel ? 0 : (z - fog_a) * fog_b;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    /* Signed channels: subtracting from uint32_t overflowed into rainbows. */
    float fl=fminf(1,shade*2);
    int fr=(int)((fog_rgb&255)*fl),fg=(int)(((fog_rgb>>8)&255)*fl),fb=(int)(((fog_rgb>>16)&255)*fl);
    ir = (int)(ir + (fr - ir) * t);
    ig = (int)(ig + (fg - ig) * t);
    ib = (int)(ib + (fb - ib) * t);
    return (uint32_t)ir | ((uint32_t)ig << 8) | ((uint32_t)ib << 16) | 0xff000000u;
}

int rend3d_begin(Buffer *b, int sc, float cx, float cy, float cz, float yaw, float pitch, float fov_deg) {
    dst = NULL;viewmodel=0;
    if (!b || !b->pixels || b->width <= 0 || b->height <= 0 || b->stride < b->width ||
        !isfinite(cx + cy + cz + yaw + pitch + fov_deg) || fov_deg < 5 || fov_deg > 175)
        return 0;
    scale = sc < 1 ? 1 : sc;
    rw = b->width / scale;
    rh = b->height / scale;
    if (rw < 8 || rh < 8 || rw > INT_MAX / rh) return 0;
    size_t need = (size_t)rw * rh;
    if (need > SIZE_MAX / sizeof(float) || need > SIZE_MAX / sizeof(uint32_t)) return 0;
    if (need > cap) {
        uint32_t *np = (uint32_t *)realloc(pix, need * sizeof(*pix));
        if (!np) return 0;
        pix = np; /* no dangling pointer if the second realloc fails */
        float *nz = (float *)realloc(zbuf, need * sizeof(*zbuf));
        if (!nz) return 0;
        zbuf = nz;
        float *nr = realloc(ray_length,need*sizeof(*nr));
        if (!nr) return 0;
        ray_length=nr;
        cap = need;
    }
    if (scale>1 && ((size_t)b->width>SIZE_MAX/sizeof(Sample) || (size_t)b->width>SIZE_MAX/sizeof(uint32_t)/2)) return 0;
    if (scale>1 && b->width>horizontal_cap) {
        uint32_t *rows=realloc(horizontal_rows,(size_t)b->width*2*sizeof(*rows));
        if(!rows)return 0;
        horizontal_rows=rows;horizontal_cap=b->width;
    }
    if (scale > 1 && b->width > sample_cap) {
        Sample *ns = (Sample *)realloc(xsample, (size_t)b->width * sizeof(*xsample));
        if (!ns) return 0;
        xsample = ns;
        sample_cap = b->width;
    }
    camx = cx; camy = cy; camz = cz;
    yaw_s = sinf(yaw); yaw_c = cosf(yaw);
    pitch_s = sinf(pitch); pitch_c = cosf(pitch);
    float fov = fov_deg * (float)M_PI / 180.0f;
    foc = (0.5f * (float)rh) / tanf(fov * 0.5f);
    view_x = 0.5f * rw / foc;
    view_y = 0.5f * rh / foc;
    side_x = sqrtf(1.0f + view_x * view_x);
    side_y = sqrtf(1.0f + view_y * view_y);
    if (ray_w!=rw || ray_h!=rh || ray_foc!=foc) {
        for (int y=0;y<rh;y++) for (int x=0;x<rw;x++) {
            float dx=(x+.5f-rw*.5f)/foc,dy=(y+.5f-rh*.5f)/foc;
            ray_length[y*rw+x]=sqrtf(1+dx*dx+dy*dy);
        }
        ray_w=rw;ray_h=rh;ray_foc=foc;
    }
    dst = b;
    return 1;
}

void rend3d_fog(float start,float end) {
    if (!isfinite(start+end) || start<0 || end<=start) return;
    fog_a=start;fog_b=1/(end-start);
}
void rend3d_sky(uint32_t top, uint32_t bot) {
    if (!dst) return;
    uint32_t t = pack(top), b = pack(bot);
    fog_rgb = b;
    fog_a = REND_FOG_START;
    fog_b = 1.0f / (REND_FOG_END - REND_FOG_START);
    int tr = t & 0xff, tg = (t >> 8) & 0xff, tb = (t >> 16) & 0xff;
    int br = b & 0xff, bg = (b >> 8) & 0xff, bb = (b >> 16) & 0xff;
    float horizon = rh * .5f + foc * pitch_s / fmaxf(.05f, pitch_c);
    for (int y = 0; y < rh; y++) {
        float k = fmaxf(0, fminf(1, 1 - (horizon - y) / (rh * .9f)));
        k = k * k * (3.0f - 2.0f * k);
        int r = (int)(tr + (br - tr) * k);
        int g = (int)(tg + (bg - tg) * k);
        int bl = (int)(tb + (bb - tb) * k);
        uint32_t c = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bl << 16) | 0xff000000u;
        uint32_t *row = pix + y * rw;
        for (int x = 0; x < rw; x++) row[x] = c;
        float *zr = zbuf + y * rw;
        for (int x = 0; x < rw; x++) zr[x] = 1.0f / FAR_Z;
    }
}

void rend3d_viewmodel(int enabled) {viewmodel=enabled!=0;}
static int to_view(float wx, float wy, float wz, V3 *o) {
    if (viewmodel) {*o=(V3){wx,wy,wz,0,0,0};return wz>.01f;}
    float dx = wx - camx, dy = wy - camy, dz = wz - camz;
    float rx = dx * yaw_c - dz * yaw_s;          /* right */
    float rz = dx * yaw_s + dz * yaw_c;          /* forward */
    float ry = dy;
    o->x = rx;
    o->u = o->v = o->light = 0;
    /* Positive pitch looks up, matching the camera and flight vectors. */
    o->y = ry * pitch_c - rz * pitch_s;
    o->z = ry * pitch_s + rz * pitch_c;
    return o->z > 0.01f;
}

static int project_v(V3 v, float *sx, float *sy) {
    if (v.z < 0.04f) return 0;
    float iz = foc / v.z;
    *sx = (float)rw * 0.5f + v.x * iz;
    *sy = (float)rh * 0.5f - v.y * iz;
    return 1;
}

static V3 lerp3(V3 a, V3 b, float t) {
    V3 r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    r.u = a.u + (b.u - a.u) * t;
    r.v = a.v + (b.v - a.v) * t;
    r.light = a.light + (b.light - a.light) * t;
    return r;
}

/* Clip before projecting: even next to a plane there are no huge screen
 * coordinates and no stray full-screen triangles. */
static float plane_distance(V3 v, int plane) {
    switch (plane) {
        case 0: return v.z - NEAR_Z;
        case 1: return FAR_Z - v.z;
        case 2: return v.z * view_x + v.x;
        case 3: return v.z * view_x - v.x;
        case 4: return v.z * view_y + v.y;
        default: return v.z * view_y - v.y;
    }
}

static int clip_plane(const V3 *in, int n, V3 *out, int plane) {
    int m = 0;
    V3 a = in[n - 1];
    float da = plane_distance(a, plane);
    for (int i = 0; i < n; i++) {
        V3 b = in[i];
        float db = plane_distance(b, plane);
        if ((da >= 0) != (db >= 0)) {
            V3 v = lerp3(a, b, da / (da - db));
            if (plane == 0) v.z = NEAR_Z;
            else if (plane == 1) v.z = FAR_Z;
            out[m++] = v;
        }
        if (db >= 0) out[m++] = b;
        a = b; da = db;
    }
    return m;
}

typedef struct { float x, y, iz, u, v, light; } ScreenV;
typedef struct {
    uint32_t color;
    const uint32_t *palette;
    const uint32_t (*shades)[FOG_LEVELS*PALETTE_SIZE];
    const unsigned char *texels;
    float plane;
    int fog,smooth,alpha;
} Paint;

/* Transparent pixels (water): weighted mix over the framebuffer instead of a
 * plain store. Weights are 0..256, rounding matches the opaque upscalers. */
static uint32_t blend_alpha(uint32_t d, uint32_t s, uint32_t a) {
    uint32_t inv = 256 - a;
    uint32_t rb = (((d & 0x00ff00ffu) * inv + (s & 0x00ff00ffu) * a + 0x00800080u) >> 8) & 0x00ff00ffu;
    uint32_t g = (((d & 0x0000ff00u) * inv + (s & 0x0000ff00u) * a + 0x00008000u) >> 8) & 0x0000ff00u;
    return rb | g | 0xff000000u;
}

static ScreenV mix_vertex(ScreenV a, ScreenV b, float t) {
    ScreenV r = {a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.iz + (b.iz-a.iz)*t,
                 a.u + (b.u-a.u)*t, a.v + (b.v-a.v)*t,a.light+(b.light-a.light)*t};
    return r;
}
static void span(int y, ScreenV a, ScreenV b, const Paint *paint) {
    if (a.x>b.x) {ScreenV t=a;a=b;b=t;}
    float left=ceilf(a.x-.5f),right=ceilf(b.x-.5f);
    int i0=(int)(left<0 ? 0 : left>rw ? rw : left),i1=(int)(right<0 ? 0 : right>rw ? rw : right);
    if (i0>=i1) return;
    float inv=b.x-a.x>1e-6f ? 1/(b.x-a.x) : 0;
    float diz=(b.iz-a.iz)*inv,offset=i0+.5f-a.x,iz=a.iz+offset*diz;
    uint32_t *row=pix+y*rw;float *zr=zbuf+y*rw;
    if (!paint->texels) {
        for (int x=i0;x<i1;x++,iz+=diz) if (iz>zr[x]) {zr[x]=iz;row[x]=paint->color;}
        return;
    }
    float du=(b.u-a.u)*inv,dv=(b.v-a.v)*inv,u=a.u+offset*du,v=a.v+offset*dv;
    float dl=(b.light-a.light)*inv,light=a.light+offset*dl;
    const float *ray=ray_length+y*rw;
    static const int offsets[6]={1364,1360,1344,1280,1024,0};
    /* Fine LOD step (8 instead of 16): less blocky pixelation in the distance. */
    for (int start=i0;start<i1;start+=8) {
        int end=start+8<i1 ? start+8 : i1;
        float mid=iz+diz*(end-start-1)*.5f;
        float angle=paint->plane*mid;
        float pixels=foc*mid*(angle<1 ? angle : 1);
        union {float f;uint32_t u;} bits={pixels};
        int exponent=(int)((bits.u>>23)&255)-127;
        if (exponent<0) exponent=0;
        if (exponent>5) exponent=5;
        int shift=5-exponent;
        const unsigned char *texels=paint->texels+offsets[exponent];
        /* Near/uniform surfaces take a lean loop. Specialization changes no
         * texel, rounding or depth math; smooth/foggy pixels retain the full path. */
#define DRAW_PIXELS(FOG,SMOOTH,ALPHA) \
    for (int x=start;x<end;x++,iz+=diz,u+=du,v+=dv,light+=(SMOOTH)?dl:0) { \
        if (iz<=zr[x]) continue; \
        float depth=1/iz,fu=u*depth,fv=v*depth; \
        int iu=(int)fu,iv=(int)fv; \
        iu-=fu<iu;iv-=fv<iv; /* bounded floor, including negative UV */ \
        int tx=(iu&31)>>shift,ty=(iv&31)>>shift; \
        int color=texels[(ty<<exponent)+tx]; \
        if (FOG) { \
            int level=(int)((depth*ray[x]-fog_a)*fog_b*(FOG_LEVELS-1)+.5f); \
            if (level<0) level=0; \
            if (level>=FOG_LEVELS) level=FOG_LEVELS-1; \
            color+=level*PALETTE_SIZE; \
        } \
        const uint32_t *palette=paint->palette; \
        if (SMOOTH) { \
            int shade=(int)(light*depth+.5f); \
            if (shade<0) shade=0; \
            if (shade>=LIGHT_LEVELS) shade=LIGHT_LEVELS-1; \
            palette=paint->shades[shade]; \
        } \
        if (ALPHA) row[x]=blend_alpha(row[x],palette[color],(uint32_t)paint->alpha); \
        else row[x]=palette[color]; \
        zr[x]=iz; \
    }
        if (paint->alpha) {
            if (paint->smooth) {
                if (paint->fog) {DRAW_PIXELS(1,1,1)} else {DRAW_PIXELS(0,1,1)}
            } else {
                if (paint->fog) {DRAW_PIXELS(1,0,1)} else {DRAW_PIXELS(0,0,1)}
            }
        } else if (paint->smooth) {
            if (paint->fog) {DRAW_PIXELS(1,1,0)} else {DRAW_PIXELS(0,1,0)}
        } else {
            if (paint->fog) {DRAW_PIXELS(1,0,0)} else {DRAW_PIXELS(0,0,0)}
        }
#undef DRAW_PIXELS
    }
}
static void fill_tri(ScreenV a, ScreenV b, ScreenV c, const Paint *paint) {
    if (a.y > b.y) { ScreenV t = a; a = b; b = t; }
    if (a.y > c.y) { ScreenV t = a; a = c; c = t; }
    if (b.y > c.y) { ScreenV t = b; b = c; c = t; }
    if (c.y - a.y < 1e-6f) return;
    int ys = (int)fmaxf(0, ceilf(a.y - .5f));
    int ye = (int)fminf((float)rh, ceilf(c.y - .5f));
    float inv_long=1/(c.y-a.y),inv_top=b.y-a.y>1e-6f ? 1/(b.y-a.y) : 0,inv_bot=c.y-b.y>1e-6f ? 1/(c.y-b.y) : 0;
    for (int y = ys; y < ye; y++) {
        float fy = y + .5f;
        ScreenV left = mix_vertex(a, c, (fy-a.y)*inv_long);
        ScreenV lo = fy < b.y ? a : b, hi = fy < b.y ? b : c;
        if (hi.y - lo.y < 1e-6f) continue;
        ScreenV right = mix_vertex(lo, hi, (fy-lo.y)*(fy<b.y ? inv_top : inv_bot));
        span(y, left, right, paint);
    }
}

void rend3d_polygon(const RendVertex *w, int n, float nx, float ny, float nz,
                   uint32_t color, RendMaterial *material, const unsigned char *light) {
    if (!dst || n<3 || n>8) return;
    float ex=viewmodel ? 0 : camx,ey=viewmodel ? 0 : camy,ez=viewmodel ? 0 : camz;
    float plane=nx*(ex-w[0].x)+ny*(ey-w[0].y)+nz*(ez-w[0].z);
    if (plane<=0) return;
    V3 buffers[2][16],*in=buffers[0],*out=buffers[1];
    float wx=0,wy=0,wz=0,max_distance2=0;
    float face=rend_face_shade(nx,ny,nz),lo=LIGHT_LEVELS,hi=0;
    unsigned clip=0,all=63;
    for (int i=0;i<n;i++) {
        to_view(w[i].x,w[i].y,w[i].z,&in[i]);
        in[i].u=w[i].u;in[i].v=w[i].v;
        float ambient=light ? light[i]/255.f : 1;
        in[i].light=face*(REND_DARK_FLOOR+(1-REND_DARK_FLOOR)*ambient)*(LIGHT_LEVELS-1);
        if (in[i].light<lo) lo=in[i].light;
        if (in[i].light>hi) hi=in[i].light;
        if (!material) {wx+=in[i].x;wy+=in[i].y;wz+=in[i].z;}
        float d2=in[i].x*in[i].x+in[i].y*in[i].y+in[i].z*in[i].z;
        if (d2>max_distance2) max_distance2=d2;
        V3 v=in[i];
        unsigned code=(v.z<NEAR_Z) | ((v.z>FAR_Z)<<1) | ((v.z*view_x+v.x<0)<<2) |
            ((v.z*view_x-v.x<0)<<3) | ((v.z*view_y+v.y<0)<<4) | ((v.z*view_y-v.y<0)<<5);
        clip|=code;all&=code;
    }
    if (all) return;
    float distance=material ? 0 : sqrtf(wx*wx+wy*wy+wz*wz)/n;
    /* Most visible voxel quads need no clipping or temporary polygon copies. */
    for (int p=0;p<6;p++) if (clip&(1u<<p)) {
        n=clip_plane(in,n,out,p);
        if (n<3) return;
        V3 *swap=in;in=out;out=swap;
    }
    Paint paint={0};
    if (material) {
        int first=(int)fmaxf(0,lo+.5f),last=(int)fminf(LIGHT_LEVELS-1,hi+.5f);
        if (first>=LIGHT_LEVELS) first=LIGHT_LEVELS-1;
        if (last<first) last=first;
        for (int level=first;level<=last;level++) rend_material_shades(material,level,fog_rgb);
        paint.palette=material->shades[first];paint.shades=material->shades;
        paint.smooth=first!=last;
        paint.texels=material->mip;paint.plane=plane;
        paint.alpha=material->alpha;
        paint.fog=!viewmodel && max_distance2>fog_a*fog_a;
    } else paint.color=shade_fog(pack(color),distance,(lo+hi)*.5f/(LIGHT_LEVELS-1));
    ScreenV v[16];
    for (int i=0;i<n;i++) {
        if (!project_v(in[i],&v[i].x,&v[i].y)) return;
        v[i].iz=1/in[i].z;
        v[i].u=in[i].u*TEXTURE_SIZE*v[i].iz;
        v[i].v=in[i].v*TEXTURE_SIZE*v[i].iz;
        v[i].light=in[i].light*v[i].iz;
    }
    for (int i=1;i<n-1;i++) fill_tri(v[0],v[i],v[i+1],&paint);
}
int rend3d_face_visible(int face,float plane) {
    float eye=face<2 ? camy : face<4 ? camz : camx;
    return dst && ((face&1) ? eye<plane : eye>plane);
}

int rend3d_visible(float x, float y, float z, float hx, float hy, float hz) {
    if (!dst || !isfinite(x+y+z+hx+hy+hz)) return 0;
    V3 center;
    to_view(x, y, z, &center);
    float radius = sqrtf(hx*hx + hy*hy + hz*hz);
    float far=fog_a+1/fog_b;
    if (center.x*center.x+center.y*center.y+center.z*center.z>(far+radius)*(far+radius)) return 0;
    return center.z + radius >= NEAR_Z && center.z - radius <= FAR_Z &&
           fabsf(center.x) - center.z*view_x <= radius*side_x &&
           fabsf(center.y) - center.z*view_y <= radius*side_y;
}

/* Screen coordinates of a point in the active space (world or viewmodel);
 * 0 when it is behind the near plane. */
int rend3d_project(float x, float y, float z, float *sx, float *sy) {
    if (!dst || !sx || !sy || !isfinite(x + y + z)) return 0;
    V3 v;
    to_view(x, y, z, &v);
    return project_v(v, sx, sy);
}

/* Overlay above the world (the hand): its screen region always passes the
 * z-test, yet the overlay parts still occlude each other correctly. */
void rend3d_depth_clear(float x0, float y0, float x1, float y1) {
    if (!dst) return;
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
    int ix0 = (int)fmaxf(0, floorf(x0)), iy0 = (int)fmaxf(0, floorf(y0));
    int ix1 = (int)fminf((float)rw, ceilf(x1)), iy1 = (int)fminf((float)rh, ceilf(y1));
    if (ix1 <= ix0 || iy1 <= iy0) return;
    for (int y = iy0; y < iy1; y++)
        memset(zbuf + (size_t)y*rw + ix0, 0, (size_t)(ix1-ix0)*sizeof(*zbuf));
}

/* Near-to-far segmented 3D line (block outlines); the 1% z-bias keeps it
 * readable against faces it grazes. */
void rend3d_segment(float x,float y,float z,float x2,float y2,float z2,uint32_t color) {
    if (!dst || !isfinite(x+y+z+x2+y2+z2)) return;
    V3 a,b;to_view(x,y,z,&a);to_view(x2,y2,z2,&b);
    for (int p=0;p<6;p++) {
        float da=plane_distance(a,p),db=plane_distance(b,p);
        if (da<0 && db<0) return;
        if ((da<0)!=(db<0)) {
            V3 v=lerp3(a,b,da/(da-db));
            if(p==0)v.z=NEAR_Z;
            if (da<0) a=v;else b=v;
        }
    }
    float ax,ay,bx,by;
    if (!project_v(a,&ax,&ay) || !project_v(b,&bx,&by)) return;
    int steps=(int)ceilf(fmaxf(fabsf(bx-ax),fabsf(by-ay)));if(steps<1)steps=1;
    /* Larger bias so the outline does not half-disappear into z-fighting. */
    for (int i=0;i<=steps;i++) {
        float t=(float)i/steps,iz=(1-t)/a.z+t/b.z;
        int px=(int)floorf(ax+(bx-ax)*t),py=(int)floorf(ay+(by-ay)*t);
        if (px>=0 && py>=0 && px<rw && py<rh && iz+iz*.010f>=zbuf[py*rw+px])
            pix[py*rw+px]=pack(color);
    }
}

/* Weighted sum instead of unsigned (b-a): channels cannot overflow.
 * R/B are handled together, G separately; weights 0..256 keep the RGB range. */
static uint32_t mix_rgb(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t inv = 256 - t;
    uint32_t rb = (((a & 0x00ff00ffu) * inv + (b & 0x00ff00ffu) * t + 0x00800080u) >> 8) & 0x00ff00ffu;
    uint32_t g = (((a & 0x0000ff00u) * inv + (b & 0x0000ff00u) * t + 0x00008000u) >> 8) & 0x0000ff00u;
    return rb | g | 0xff000000u;
}

/* The common phone 2x upscale has fixed 3:1 weights. Add/shift arithmetic
 * replaces per-pixel multiplies and X table reads, with identical Q8 rounding. */
static uint32_t mix_quarter(uint32_t a,uint32_t b) {
    uint32_t rb=(((a&0x00ff00ffu)*3+(b&0x00ff00ffu)+0x00020002u)>>2)&0x00ff00ffu;
    uint32_t g=(((a&0x0000ff00u)*3+(b&0x0000ff00u)+0x00000200u)>>2)&0x0000ff00u;
    return rb|g|0xff000000u;
}
static void expand_two(const uint32_t *in,uint32_t *out) {
    out[0]=in[0];out[rw*2-1]=in[rw-1];
    for (int x=0;x<rw-1;x++) {
        out[x*2+1]=mix_quarter(in[x],in[x+1]);out[x*2+2]=mix_quarter(in[x+1],in[x]);
    }
}
static void upscale_two(void) {
    int w=dst->width,stride=dst->stride;
    expand_two(pix,horizontal_rows);
    memcpy(dst->pixels,horizontal_rows,(size_t)w*sizeof(uint32_t));
    for (int y=0;y<rh-1;y++) {
        const uint32_t *a=horizontal_rows+(y&1)*w;
        uint32_t *b=horizontal_rows+((y+1)&1)*w;
        expand_two(pix+(y+1)*rw,b);
        uint32_t *out0=dst->pixels+(y*2+1)*stride,*out1=out0+stride;
        for (int x=0;x<w;x++) {out0[x]=mix_quarter(a[x],b[x]);out1[x]=mix_quarter(b[x],a[x]);}
    }
    memcpy(dst->pixels+(dst->height-1)*stride,horizontal_rows+((rh-1)&1)*w,(size_t)w*sizeof(uint32_t));
}

static Sample sample_at(int pos, int source, int target) {
    float f = ((float)pos + 0.5f) * source / target - 0.5f;
    if (f < 0) f = 0;
    if (f > source - 1) f = (float)(source - 1);
    int lo = (int)f;
    Sample s = {lo, lo + 1 < source ? lo + 1 : lo, (uint32_t)((f - lo) * 256 + 0.5f)};
    return s;
}

/* Bilinear upscale without expensive float math per pixel.
 * The X table is recomputed only when the resolution changes. */
static void upscale_smooth(void) {
    int W = dst->width, H = dst->height, st = dst->stride;
    if (sample_w != W || sample_rw != rw) {
        for (int x = 0; x < W; x++) xsample[x] = sample_at(x, rw, W);
        sample_w = W; sample_rw = rw;
    }
    int cached[2]={-1,-1};
    for (int y=0;y<H;y++) {
        Sample sy=sample_at(y,rh,H);
        const uint32_t *expanded[2];
        /* Horizontal interpolation is shared by adjacent destination rows.
         * The Q8 rounding is bit-identical to the direct bilinear formula. */
        for (int i=0;i<2;i++) {
            int source=i ? sy.hi : sy.lo,index=source&1;
            uint32_t *row=horizontal_rows+index*W;
            if (cached[index]!=source) {
                const uint32_t *in=pix+source*rw;
                for (int x=0;x<W;x++) {
                    Sample sx=xsample[x];row[x]=mix_rgb(in[sx.lo],in[sx.hi],sx.weight);
                }
                cached[index]=source;
            }
            expanded[i]=row;
        }
        uint32_t *out=dst->pixels+y*st;
        for (int x=0;x<W;x++) out[x]=mix_rgb(expanded[0][x],expanded[1][x],sy.weight);
    }
}

void rend3d_end(void) {
    if (!dst || !pix) return;
    int H = dst->height, st = dst->stride;
    if (scale == 1) {
        for (int y = 0; y < rh && y < H; y++)
            memcpy(dst->pixels + y * st, pix + y * rw, (size_t)rw * 4);
        return;
    }
    if (dst->width==rw*2 && dst->height==rh*2) upscale_two();
    else upscale_smooth();
}
