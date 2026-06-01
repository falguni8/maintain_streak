#include <unistd.h>
#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#ifdef memcpy
#undef memcpy
#endif
/*
all c function from scratch coding to understand what is happening on standard functions
*/

typedef char ALIGN[16];

typedef union block{
    struct{
        size_t size;
        unsigned is_free;
        union block *next;
    }s;
    ALIGN st;
} block;

block *head,*tail;

pthread_mutex_t global_malloc_lock = PTHREAD_MUTEX_INITIALIZER;

block* free_block_size(size_t size){
    block *curr =head;
    while(curr){
        if(curr->s.is_free  && curr->s.size >= size){
            return curr;
        }
        curr=curr->s.next;
    }
    return NULL;
    
}

void *malloc(size_t size){
    if(!size){
        return NULL;
    }
    size_t t_size;
    pthread_mutex_lock(&global_malloc_lock);
    block* free=free_block_size(size);
    if(free){
        free->s.is_free=0;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void *)(free + 1);
    }
    t_size=sizeof(block) + size;
    block *block=sbrk(t_size);
    if(block == (void *)-1){
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }
    block->s.size=size;
    block->s.is_free=0;
    block->s.next=NULL;
    if(!head){
        head=block;
    }
    if(tail){
        tail->s.next=block;
    }
    tail=block;
    pthread_mutex_unlock(&global_malloc_lock);
    return (void *)(block+1);

}

void free(void *ptr){

    if(!ptr){
        return;
    }

    pthread_mutex_lock(&global_malloc_lock);
    block *header = (block *)ptr - 1;
    void *programbreak;
    block *tmp;
    programbreak=sbrk(0);
    if((char*)ptr+ header->s.size == programbreak){
       if(head==tail){
            head=tail=NULL;
       }
       else{
            tmp=head;
            while(tmp){
                if(tmp->s.next==tail){
                    tmp->s.next=NULL;
                    tail=tmp;
                }
                tmp=tmp->s.next;
            }
       }
       sbrk(0 - sizeof(block) - header->s.size);
       pthread_mutex_unlock(&global_malloc_lock);
       return;
    }
    header->s.is_free=1;
    pthread_mutex_unlock(&global_malloc_lock);

}

void *calloc(size_t num, size_t nsize){
    size_t size;
    void *block;
    if(!num || !nsize){
        return NULL;
    }
    size=num*nsize;
    block=malloc(size);
    if(!block){
        return NULL;
    }
    memset(block, 0, size);
    return block;
}

void *memcpy(void *dest, const void *src, size_t n){
    unsigned char *d = dest;
    const unsigned char *s = src;

    for(size_t i = 0; i < n; i++){
        d[i] = s[i];
    }

    return dest;
}

void *realloc(void *ptr, size_t size){
    if(!ptr){
        return malloc(size);
    }
    if(!size){
        free(ptr);
        return NULL;
    }
    block *header=(block *)ptr - 1;
    if(header->s.size>=size){
        return ptr;
    }
    void* ret=malloc(size);
    if(ret){
        memcpy(ret, ptr, header->s.size);
        free(ptr);
    }
    return ret;

}

int main(void){
    int *arr=calloc(5, sizeof(int));
    for(int i=0;i<5;i++){
        printf("ARRAY[%d] = %d\n", i, arr[i]);
    }
    realloc(arr,7);
    for(int i=0;i<7;i++){
        printf("ARRAY[%d] = %d\n", i, arr[i]);
    }
    return 0;
}
