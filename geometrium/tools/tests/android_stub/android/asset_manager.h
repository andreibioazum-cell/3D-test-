#ifndef STUB_ASSET_MANAGER_H
#define STUB_ASSET_MANAGER_H
#include <stddef.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;
enum { AASSET_MODE_UNKNOWN = 0, AASSET_MODE_RANDOM = 1,
       AASSET_MODE_STREAMING = 2, AASSET_MODE_BUFFER = 3 };
AAsset *AAssetManager_open(AAssetManager *, const char *, int);
int AAsset_read(AAsset *, void *, size_t);
off_t AAsset_getLength(AAsset *);
void AAsset_close(AAsset *);
#ifdef __cplusplus
}
#endif
#endif
