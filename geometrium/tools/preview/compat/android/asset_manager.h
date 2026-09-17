/* Host replacement for <android/asset_manager.h> in the PC preview.
 * Same API, but reads files from disk under the asset root. */
#ifndef HOST_COMPAT_ASSET_MANAGER_H
#define HOST_COMPAT_ASSET_MANAGER_H
#include <stddef.h>
#include <sys/types.h>

typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;

enum { AASSET_MODE_UNKNOWN = 0, AASSET_MODE_BUFFER = 3 };

AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename, int mode);
off_t AAsset_getLength(AAsset *asset);
int AAsset_read(AAsset *asset, void *buf, size_t count);
void AAsset_close(AAsset *asset);

/* host only */
AAssetManager *host_asset_manager(const char *root);

#endif
