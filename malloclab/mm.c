/*
 * mm.c - The fastest, least memory-efficient malloc package.
 * 
 * Simple, 32-bit and 64-bit clean allocator based on explicit free
 * lists, best-fit placement, and boundary tag coalescing, as described
 * in the CS:APP3e text. Blocks must be aligned to doubleword (8 byte) 
 * boundaries. Minimum block size is 16 bytes. 
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "@junqi-xie",
    /* First member's full name */
    "Junqi Xie",
    /* First member's email address */
    "junqi_xie@outlook.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* $begin mallocmacros */
/* Basic constants and macros */
#define WSIZE 4             /* Word and header/footer size (bytes) */
#define DSIZE 8             /* Double word size (bytes) */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read and write a double word at address p */
#define GETA(p) ((void *)(*(unsigned long *)(p)))
#define PUTA(p, val) (*(unsigned long *)(p) = (unsigned long)(val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr, compute address of its header and footer */
#define HDRP(ptr) ((char *)(ptr) - WSIZE)
#define FTRP(ptr) ((char *)(ptr) + GET_SIZE(HDRP(ptr)) - DSIZE)

/* Given block ptr, compute address of next and previous blocks */
#define NEXT_BLKP(ptr) ((char *)(ptr) + GET_SIZE(((char *)(ptr) - WSIZE)))
#define PREV_BLKP(ptr) ((char *)(ptr) - GET_SIZE(((char *)(ptr) - DSIZE)))

/* Given block ptr, compute address of its predecessor and successor fields */
#define PREP(ptr) ((char *)(ptr))
#define SUCP(ptr) ((char *)(ptr) + DSIZE)

/* Given block ptr, compute address of predecessor and successor blocks */
#define PRED_BLKP(ptr) (GETA(PREP(ptr)))
#define SUCC_BLKP(ptr) (GETA(SUCP(ptr)))

/* Given block ptr, insert or delete it from the list */
#define INSERT(ptr) \
    PUTA(PREP(ptr), heap_listp); \
    PUTA(SUCP(ptr), SUCC_BLKP(heap_listp)); \
    PUTA(PREP(SUCC_BLKP(heap_listp)), ptr); \
    PUTA(SUCP(heap_listp), ptr);
#define REMOVE(ptr) \
    PUTA(PREP(SUCC_BLKP(ptr)), PRED_BLKP(ptr)); \
    PUTA(SUCP(PRED_BLKP(ptr)), SUCC_BLKP(ptr));
/* $end mallocmacros */

/* Global variables */
static char *heap_listp = 0; /* Pointer to first block */

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *ptr, size_t asize);

static void *find_fit(size_t asize);
static void *coalesce(void *ptr);

static void checkheap();
static void checkblock(void *ptr);
static void checklist(void *ptr);

/* 
 * mm_init - Initialize the memory manager 
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(7 * DSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);                          /* Alignment padding */
    heap_listp += DSIZE;                         /* Prologue (Head) */
    char *tailp = heap_listp + (3 * DSIZE);      /* Tail */

    PUT(HDRP(heap_listp), PACK((3 * DSIZE), 1)); /* Prologue header */
    PUT(FTRP(heap_listp), PACK((3 * DSIZE), 1)); /* Prologue footer */
    PUTA(PREP(heap_listp), 0);                   /* Prologue predecessor */
    PUTA(SUCP(heap_listp), tailp);               /* Prologue successor */

    PUT(HDRP(tailp), PACK((3 * DSIZE), 1));      /* Tail header */
    PUT(FTRP(tailp), PACK((3 * DSIZE), 1));      /* Tail footer */
    PUTA(PREP(tailp), heap_listp);               /* Tail predecessor */
    PUTA(SUCP(tailp), 0);                        /* Tail successor */

    PUT(HDRP(NEXT_BLKP(tailp)), PACK(0, 1));     /* Epilogue header */
   
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block with at least size bytes of payload 
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *ptr;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= (3 * DSIZE))
        asize = 4 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    /* Search the free list for a fit */
    if ((ptr = find_fit(asize)) != NULL)
    {
        place(ptr, asize);
        return ptr;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((ptr = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(ptr, asize);
    return ptr;
}

/* 
 * mm_free - Free a block 
 */
void mm_free(void *ptr)
{
    if (ptr == 0)
        return;

    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0)
    {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL)
        return mm_malloc(size);

    newptr = mm_malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if (!newptr)
        return 0;

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(ptr));
    if (size < oldsize)
        oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);

    return newptr;
}

/* 
 * mm_check - Check the heap for correctness
 */
void mm_check()
{
    checkheap();
}

/* 
 * The remaining routines are internal helper routines 
 */

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words)
{
    char *ptr;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((ptr = mem_sbrk(size)) == (void *)-1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(ptr), PACK(size, 0));         /* Free block header */
    PUT(FTRP(ptr), PACK(size, 0));         /* Free block footer */
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1)); /* New epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce(ptr);
}

/* 
 * place - Place block of asize bytes at start of free block ptr 
 *         and split if remainder would be at least minimum block size
 */
static void place(void *ptr, size_t asize)
{
    REMOVE(ptr);
    size_t csize = GET_SIZE(HDRP(ptr));

    if ((csize - asize) >= (4 * DSIZE))
    {
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));
        ptr = NEXT_BLKP(ptr);
        PUT(HDRP(ptr), PACK(csize - asize, 0));
        PUT(FTRP(ptr), PACK(csize - asize, 0));
        INSERT(ptr);
    }
    else
    {
        PUT(HDRP(ptr), PACK(csize, 1));
        PUT(FTRP(ptr), PACK(csize, 1));
    }
}

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{
    size_t fsize = __SIZE_MAX__;
    void *fptr = NULL; /* No fit */

    void *ptr;

    for (ptr = heap_listp; SUCC_BLKP(ptr); ptr = SUCC_BLKP(ptr))
    {
        size_t size = GET_SIZE(HDRP(ptr));
        if (asize <= size && size < fsize)
        {
            fptr = ptr;
            fsize = size;
        }
    }
    return fptr;
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *ptr)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));

    if (prev_alloc && !next_alloc)
    {
        REMOVE(NEXT_BLKP(ptr));
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc)
    {
        REMOVE(PREV_BLKP(ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    else if (!prev_alloc && !next_alloc)
    {
        REMOVE(PREV_BLKP(ptr));
        REMOVE(NEXT_BLKP(ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) +
                GET_SIZE(FTRP(NEXT_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }

    INSERT(ptr);
    return ptr;
}

/* 
 * checkheap - Minimal check of the heap for consistency 
 */
void checkheap()
{
    /* Check implicit free lists */
    char *ptr = heap_listp;

    if ((GET_SIZE(HDRP(heap_listp)) != (3 * DSIZE)) || !GET_ALLOC(HDRP(heap_listp)))
        printf("Bad prologue header\n");
    checkblock(heap_listp);

    for (ptr = heap_listp; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr))
        checkblock(ptr);

    if ((GET_SIZE(HDRP(ptr)) != 0) || !(GET_ALLOC(HDRP(ptr))))
        printf("Bad epilogue header\n");

    /* Check explicit free lists */
    for (ptr = SUCC_BLKP(heap_listp); SUCC_BLKP(ptr); ptr = SUCC_BLKP(ptr))
        checklist(ptr);

    if ((GET_SIZE(HDRP(ptr)) != (3 * DSIZE)) || !GET_ALLOC(HDRP(ptr)))
        printf("Bad tail header\n");
    checklist(ptr);
}

static void checkblock(void *ptr)
{
    if ((size_t)ptr % 8)
        printf("Error: %p is not doubleword aligned\n", ptr);
    if (GET(HDRP(ptr)) != GET(FTRP(ptr)))
        printf("Error: header does not match footer\n");
}

static void checklist(void *ptr)
{
    if (SUCC_BLKP(PRED_BLKP(ptr)) != ptr)
        printf("Error: consecutive pointers are not consistent");
    if (GET_ALLOC(ptr))
        printf("Error: %p in free list is allocated\n", ptr);
}
