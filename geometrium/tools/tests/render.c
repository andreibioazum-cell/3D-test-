/* Regressions of the public render API; no Android, GPU or .c includes. */
#include "engine/render/rend3d_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); exit(1); } } while (0)
#define PI 3.14159265358979323846f
#define GUARD 0x12345678u

/* Test-only oriented geometry: not shipped in the voxel game. */
static const unsigned char corners[6][4][3] = {
    {{0,1,0},{0,1,1},{1,1,1},{1,1,0}},
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}},
    {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},
    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}}
};
static const int normals[6][3] = {{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}};

static void test_box(float x, float y, float z, float hx, float hy, float hz, float yaw, uint32_t color) {
    if (hx <= 0 || hy <= 0 || hz <= 0 || !isfinite(x + y + z + hx + hy + hz + yaw) ||
        !rend3d_visible(x, y, z, hx, hy, hz)) return;
    float c = cosf(yaw), s = sinf(yaw);
    for (int face = 0; face < 6; face++) {
        RendVertex w[4];
        for (int i = 0; i < 4; i++) {
            float lx = (corners[face][i][0] * 2 - 1) * hx;
            float ly = (corners[face][i][1] * 2 - 1) * hy;
            float lz = (corners[face][i][2] * 2 - 1) * hz;
            w[i] = (RendVertex){x + lx * c + lz * s, y + ly, z - lx * s + lz * c, 0, 0};
        }
        float nx = normals[face][0] * c + normals[face][2] * s;
        float nz = -normals[face][0] * s + normals[face][2] * c;
        rend3d_polygon(w, 4, nx, normals[face][1], nz, color, NULL, 0);
    }
}

static Buffer make_buffer(int w, int h, int pad) {
    Buffer b = {NULL, w, h, w + pad};
    b.pixels = malloc((size_t)b.stride * h * sizeof(*b.pixels));
    CHECK(b.pixels);
    for (int i = 0; i < b.stride * h; i++) b.pixels[i] = GUARD;
    return b;
}

static void check_padding(const Buffer *b) {
    for (int y = 0; y < b->height; y++)
        for (int x = b->width; x < b->stride; x++) CHECK(b->pixels[y * b->stride + x] == GUARD);
}

static int channel(uint32_t p, int i) { return (p >> (i * 8)) & 255; }

static void test_color_fog(void) {
    Buffer b = make_buffer(320, 240, 7);
    const float distances[] = {10, 47.9f, 48.12f, 48.2f, 60, 80, 96, 100, 106};
    const uint32_t colors[] = {0xFFFFD600u, 0xFFFF00FFu, 0xFFFFFFFFu, 0xFF0000FFu};
    const int fog[] = {110, 197, 247};
    for (int sc = 1; sc <= 3; sc++) {
        for (unsigned c = 0; c < sizeof(colors) / sizeof(*colors); c++) {
            for (unsigned i = 0; i < sizeof(distances) / sizeof(*distances); i++) {
                float z = distances[i];
                CHECK(rend3d_begin(&b, sc, 0, 0, 0, 0, 0, 72));
                rend3d_sky(0xFF6EC5F7u, 0xFF6EC5F7u);
                test_box(0, 0, z, 4, 4, .12f, 0, colors[c]);
                rend3d_end();
                uint32_t pixel = b.pixels[120 * b.stride + 160];
                float t = fmaxf(0, fminf(1, (z - .12f - REND_FOG_START) / (REND_FOG_END - REND_FOG_START)));
                for (int ch = 0; ch < 3; ch++) {
                    int lit = (int)(((colors[c] >> (16 - ch * 8)) & 255) * rend_face_shade(0,0,-1));
                    int expected = (int)(lit + (fog[ch] - lit) * t);
                    CHECK(abs(channel(pixel, ch) - expected) <= 1);
                }
                CHECK((pixel >> 24) == 255);
            }
        }
    }
    check_padding(&b);
    free(b.pixels);
    puts("PASS fog: bright colors stay bounded at all distances/scales");
}

static void pattern(Buffer *b, int scale) {
    CHECK(rend3d_begin(b, scale, 0, 0, 0, .15f, -.12f, 72));
    rend3d_sky(0xFFBBDDEFu, 0xFF284658u);
    const uint32_t colors[] = {0xFFFFFFFFu, 0xFF000000u, 0xFFFFD600u, 0xFF0000FFu};
    for (int i = 0; i < 4; i++)
        test_box(-2.1f + i * 1.4f, 0, 6 + i * .3f, .55f, 1.4f, .3f, i * .31f, colors[i]);
    rend3d_end();
}

static void test_upscale(void) {
    const int sizes[][2] = {{320, 240}, {319, 241}, {403, 811}};
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(*sizes); k++) {
        for (int scale = 2; scale <= 3; scale++) {
            Buffer out = make_buffer(sizes[k][0], sizes[k][1], 9);
            Buffer src = make_buffer(out.width / scale, out.height / scale, 0);
            pattern(&src, 1);
            pattern(&out, scale);
            for (int y = 0; y < out.height; y++) {
                double fy = fmax(0, fmin(src.height - 1, (y + .5) * src.height / out.height - .5));
                int y0 = (int)fy, y1 = y0 + 1 < src.height ? y0 + 1 : y0;
                double ty = fy - y0;
                for (int x = 0; x < out.width; x++) {
                    double fx = fmax(0, fmin(src.width - 1, (x + .5) * src.width / out.width - .5));
                    int x0 = (int)fx, x1 = x0 + 1 < src.width ? x0 + 1 : x0;
                    double tx = fx - x0;
                    uint32_t actual = out.pixels[y * out.stride + x];
                    for (int c = 0; c < 3; c++) {
                        double a = channel(src.pixels[y0 * src.stride + x0], c);
                        double b = channel(src.pixels[y0 * src.stride + x1], c);
                        double d = channel(src.pixels[y1 * src.stride + x0], c);
                        double e = channel(src.pixels[y1 * src.stride + x1], c);
                        double top = a * (1 - tx) + b * tx, bot = d * (1 - tx) + e * tx;
                        int expected = (int)(top * (1 - ty) + bot * ty + .5);
                        CHECK(abs(channel(actual, c) - expected) <= 2);
                    }
                    CHECK((actual >> 24) == 255);
                }
            }
            check_padding(&out);
            free(src.pixels); free(out.pixels);
        }
    }
    puts("PASS bilinear upscale: matches signed reference, including odd sizes and stride");
}

typedef struct { float x, y, z, hx, hy, hz, yaw; uint32_t color; } Box;
static const Box red = {-.2f, 0, 8, 4, 3, .15f, .8f, 0xFFFF0000u};
static const Box blue = {.5f, 0, 9, 3, 2.6f, .12f, -.2f, 0xFF0000FFu};

static float ray_box(float dx, float dy, const Box *b) {
    float c = cosf(b->yaw), s = sinf(b->yaw);
    float o[] = {-b->x * c + b->z * s, -b->y, -b->x * s - b->z * c};
    float d[] = {dx * c - s, dy, dx * s + c};
    float h[] = {b->hx, b->hy, b->hz};
    float lo = .18f, hi = 110;
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (fabsf(o[i]) > h[i]) return INFINITY;
        } else {
            float a = (-h[i] - o[i]) / d[i], e = (h[i] - o[i]) / d[i];
            lo = fmaxf(lo, fminf(a, e)); hi = fminf(hi, fmaxf(a, e));
            if (lo > hi) return INFINITY;
        }
    }
    return lo;
}

static int expected_object(float x, float y, int w, int h) {
    float foc = h * .5f / tanf(72 * PI / 360);
    float dx = (x + .5f - w * .5f) / foc, dy = (h * .5f - y - .5f) / foc;
    float r = ray_box(dx, dy, &red), b = ray_box(dx, dy, &blue);
    return isfinite(r) && r < b ? 1 : isfinite(b) ? 2 : 0;
}

static void draw_box(const Box *b) {
    test_box(b->x, b->y, b->z, b->hx, b->hy, b->hz, b->yaw, b->color);
}

static void test_perspective_depth(void) {
    Buffer out = make_buffer(256, 192, 3);
    int checked = 0;
    for (int reverse = 0; reverse < 2; reverse++) {
        CHECK(rend3d_begin(&out, 1, 0, 0, 0, 0, 0, 72));
        rend3d_sky(0xFF004000u, 0xFF004000u);
        draw_box(reverse ? &blue : &red); draw_box(reverse ? &red : &blue);
        rend3d_end();
        for (int y = 4; y < out.height - 4; y++) {
            for (int x = 4; x < out.width - 4; x++) {
                int expected = expected_object(x, y, out.width, out.height);
                /* Skip only sub-pixel silhouette/intersection borders. */
                if (expected != expected_object(x - .8f, y, out.width, out.height) ||
                    expected != expected_object(x + .8f, y, out.width, out.height) ||
                    expected != expected_object(x, y - .8f, out.width, out.height) ||
                    expected != expected_object(x, y + .8f, out.width, out.height)) continue;
                uint32_t pixel = out.pixels[y * out.stride + x];
                int actual = channel(pixel, 0) ? 1 : channel(pixel, 2) ? 2 : 0;
                CHECK(actual == expected);
                checked++;
            }
        }
    }
    CHECK(checked > 60000);
    check_padding(&out); free(out.pixels);
    puts("PASS reciprocal depth: slanted overlapping surfaces match ray intersections in both draw orders");
}

static void test_clipping_and_camera(void) {
    Buffer out = make_buffer(160, 120, 5);
    CHECK(rend3d_begin(&out, 1, 0, 0, 0, 0, 0, 72));
    rend3d_sky(0xFF6EC5F7u, 0xFF6EC5F7u);
    test_box(0, 0, -5, 1, 1, 1, 0, 0xFFFFFFFFu);
    test_box(0, 0, 130, 1, 1, 1, 0, 0xFFFFFFFFu);
    test_box(1000, 0, 5, 1, 1, 1, 0, 0xFFFFFFFFu);
    test_box(0, -1000, 5, 1, 1, 1, 0, 0xFFFFFFFFu);
    rend3d_end();
    for (int y = 0; y < out.height; y++)
        for (int x = 0; x < out.width; x++) CHECK(out.pixels[y * out.stride + x] == 0xFFF7C56Eu);
    for (int i = 0; i < 120; i++) {
        CHECK(rend3d_begin(&out, 1, 0, 0, 0, 0, (i - 60) * .024f, 72));
        rend3d_sky(0xFF6EC5F7u, 0xFF6EC5F7u);
        test_box(0, 0, (i - 20) * .05f, .42f, .42f, .12f, i * .12f, 0xFFFFD600u);
        test_box(0, -2, 0, 32, .5f, 32, 0, 0xFF4CAF50u);
        rend3d_end();
        check_padding(&out);
    }
    float yaw = .7f, pitch = .8f;
    CHECK(rend3d_begin(&out, 1, 10, 6, -3, yaw, pitch, 72));
    rend3d_sky(0xff000000u,0xff000000u);
    test_box(10+sinf(yaw)*cosf(pitch)*20,6+sinf(pitch)*20,-3+cosf(yaw)*cosf(pitch)*20,.5f,.5f,.5f,0,0xffffffffu);
    rend3d_end();
    CHECK(channel(out.pixels[60*out.stride+80],0)>100); /* gaze vector projects onto the crosshair */
    Buffer invalid = out; invalid.stride = out.width - 1;
    CHECK(!rend3d_begin(&invalid, 1, 0, 0, 0, 0, 0, 72));
    rend3d_end(); /* must not write a stale frame after a failed begin */
    CHECK(!rend3d_visible(0,0,1,1,1,1));
    free(out.pixels);
    puts("PASS near/frustum clipping, first-person pitch direction, invalid frame guard");
}

static void test_voxel_materials(void) {
    for (int b = BLOCK_GRASS; b < BLOCK_COUNT; b++) for (int face = 0; face < 6; face++) {
        const RendMaterial *m = rend_material(b, face);
        CHECK(m && m->image.width==32 && m->image.height==32);
        for(int i=0;i<TEXTURE_SIZE*TEXTURE_SIZE;i++) {
            uint32_t c=m->image.pixels[i];
            uint32_t argb=0xff000000u|((c&255)<<16)|(c&0xff00)|((c>>16)&255);
            CHECK(m->palette[m->mip[i]]==argb); /* actual PNG colors, not generated tiles */
        }
        for (int i = 0; i < MIP_TEXELS; i++) CHECK(m->mip[i] < m->colors);
        for (int i = 0; i < m->colors; i++) CHECK((m->palette[i] >> 24) == 255);
    }
    Buffer b = make_buffer(160, 120, 5);
    CHECK(rend3d_begin(&b, 1, .5f, .5f, -2, 0, 0, 72));
    rend3d_sky(0xFF6EC5F7u, 0xFF6EC5F7u);
    rend3d_surface(0,0,0,2,2,3,BLOCK_STONE,0);
    rend3d_end();
    uint32_t colors[16]; int count = 0;
    for (int y = 48; y < 72; y++) for (int x = 68; x < 92; x++) {
        uint32_t c = b.pixels[y * b.stride + x];
        int found = 0;
        for (int i = 0; i < count; i++) if (colors[i] == c) found = 1;
        if (!found && count < 16) colors[count++] = c;
    }
    CHECK(count >= 3);
    check_padding(&b); free(b.pixels);
    puts("PASS voxel UV textures, bounded palettes and mip levels");
}

static void test_half_uv(void) {
    Buffer whole=make_buffer(256,192,3),parts=make_buffer(256,192,3);
    for(int negative=0;negative<2;negative++) for(int extent=1;extent<=2;extent++) {
        int origin=negative ? -2 : 0;
        for(int pass=0;pass<2;pass++) {
            Buffer *b=pass ? &parts : &whole;
            CHECK(rend3d_begin(b,1,origin+extent*.5f,origin+extent*.5f,origin-2,0,0,72));
            rend3d_sky(0xff6ec5f7u,0xff6ec5f7u);
            if(!pass)rend3d_surface(origin*2,origin*2,origin*2,extent*2,extent*2,3,BLOCK_DIRT,0);
            else for(int y=0;y<extent*2;y++) for(int x=0;x<extent*2;x++)
                rend3d_surface(origin*2+x,origin*2+y,origin*2,1,1,3,BLOCK_DIRT,0);
            rend3d_end();
        }
        int different=0;
        for(int y=0;y<whole.height;y++)for(int x=0;x<whole.width;x++)
            different+=whole.pixels[y*whole.stride+x]!=parts.pixels[y*parts.stride+x];
        CHECK(different<20); /* only possible texel-boundary floating-point ties */
        check_padding(&whole);check_padding(&parts);
    }
    free(whole.pixels);free(parts.pixels);
    puts("PASS half-cell UV: four quarters reconstruct a PNG; merged faces repeat at full-block scale, including negative coordinates");
}
static void test_merged_fog(void) {
    static RendMaterial flat;
    flat.colors=1;flat.palette[0]=0xff207010u;
    Buffer a=make_buffer(160,120,5),b=make_buffer(160,120,5);
    for(int pass=0;pass<2;pass++) {
        Buffer *out=pass ? &b : &a;
        CHECK(rend3d_begin(out,1,0,3,0,0,0,72));rend3d_sky(0xff6ec5f7u,0xff6ec5f7u);rend3d_fog(3,12);
        int step=pass ? 1 : 20;
        for(int z=2;z<22;z+=step)for(int x=-10;x<10;x+=step) {
            RendVertex v[4]={{x,0,z,0,0},{x,0,z+step,0,step},{x+step,0,z+step,step,step},{x+step,0,z,step,0}};
            rend3d_polygon(v,4,0,1,0,0,&flat,0);
        }
        rend3d_end();
    }
    int different=0;
    for(int y=0;y<a.height;y++)for(int x=0;x<a.width;x++) {
        uint32_t p=a.pixels[y*a.stride+x],q=b.pixels[y*b.stride+x];different+=p!=q;
        for(int c=0;c<3;c++)CHECK(abs(channel(p,c)-channel(q,c))<=4);
    }
    CHECK(different<100);
    CHECK(channel(a.pixels[85*a.stride+80],2)>channel(a.pixels[110*a.stride+80],2));
    check_padding(&a);check_padding(&b);free(a.pixels);free(b.pixels);
    puts("PASS per-pixel radial fog stays continuous across greedy / individual face boundaries");
}

static void test_smooth_light(void) {
    static RendMaterial flat;
    flat.colors=1;flat.palette[0]=0xfff0f0f0u;
    Buffer a=make_buffer(320,240,5),b=make_buffer(320,240,5);
    for(int slant=0;slant<2;slant++) {
        for(int pass=0;pass<2;pass++) {
            Buffer *out=pass ? &b : &a;
            CHECK(rend3d_begin(out,1,0,0,0,0,0,72));rend3d_sky(0xff000000u,0xff000000u);
            int pieces=pass ? 4 : 1;
            for(int i=0;i<pieces;i++) {
                float x0=-3+6.f*i/pieces,x1=-3+6.f*(i+1)/pieces;
                float z0=slant ? 6+x0 : 4,z1=slant ? 6+x1 : 4;
                RendVertex v[4]={{x0,-1,z0,0,0},{x1,-1,z1,0,0},{x1,1,z1,0,0},{x0,1,z0,0,0}};
                unsigned char lo=(unsigned char)(255.f*i/pieces+.5f),hi=(unsigned char)(255.f*(i+1)/pieces+.5f);
                unsigned char light[4]={lo,hi,hi,lo};
                rend3d_polygon(v,4,slant ? .70710678f : 0,0,slant ? -.70710678f : -1,0,&flat,light);
            }
            rend3d_end();
        }
        for(int y=0;y<a.height;y++)for(int x=0;x<a.width;x++) {
            int p=channel(a.pixels[y*a.stride+x],0),q=channel(b.pixels[y*b.stride+x],0);
            CHECK(abs(p-q)<=8); /* at most one palette level at a rounding tie */
        }
        int shades=0,last=-1;
        for(int x=0;x<a.width;x++) {
            int p=channel(a.pixels[120*a.stride+x],0);
            if(!p)continue;
            CHECK(p>=last);if(p!=last)shades++;last=p;
        }
        CHECK(shades>=20);
    }
    const uint32_t *dark=rend_material_shades(&flat,1,0xffffffffu);
    CHECK(channel(dark[(FOG_LEVELS-1)*PALETTE_SIZE],0)<20);
    const uint32_t *day=rend_material_shades(&flat,LIGHT_LEVELS-1,0xffffffffu);
    CHECK(channel(day[(FOG_LEVELS-1)*PALETTE_SIZE],0)==255);
    check_padding(&a);check_padding(&b);free(a.pixels);free(b.pixels);
    puts("PASS perspective-correct smooth vertex light, consistent split quads, bounded light palettes and non-glowing cave fog");
}

int main(void) {
    screen_w=320;screen_h=240;
    CHECK(rend_materials_load(host_asset_manager("assets")));
    test_color_fog();
    test_upscale();
    test_perspective_depth();
    test_clipping_and_camera();
    test_voxel_materials();
    test_half_uv();test_merged_fog();test_smooth_light();
    return 0;
}
