#if !defined(RUNTIME_HEAP_H)
#define RUNTIME_HEAP_H

#include <heaplayers>
#include <shuffleheap.h>

#include "Util.h"
#include "MMapSource.h"

enum {
    DataShuffle = 256,
    DataProt = PROT_READ | PROT_WRITE,
    DataFlags = MAP_PRIVATE | MAP_ANONYMOUS,
    DataSize = 0x2000000,
    
    CodeShuffle = 256,
    CodeProt = PROT_READ | PROT_WRITE | PROT_EXEC,
    CodeFlags = MAP_PRIVATE | MAP_ANONYMOUS,
    CodeSize = 0x2000000
};

class DataSource : public SizeHeap<FreelistHeap<BumpAlloc<DataSize, MMapSource<DataProt, DataFlags>, 16> > > {};
class CodeSource : public SizeHeap<FreelistHeap<BumpAlloc<CodeSize, MMapSource<CodeProt, CodeFlags>, CODE_ALIGN> > > {};
    
// Restore DieHard's shuffling layer for heap/code placement randomization.
// Modern `ShuffleHeap` takes (ChunkSize, MaxSize, SuperHeap). We use a shuffled
// KingsleyHeap, preserving the original `DataShuffle`/`CodeShuffle` tunables as
// the maximum shuffled object size.

// ShuffleHeap::malloc() bypasses its own shuffle buffer for requests bigger
// than MaxSize, calling SuperHeap::malloc() (KingsleyHeap) directly. But
// ShuffleHeap::free() has no matching bypass: it always computes a Kingsley
// bin index for the pointer's real size and swaps it into that bin's
// shuffle buffer -- even for bins malloc() never filled because every
// request that size took the bypass. For any object over MaxSize bytes,
// that swap pulls a null pointer out of a never-filled buffer slot and
// hands it to SuperHeap::free(), which crashes reading its size header.
// ShuffleFreeGuard restores malloc()'s bypass for free(), routing anything
// over MaxSize straight to the unshuffled heap underneath ShuffleHeap
// instead of through ShuffleHeap's shuffle bookkeeping.
template <size_t MaxSize, class ShuffledHeap, class UnshuffledHeap>
class ShuffleFreeGuard : public ShuffledHeap {
public:
    inline void free(void* ptr) {
        if (ShuffledHeap::getSize(ptr) > MaxSize) {
            UnshuffledHeap::free(ptr);
        } else {
            ShuffledHeap::free(ptr);
        }
    }
};

typedef ANSIWrapper<ShuffleFreeGuard<DataShuffle, ShuffleHeap<4096, DataShuffle, KingsleyHeap<DataSource, DataSource> >, KingsleyHeap<DataSource, DataSource> > > DataHeapType;
typedef ANSIWrapper<ShuffleHeap<4096, CodeShuffle, KingsleyHeap<CodeSource, CodeSource> > > CodeHeapType;
    
DataHeapType* getDataHeap();
CodeHeapType* getCodeHeap();

#endif
