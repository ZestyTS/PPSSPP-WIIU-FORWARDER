/****************************************************************************
 * Copyright (C) 2015 Dimok
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#include <malloc.h>
#include <string.h>
#include <wiiu/os/memory.h>
#include <wiiu/mem.h>

static MEMExpandedHeap* mem1_heap;
static MEMExpandedHeap* bucket_heap;
static BOOL mem1_frame_allocated;
static BOOL bucket_frame_allocated;
static const char *memory_initialization_error;

BOOL memoryInitialize(void)
{
   MEMHeapHandle mem1_heap_handle;
   MEMHeapHandle bucket_heap_handle;
   unsigned int mem1_allocatable_size;
   unsigned int bucket_allocatable_size;
   void *mem1_memory;
   void *bucket_memory;

   memory_initialization_error = NULL;
   mem1_heap = NULL;
   bucket_heap = NULL;
   mem1_frame_allocated = FALSE;
   bucket_frame_allocated = FALSE;

   mem1_heap_handle = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
   if (!mem1_heap_handle) {
      memory_initialization_error = "MEM1 base heap is unavailable";
      return FALSE;
   }

   mem1_allocatable_size = MEMGetAllocatableSizeForFrmHeapEx(mem1_heap_handle, 4);
   if (!mem1_allocatable_size) {
      memory_initialization_error = "MEM1 has no allocatable memory";
      return FALSE;
   }

   mem1_memory = MEMAllocFromFrmHeapEx(mem1_heap_handle, mem1_allocatable_size, 4);
   if (!mem1_memory) {
      memory_initialization_error = "MEM1 frame allocation failed";
      return FALSE;
   }
   mem1_frame_allocated = TRUE;

   mem1_heap = MEMCreateExpHeapEx(mem1_memory, mem1_allocatable_size, 0);
   if (!mem1_heap) {
      memory_initialization_error = "MEM1 expanded heap creation failed";
      memoryRelease();
      return FALSE;
   }

   bucket_heap_handle = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);
   if (!bucket_heap_handle) {
      memory_initialization_error = "foreground bucket base heap is unavailable";
      memoryRelease();
      return FALSE;
   }

   bucket_allocatable_size = MEMGetAllocatableSizeForFrmHeapEx(bucket_heap_handle, 4);
   if (!bucket_allocatable_size) {
      memory_initialization_error = "foreground bucket has no allocatable memory";
      memoryRelease();
      return FALSE;
   }

   bucket_memory = MEMAllocFromFrmHeapEx(bucket_heap_handle, bucket_allocatable_size, 4);
   if (!bucket_memory) {
      memory_initialization_error = "foreground bucket frame allocation failed";
      memoryRelease();
      return FALSE;
   }
   bucket_frame_allocated = TRUE;

   bucket_heap = MEMCreateExpHeapEx(bucket_memory, bucket_allocatable_size, 0);
   if (!bucket_heap) {
      memory_initialization_error = "foreground bucket expanded heap creation failed";
      memoryRelease();
      return FALSE;
   }

   return TRUE;
}

void memoryRelease(void)
{
   if (mem1_heap) {
      MEMDestroyExpHeap(mem1_heap);
      mem1_heap = NULL;
   }
   if (mem1_frame_allocated) {
      MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
      if (heap)
         MEMFreeToFrmHeap(heap, MEM_FRAME_HEAP_FREE_ALL);
      mem1_frame_allocated = FALSE;
   }

   if (bucket_heap) {
      MEMDestroyExpHeap(bucket_heap);
      bucket_heap = NULL;
   }
   if (bucket_frame_allocated) {
      MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);
      if (heap)
         MEMFreeToFrmHeap(heap, MEM_FRAME_HEAP_FREE_ALL);
      bucket_frame_allocated = FALSE;
   }
}

const char *memoryInitializationError(void) { return memory_initialization_error; }

u32 MEM2_avail() {
   MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);
   return heap ? MEMGetTotalFreeSizeForExpHeap(heap) : 0;
}
u32 MEM1_avail() { return mem1_heap ? MEMGetTotalFreeSizeForExpHeap(mem1_heap) : 0; }
u32 MEMBucket_avail() { return bucket_heap ? MEMGetTotalFreeSizeForExpHeap(bucket_heap) : 0; }

void* _memalign_r(struct _reent *r, size_t alignment, size_t size)
{
   return MEMAllocFromExpHeapEx(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2), size, alignment);
}

void* _malloc_r(struct _reent *r, size_t size)
{
   return _memalign_r(r, 8, size);
}

void _free_r(struct _reent *r, void *ptr)
{
   if (ptr)
      MEMFreeToExpHeap(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2), ptr);
}

size_t _malloc_usable_size_r(struct _reent *r, void *ptr)
{
   return MEMGetSizeForMBlockExpHeap(ptr);
}

void * _realloc_r(struct _reent *r, void *ptr, size_t size)
{
   void *realloc_ptr = NULL;
   if (!ptr)
      return _malloc_r(r, size);

   if (_malloc_usable_size_r(r, ptr) >= size)
      return ptr;

   realloc_ptr = _malloc_r(r, size);

   if(!realloc_ptr)
      return NULL;

   memcpy(realloc_ptr, ptr, _malloc_usable_size_r(r, ptr));
   _free_r(r, ptr);

   return realloc_ptr;
}

void* _calloc_r(struct _reent *r, size_t num, size_t size)
{
   void *ptr = _malloc_r(r, num*size);

   if(ptr)
      memset(ptr, 0, num*size);

   return ptr;
}

void * _valloc_r(struct _reent *r, size_t size)
{
   return _memalign_r(r, 64, size);
}


/* some wrappers */

void * MEM2_alloc(unsigned int size, unsigned int align)
{
   return MEMAllocFromExpHeapEx(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2), size, align);
}

void MEM2_free(void *ptr)
{
   if (ptr)
      MEMFreeToExpHeap(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2), ptr);
}

void * MEM1_alloc(unsigned int size, unsigned int align)
{
   if (!mem1_heap)
      return NULL;
   if (align < 4)
      align = 4;
   return MEMAllocFromExpHeapEx(mem1_heap, size, align);
}

void MEM1_free(void *ptr)
{
   if (ptr && mem1_heap)
      MEMFreeToExpHeap(mem1_heap, ptr);
}

void * MEMBucket_alloc(unsigned int size, unsigned int align)
{
   if (!bucket_heap)
      return NULL;
   if (align < 4)
      align = 4;
   return MEMAllocFromExpHeapEx(bucket_heap, size, align);
}

void MEMBucket_free(void *ptr)
{
   if (ptr && bucket_heap)
      MEMFreeToExpHeap(bucket_heap, ptr);
}
