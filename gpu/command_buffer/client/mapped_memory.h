// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_CLIENT_MAPPED_MEMORY_H_
#define GPU_COMMAND_BUFFER_CLIENT_MAPPED_MEMORY_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/bind.h"
#include "base/macros.h"
#include "base/trace_event/memory_dump_provider.h"
#include "gpu/command_buffer/client/fenced_allocator.h"
#include "gpu/command_buffer/common/buffer.h"
#include "gpu/gpu_export.h"

#if defined(CASTANETS)
#include "base/command_line.h"
#include "mojo/public/cpp/system/platform_handle.h"
#endif

namespace gpu {

class CommandBufferHelper;

// Manages a shared memory segment.
class GPU_EXPORT MemoryChunk {
 public:
  MemoryChunk(int32_t shm_id,
              scoped_refptr<gpu::Buffer> shm,
              CommandBufferHelper* helper);
  ~MemoryChunk();

  // Gets the size of the largest free block that is available without waiting.
  unsigned int GetLargestFreeSizeWithoutWaiting() {
    return allocator_.GetLargestFreeSize();
  }

  // Gets the size of the largest free block that can be allocated if the
  // caller can wait.
  unsigned int GetLargestFreeSizeWithWaiting() {
    return allocator_.GetLargestFreeOrPendingSize();
  }

  // Gets the size of the chunk.
  unsigned int GetSize() const {
    return static_cast<unsigned int>(shm_->size());
  }

  // The shared memory id for this chunk.
  int32_t shm_id() const {
    return shm_id_;
  }

  gpu::Buffer* shared_memory() const { return shm_.get(); }

  // Allocates a block of memory. If the buffer is out of directly available
  // memory, this function may wait until memory that was freed "pending a
  // token" can be re-used.
  //
  // Parameters:
  //   size: the size of the memory block to allocate.
  //
  // Returns:
  //   the pointer to the allocated memory block, or NULL if out of
  //   memory.
  void* Alloc(unsigned int size) {
    return allocator_.Alloc(size);
  }

  // Gets the offset to a memory block given the base memory and the address.
  // It translates NULL to FencedAllocator::kInvalidOffset.
  unsigned int GetOffset(void* pointer) {
    return allocator_.GetOffset(pointer);
  }

  // Frees a block of memory.
  //
  // Parameters:
  //   pointer: the pointer to the memory block to free.
  void Free(void* pointer) {
    allocator_.Free(pointer);
  }

  // Frees a block of memory, pending the passage of a token. That memory won't
  // be re-allocated until the token has passed through the command stream.
  //
  // Parameters:
  //   pointer: the pointer to the memory block to free.
  //   token: the token value to wait for before re-using the memory.
  void FreePendingToken(void* pointer, unsigned int token) {
    allocator_.FreePendingToken(pointer, token);
#if defined(CASTANETS)
    if (std::string("renderer") ==
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("type")) {
      mojo::SyncSharedMemoryHandle(shm_->backing()->GetGUID(),
                                   GetOffset(pointer),
                                   allocator_.GetBlockSize(pointer));
    }
#endif
  }

  // Frees any blocks whose tokens have passed.
  void FreeUnused() {
    allocator_.FreeUnused();
  }

  // Gets the free size of the chunk.
  unsigned int GetFreeSize() { return allocator_.GetFreeSize(); }

  // Returns true if pointer is in the range of this block.
  bool IsInChunk(void* pointer) const {
    return pointer >= shm_->memory() &&
           pointer <
               reinterpret_cast<const int8_t*>(shm_->memory()) + shm_->size();
  }

  // Returns true of any memory in this chunk is in use or free pending token.
  bool InUseOrFreePending() { return allocator_.InUseOrFreePending(); }

  size_t bytes_in_use() const {
    return allocator_.bytes_in_use();
  }

  FencedAllocator::State GetPointerStatusForTest(void* pointer,
                                                 int32_t* token_if_pending) {
    return allocator_.GetPointerStatusForTest(pointer, token_if_pending);
  }

 private:
  int32_t shm_id_;
  scoped_refptr<gpu::Buffer> shm_;
  FencedAllocatorWrapper allocator_;

  DISALLOW_COPY_AND_ASSIGN(MemoryChunk);
};

// Manages MemoryChunks.
class GPU_EXPORT MappedMemoryManager {
 public:
  enum MemoryLimit {
    kNoLimit = 0,
  };

  // |unused_memory_reclaim_limit|: When exceeded this causes pending memory
  // to be reclaimed before allocating more memory.
  MappedMemoryManager(CommandBufferHelper* helper,
                      size_t unused_memory_reclaim_limit);

  ~MappedMemoryManager();

  unsigned int chunk_size_multiple() const {
    return chunk_size_multiple_;
  }

  void set_chunk_size_multiple(unsigned int multiple) {
    DCHECK(multiple % FencedAllocator::kAllocAlignment == 0);
    chunk_size_multiple_ = multiple;
  }

  size_t max_allocated_bytes() const {
    return max_allocated_bytes_;
  }

  void set_max_allocated_bytes(size_t max_allocated_bytes) {
    max_allocated_bytes_ = max_allocated_bytes;
  }

  // Allocates a block of memory
  // Parameters:
  //   size: size of memory to allocate.
  //   shm_id: pointer to variable to receive the shared memory id.
  //   shm_offset: pointer to variable to receive the shared memory offset.
  // Returns:
  //   pointer to allocated block of memory. NULL if failure.
  void* Alloc(
      unsigned int size, int32_t* shm_id, unsigned int* shm_offset);

  // Frees a block of memory.
  //
  // Parameters:
  //   pointer: the pointer to the memory block to free.
  void Free(void* pointer);

  // Frees a block of memory, pending the passage of a token. That memory won't
  // be re-allocated until the token has passed through the command stream.
  //
  // Parameters:
  //   pointer: the pointer to the memory block to free.
  //   token: the token value to wait for before re-using the memory.
  void FreePendingToken(void* pointer, int32_t token);

  // Free Any Shared memory that is not in use.
  void FreeUnused();

  // Dump memory usage - called from GLES2Implementation.
  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd);

  // Used for testing
  size_t num_chunks() const {
    return chunks_.size();
  }

  size_t bytes_in_use() const {
    size_t bytes_in_use = 0;
    for (size_t ii = 0; ii < chunks_.size(); ++ii) {
      bytes_in_use += chunks_[ii]->bytes_in_use();
    }
    return bytes_in_use;
  }

  // Used for testing
  size_t allocated_memory() const {
    return allocated_memory_;
  }

  // Gets the status of a previous allocation, as well as the corresponding
  // token if FREE_PENDING_TOKEN (and token_if_pending is not null).
  FencedAllocator::State GetPointerStatusForTest(void* pointer,
                                                 int32_t* token_if_pending);

 private:
  typedef std::vector<std::unique_ptr<MemoryChunk>> MemoryChunkVector;

  // size a chunk is rounded up to.
  unsigned int chunk_size_multiple_;
  CommandBufferHelper* helper_;
  MemoryChunkVector chunks_;
  size_t allocated_memory_;
  size_t max_free_bytes_;
  size_t max_allocated_bytes_;
  // A process-unique ID used for disambiguating memory dumps from different
  // mapped memory manager.
  int tracing_id_;

  DISALLOW_COPY_AND_ASSIGN(MappedMemoryManager);
};

// A class that will manage the lifetime of a mapped memory allocation
class GPU_EXPORT ScopedMappedMemoryPtr {
 public:
  ScopedMappedMemoryPtr(
      uint32_t size,
      CommandBufferHelper* helper,
      MappedMemoryManager* mapped_memory_manager)
      : buffer_(NULL),
        size_(0),
        shm_id_(0),
        shm_offset_(0),
        flush_after_release_(false),
        helper_(helper),
        mapped_memory_manager_(mapped_memory_manager) {
    Reset(size);
  }

  ~ScopedMappedMemoryPtr() {
    Release();
  }

  bool valid() const {
    return buffer_ != NULL;
  }

  void SetFlushAfterRelease(bool flush_after_release) {
    flush_after_release_ = flush_after_release;
  }

  uint32_t size() const {
    return size_;
  }

  int32_t shm_id() const {
    return shm_id_;
  }

  uint32_t offset() const {
    return shm_offset_;
  }

  void* address() const {
    return buffer_;
  }

  void Release();

  void Reset(uint32_t new_size);

 private:
  void* buffer_;
  uint32_t size_;
  int32_t shm_id_;
  uint32_t shm_offset_;
  bool flush_after_release_;
  CommandBufferHelper* helper_;
  MappedMemoryManager* mapped_memory_manager_;
  DISALLOW_COPY_AND_ASSIGN(ScopedMappedMemoryPtr);
};

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_CLIENT_MAPPED_MEMORY_H_
