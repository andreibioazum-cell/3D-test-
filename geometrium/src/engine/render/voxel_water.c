/* Bounded, event-driven voxel water. Generated/placed water is a source;
 * flowing cells remember their distance, never silently become new sources.
 * Evaluate a tick before applying it: water advances through faces, not diagonals
 * or a recursive flood fill in a single frame. No whole-world scans per frame. */
#include "voxel_internal.h"

enum { QUEUE_CAP=32768,HASH_CAP=65536,TICK_BUDGET=512,RECOVERY_BUDGET=8 };
#define WATER_STEP .10f
static const int dirs[6][3]={{0,-1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0}};
typedef struct {int x,y,z,next;} Pending;
typedef struct {int x,y,z,level;} Change;
static Pending queue[QUEUE_CAP];
static int buckets[HASH_CAP],head,count,overflow,recovery,last_work;
static float timer;

static unsigned hash(int x,int y,int z) {
    uint32_t h=(uint32_t)x*0x8da6b343u^(uint32_t)y*0xd8163841u^(uint32_t)z*0xcb1ab31fu;
    h^=h>>16;return h&(HASH_CAP-1);
}
static void enqueue(int x,int y,int z) {
    if (y<2 || y>=WORLD_HEIGHT*2 || x< -20000000 || x>20000000 || z< -20000000 || z>20000000) return;
    unsigned h=hash(x,y,z);
    for (int i=buckets[h];i>=0;i=queue[i].next)
        if (queue[i].x==x && queue[i].y==y && queue[i].z==z) return;
    if (count==QUEUE_CAP) {overflow=1;return;}
    int index=(head+count)&(QUEUE_CAP-1);
    queue[index]=(Pending){x,y,z,buckets[h]};buckets[h]=index;count++;
}
static Pending pop(void) {
    Pending p=queue[head];
    int *link=&buckets[hash(p.x,p.y,p.z)];
    while (*link!=head) link=&queue[*link].next;
    *link=p.next;head=(head+1)&(QUEUE_CAP-1);count--;
    return p;
}
void voxel_water_reset(void) {
    for (int i=0;i<HASH_CAP;i++) buckets[i]=-1;
    head=count=overflow=last_work=0;recovery=-1;timer=0;
}
int voxel_water_level(int sx,int sy,int sz) {
    if (voxel_world_cell(sx,sy,sz)!=BLOCK_WATER) return -1;
    int x=voxel_floor_div(sx,2),y=voxel_floor_div(sy,2),z=voxel_floor_div(sz,2);
    const VoxelEdit *e=voxel_edit_find(x,y,z);
    return e ? e->flow[sx-x*2+(sy-y*2)*2+(sz-z*2)*4] : 0;
}
void voxel_water_wake(int x,int y,int z) {
    int above=voxel_world_cell(x,y+1,z)==BLOCK_WATER;
    int wet=above || voxel_world_cell(x,y,z)==BLOCK_WATER;
    for (int i=0;!wet && i<6;i++) wet=voxel_world_cell(x+dirs[i][0],y+dirs[i][1],z+dirs[i][2])==BLOCK_WATER;
    if (!wet) return;
    enqueue(x,y,z);
    for (int i=0;i<6;i++) enqueue(x+dirs[i][0],y+dirs[i][1],z+dirs[i][2]);
    /* Lateral flow reads the support beneath its neighbour. Changing that
     * support must wake the four cells around the water above, even if the
     * source itself remains unchanged (opening/closing a drain in a floor). */
    if (above) for (int i=1;i<=4;i++) enqueue(x+dirs[i][0],y+1,z+dirs[i][2]);
}
static void activate_edit(const VoxelEdit *e) {
    /* Also wakes dry edited holes: an adjacent original lake must refill them. */
    for (int i=0;i<8;i++) {
        int x=e->x*2+(i&1),y=e->y*2+((i>>1)&1),z=e->z*2+(i>>2);
        if (voxel_world_active(x,z)) voxel_water_wake(x,y,z);
    }
}
void voxel_water_activate(int cx,int cz) {
    for (const VoxelEdit *e=voxel_edit_first(cx,cz);e;e=voxel_edit_next(e)) activate_edit(e);
}
static int supported(int x,int y,int z) {
    int b=voxel_world_cell(x,y-1,z);
    return (b!=BLOCK_AIR && b!=BLOCK_WATER) || (b==BLOCK_WATER && voxel_water_level(x,y-1,z)==0);
}
static int desired(int x,int y,int z,int old) {
    if (old==0) return 0; /* only genuine sources are persistent */
    if (voxel_world_cell(x,y+1,z)==BLOCK_WATER) return VOXEL_WATER_FALLING;
    int best=VOXEL_WATER_REACH+1;
    for (int i=1;i<=4;i++) {
        int nx=x+dirs[i][0],nz=z+dirs[i][2];
        int level=voxel_water_level(nx,y,nz);
        if (level<0 || !supported(nx,y,nz)) continue;
        if (level==VOXEL_WATER_FALLING) level=0; /* a waterfall spreads at its foot */
        if (level+1<best) best=level+1;
    }
    return best<=VOXEL_WATER_REACH ? best : -1;
}
void voxel_water_update(float d) {
    last_work=0;
    if (!isfinite(d) || d<=0) return;
    timer+=fminf(d,.05f);
    if (timer+.000001f<WATER_STEP) return;
    timer=fmaxf(0,timer-WATER_STEP);
    /* Overflow cannot permanently strand a flow. Retry edited parents in small
     * batches after pressure drops; ordinary frames never walk this index. */
    if (overflow && recovery<0 && count<QUEUE_CAP/2) {recovery=0;overflow=0;}
    /* Eight parents can enqueue at most 320 distinct dependent cells: recovery
     * alone cannot outgrow the 512-cell consumer and repeatedly overflow. */
    if (recovery>=0) {
        for (int i=0;i<RECOVERY_BUDGET;i++) {
            const VoxelEdit *e=voxel_edit_at(recovery++);
            if (!e) {recovery=-1;break;}
            activate_edit(e);
        }
    }
    Change changes[TICK_BUDGET];int n=0,jobs=count<TICK_BUDGET ? count : TICK_BUDGET;
    for (int i=0;i<jobs;i++) {
        Pending p=pop();last_work++;
        if (!voxel_world_active(p.x,p.z) || voxel_cell_solid(p.x,p.y,p.z)) continue;
        int old=voxel_water_level(p.x,p.y,p.z),next=desired(p.x,p.y,p.z,old);
        if (old!=next) changes[n++]=(Change){p.x,p.y,p.z,next};
    }
    for (int i=0;i<n;i++) {
        Change p=changes[i];
        voxel_world_water_set(p.x,p.y,p.z,p.level);
    }
}
int voxel_water_pending(void) {return count+(overflow || recovery>=0);}
int voxel_water_last_work(void) {return last_work;}
