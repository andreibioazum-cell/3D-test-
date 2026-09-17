#ifndef GFX_INTERNAL_H
#define GFX_INTERNAL_H
#include "engine.h"
#include "ttf/ttf_internal.h"
uint32_t gfx_pack(uint32_t c);
uint32_t gfx_blend(uint32_t dest,uint32_t src);
int gfx_cl_floor(float v,int limit);
int gfx_cl_ceil(float v,int limit);
void gfx_fill_span(uint32_t *d,int n,uint32_t c);
void gfx_paint_span(uint32_t *d,int n,uint32_t c);
void gfx_clear(Buffer *b,uint32_t c);
void gfx_render_rect(Buffer *b,float x,float y,float w,float h,uint32_t c);
void gfx_render_roundrect(Buffer *b,float x,float y,float w,float h,float r,uint32_t c);
void gfx_render_circle(Buffer *b,float x,float y,float r,uint32_t c);
void gfx_render_ring(Buffer *b,float x,float y,float r,float th,uint32_t c);
void gfx_render_line(Buffer *b,float x,float y,float x2,float y2,float th,uint32_t c);
AAssetManager *gfx_assets(void);
int gfx_font_ensure(void);
void gfx_font_release(void);
int gfx_utf8_decode(const char **cursor);
void gfx_render_text(Buffer *b,const char *s,float x,float y,uint32_t c,float scale);
Buffer *gfx_current_buffer(void);
#endif
