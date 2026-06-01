# Memory Allocator From Scratch

This project is a beginner-friendly implementation of a tiny memory allocator in C.

The goal is not to beat the real C standard library. The goal is to understand what functions like `malloc`, `free`, `calloc`, `realloc`, and `memcpy` are doing under the hood.

The implementation lives in `sys_call.c`.

## What This Project Implements

This file contains simple versions of:

```c
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t num, size_t nsize);
void *realloc(void *ptr, size_t size);
void *memcpy(void *dest, const void *src, size_t n);
```

The allocator uses:

- `sbrk` to grow and shrink the heap.
- A hidden header before every allocation.
- A linked list to track all allocated blocks.
- A global mutex to avoid two threads changing allocator state at the same time.

## Program Memory Layout

When a C program runs, its memory is usually divided into sections:

```text
+------------------+
| text/code         |
+------------------+
| initialized data  |
+------------------+
| BSS               |
+------------------+
| heap              | grows upward
+------------------+
|                  |
| free space        |
|                  |
+------------------+
| stack             | grows downward
+------------------+
```

The heap is where dynamic allocation happens. When you call `malloc`, memory usually comes from the heap.

The current end of the heap is called the program break.

This project uses:

```c
sbrk(0)
```

to get the current program break.

It uses:

```c
sbrk(n)
```

to move the program break forward by `n` bytes.

It uses:

```c
sbrk(-n)
```

to move the program break backward by `n` bytes.

On failure, `sbrk` returns:

```c
(void *)-1
```

On macOS, `sbrk` is deprecated. That is fine for this project because this is for learning allocator mechanics, not for production use.

## Why A Header Is Needed

The first simple version of `malloc` could look like this:

```c
void *malloc(size_t size)
{
    return sbrk(size);
}
```

But this is not enough.

The problem appears when we want to implement `free`.

If the user calls:

```c
int *arr = malloc(20);
free(arr);
```

How does `free` know that `arr` was 20 bytes?

The pointer alone does not contain the allocation size.

So this allocator stores metadata before the pointer that gets returned to the user.

## Block Header

The header type is:

```c
typedef char ALIGN[16];

typedef union block {
    struct {
        size_t size;
        unsigned is_free;
        union block *next;
    } s;
    ALIGN st;
} block;
```

The header stores:

- `size`: number of user bytes requested.
- `is_free`: whether this block can be reused.
- `next`: pointer to the next block in the allocator linked list.

The union also contains:

```c
ALIGN st;
```

This helps keep the header aligned to 16 bytes. Alignment matters because different CPU types expect certain data types to start at certain memory addresses.

## Block Layout

Each allocation looks like this in memory:

```text
+----------------------+----------------------+
| block header         | user memory          |
+----------------------+----------------------+
^                      ^
|                      |
internal pointer       pointer returned to user
```

The allocator sees the full block.

The user only sees the memory after the header.

That is why `malloc` returns:

```c
return (void *)(block + 1);
```

If `block` points to the header, then `block + 1` points just after the header.

## Linked List Of Blocks

The allocator keeps track of all blocks with:

```c
block *head, *tail;
```

`head` points to the first block.

`tail` points to the last block.

Each block points to the next block:

```text
head
 |
 v
+---------+      +---------+      +---------+
| block 1 | ---> | block 2 | ---> | block 3 |
+---------+      +---------+      +---------+
                                      ^
                                      |
                                     tail
```

This linked list is needed because `malloc` must be able to search for free blocks, and `free` must be able to update the allocator's list.

## Finding A Free Block

The helper function:

```c
block *free_block_size(size_t size)
```

walks through the linked list:

```c
block *curr = head;
while(curr){
    if(curr->s.is_free && curr->s.size >= size){
        return curr;
    }
    curr = curr->s.next;
}
return NULL;
```

This uses a first-fit strategy.

That means it returns the first free block that is large enough.

It does not search for the smallest possible block. It does not split large blocks. It simply finds a usable block and returns it.

## `malloc`

`malloc(size)` allocates `size` bytes and returns a pointer to usable memory.

The implementation has two paths:

1. Reuse an old free block if possible.
2. Ask the operating system for more heap memory if no reusable block exists.

The main logic is:

```c
block *free = free_block_size(size);
if(free){
    free->s.is_free = 0;
    return (void *)(free + 1);
}
```

If a free block exists, it is marked as used and returned.

If not, the allocator grows the heap:

```c
t_size = sizeof(block) + size;
block *block = sbrk(t_size);
```

The total size is:

```text
header size + user requested size
```

Then the metadata is filled:

```c
block->s.size = size;
block->s.is_free = 0;
block->s.next = NULL;
```

Finally, the new block is added to the linked list.

## `free`

`free(ptr)` releases or marks memory as reusable.

The first step is getting back to the hidden header:

```c
block *header = (block *)ptr - 1;
```

This works because `ptr` points just after the header.

Then the allocator checks whether this block is at the very end of the heap:

```c
programbreak = sbrk(0);
if((char*)ptr + header->s.size == programbreak){
    ...
}
```

The cast to `char *` matters.

Pointer arithmetic on `char *` moves byte by byte, so:

```c
(char *)ptr + header->s.size
```

means:

```text
start of user memory + number of user bytes
```

If that equals the program break, the block is the last block in the heap.

When the block is last, the allocator can shrink the heap:

```c
sbrk(0 - sizeof(block) - header->s.size);
```

If the block is not last, the allocator cannot return it to the operating system because there may be other allocated blocks after it.

So it only does:

```c
header->s.is_free = 1;
```

That means a future `malloc` can reuse it.

## Freeing vs Releasing

In this allocator, freeing and releasing are different ideas.

Freeing means:

```text
mark this block as reusable
```

Releasing means:

```text
give this memory back by shrinking the heap
```

Only the last block can be released with `sbrk`.

A middle block can only be marked free.

Example:

```text
+---------+---------+---------+
| block A | block B | block C |
+---------+---------+---------+
```

If `block B` is freed, the heap cannot shrink because `block C` is after it.

So `block B` becomes reusable, but the program break stays where it is.

If `block C` is freed, the heap can shrink because it is at the end.

## `calloc`

`calloc(num, nsize)` allocates memory for an array and fills it with zero bytes.

The total size is:

```c
size = num * nsize;
```

Then it calls:

```c
block = malloc(size);
```

Then it clears the allocated memory:

```c
memset(block, 0, size);
```

Example:

```c
int *arr = calloc(5, sizeof(int));
```

This creates space for 5 integers and initializes all bytes to zero.

Learning note: a safer `calloc` should check for multiplication overflow before doing `num * nsize`.

## `memcpy`

`memcpy(dest, src, n)` copies `n` bytes from `src` to `dest`.

The implementation is:

```c
void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    for(size_t i = 0; i < n; i++){
        d[i] = s[i];
    }

    return dest;
}
```

It uses `unsigned char *` because one `char` is one byte.

So this loop copies memory byte by byte.

Important limitation: this does not handle overlapping memory correctly.

For example:

```c
memcpy(arr + 1, arr, 4);
```

may behave incorrectly if the source and destination overlap.

The standard function for overlapping memory is `memmove`.

## Why `#undef memcpy` Is Used

The file includes:

```c
#include <string.h>
```

On macOS, `memcpy` may be defined as a macro for extra checking.

That can break this custom function definition:

```c
void *memcpy(...)
```

So the code uses:

```c
#ifdef memcpy
#undef memcpy
#endif
```

This removes the macro definition and allows the file to define its own `memcpy` function.

## `realloc`

`realloc(ptr, size)` changes the size of an existing allocation.

The implementation handles these cases:

```c
if(!ptr){
    return malloc(size);
}
```

If the pointer is `NULL`, `realloc` behaves like `malloc`.

```c
if(!size){
    free(ptr);
    return NULL;
}
```

If the new size is zero, the allocation is freed.

Then it gets the old header:

```c
block *header = (block *)ptr - 1;
```

If the old block is already large enough, the same pointer is returned:

```c
if(header->s.size >= size){
    return ptr;
}
```

Otherwise, a new allocation is created:

```c
void *ret = malloc(size);
```

The old data is copied:

```c
memcpy(ret, ptr, header->s.size);
```

Then the old block is freed:

```c
free(ptr);
```

Finally, the new pointer is returned.

## Correct `realloc` Usage

The return value of `realloc` is important.

This is wrong:

```c
realloc(arr, 7);
```

There are two problems:

1. The return value is ignored.
2. `7` means 7 bytes, not 7 integers.

Use:

```c
arr = realloc(arr, 7 * sizeof(int));
```

This asks for enough space for 7 integers and stores the possibly new pointer back into `arr`.

## Thread Locking

The allocator has one global mutex:

```c
pthread_mutex_t global_malloc_lock = PTHREAD_MUTEX_INITIALIZER;
```

`malloc` and `free` lock this mutex before changing shared allocator state.

This protects:

- `head`
- `tail`
- block `next` pointers
- block `is_free` flags

This is a simple approach. Real allocators usually use more advanced strategies because one global lock can become slow when many threads allocate memory at the same time.

## Build And Run

Compile:

```sh
cc -Wall -Wextra -pthread sys_call.c -o sys_call
```

Run:

```sh
./sys_call
```

Expected demo output starts like:

```text
ARRAY[0] = 0
ARRAY[1] = 0
ARRAY[2] = 0
ARRAY[3] = 0
ARRAY[4] = 0
```

Warnings about `sbrk` being deprecated on macOS are expected.

## Important Limitations

This allocator is intentionally small and incomplete.

It does not:

- split a large free block into smaller blocks
- merge adjacent free blocks
- validate pointers passed to `free`
- detect double-free bugs
- protect against `calloc` multiplication overflow
- support `memcpy` with overlapping source and destination
- use `mmap` for large allocations
- return middle free blocks to the operating system
- behave exactly like a production C allocator

That is okay. The purpose here is to learn the main ideas:

- heap growth
- program break
- metadata headers
- pointer arithmetic
- linked lists
- reusing freed blocks
- copying bytes
- resizing allocations

## Next Improvements

Good next steps:

1. Add overflow checking in `calloc`.
2. Fix the demo to assign the result of `realloc`.
3. Add a `memset` implementation instead of using the library one.
4. Add block splitting.
5. Add adjacent block coalescing.
6. Add tests for `malloc`, `calloc`, `realloc`, and `free`.

This project is a nice small window into how much hidden work happens behind one line like:

```c
int *arr = malloc(10 * sizeof(int));
```
