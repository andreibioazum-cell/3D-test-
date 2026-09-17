/* Заглушка для хост-тестов. */
#ifndef HOST_STUB_ANDROID_ASSET_H
#define HOST_STUB_ANDROID_ASSET_H
#include <stddef.h>
#include <sys/types.h>
typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;
enum { AASSET_MODE_BUFFER = 3 };
AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename, int mode);
off_t AAsset_getLength(AAsset *asset);
int AAsset_read(AAsset *asset, void *buf, size_t count);
int AAsset_close(AAsset *asset);
#endif
