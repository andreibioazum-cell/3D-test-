/* PNG decoding only. Images are RGBA in the frame buffer's native byte order. */
#include "engine.h"
#include <limits.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_FAILURE_USERMSG
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
int image_load(AAssetManager *assets,const char *name,Image *out) {
    if (!out) return 0;
    *out=(Image){0};
    uint8_t *data; size_t size;
    if (!asset_read(assets,name,&data,&size) || size>INT_MAX) return 0;
    int w,h,channels;
    if (!stbi_info_from_memory(data,(int)size,&w,&h,&channels) || w<1 || h<1 || w>1024 || h>1024) { free(data); return 0; }
    unsigned char *rgba=stbi_load_from_memory(data,(int)size,&w,&h,&channels,4);
    free(data);
    if (!rgba) return 0;
    out->pixels=(uint32_t *)rgba; out->width=w; out->height=h;
    return 1;
}
void image_free(Image *image) {
    if (!image) return;
    stbi_image_free(image->pixels); *image=(Image){0};
}
