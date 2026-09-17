/* Preview transport only; native Android renders straight to its window. */
#include "engine.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "third_party/stb_image_write.h"
typedef struct { unsigned char *data;size_t length,capacity;int failed; } Encoded;
static Encoded encoded;
static void append(void *context,void *data,int length) {
    Encoded *e=context;
    if(e->failed || length<=0)return;
    size_t need=e->length+(size_t)length;
    if(need>e->capacity) {
        size_t cap=e->capacity ? e->capacity*2 : 65536;
        if(cap<need)cap=need;
        unsigned char *p=realloc(e->data,cap);
        if(!p){e->failed=1;return;}
        e->data=p;e->capacity=cap;
    }
    memcpy(e->data+e->length,data,(size_t)length);e->length=need;
}
const unsigned char *preview_jpeg(const Buffer *frame,size_t *length) {
    if(!frame || !frame->pixels || frame->width!=frame->stride || frame->width<1 || frame->height<1)return NULL;
    encoded.length=0;encoded.failed=0;
    int ok=stbi_write_jpg_to_func(append,&encoded,frame->width,frame->height,4,frame->pixels,88);
    if(!ok || encoded.failed)return NULL;
    *length=encoded.length;return encoded.data;
}
