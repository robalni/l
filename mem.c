struct MemHeader {
    size_t size;
    size_t top;
};
typedef struct MemHeader MemHeader;

struct Mem {
    MemHeader* last;
};
typedef struct Mem Mem;

static MemHeader*
mem_new_segment(Mem* mem, size_t space_needed) {
    space_needed += sizeof (MemHeader);
    size_t size = space_needed > 4096 ? space_needed : 4096;
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void* ptr = mmap(NULL, size, prot, flags, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
    }
    MemHeader* h = (MemHeader*)ptr;
    *h = (MemHeader){
        .size = size,
        .top = 0,
    };
    mem->last = h;
    return h;
}

#define mem_alloc(mem, type) ((type*)mem_alloc_size(mem, sizeof (type)))

static void*
mem_alloc_size(Mem* mem, size_t size) {
    if (mem->last == NULL) {
        mem_new_segment(mem, size);
    }
    if (mem->last->top + size >= mem->last->size) {
        mem_new_segment(mem, size);
    }
    void* ptr = (char*)mem->last + sizeof (MemHeader) + mem->last->top;
    mem->last->top += size;
    return ptr;
}
