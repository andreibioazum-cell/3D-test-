/* Asset reads shared by fonts, PNG textures and sound. */
#include "engine.h"
int asset_read(AAssetManager *assets,const char *name,uint8_t **data,size_t *size) {
    if (!assets || !name || !data || !size) return 0;
    *data=NULL; *size=0;
    AAsset *a=AAssetManager_open(assets,name,AASSET_MODE_BUFFER);
    if (!a) return 0;
    off_t len=AAsset_getLength(a);
    if (len<=0 || (uint64_t)len>32u*1024u*1024u) { AAsset_close(a); return 0; }
    uint8_t *bytes=malloc((size_t)len);
    if (!bytes) { AAsset_close(a); return 0; }
    size_t offset=0;
    while (offset<(size_t)len) {
        int n=AAsset_read(a,bytes+offset,(size_t)len-offset);
        if (n<=0) break;
        offset+=(size_t)n;
    }
    AAsset_close(a);
    if (offset!=(size_t)len) { free(bytes); return 0; }
    *data=bytes; *size=offset;
    return 1;
}
