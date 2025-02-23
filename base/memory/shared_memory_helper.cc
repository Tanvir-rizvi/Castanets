// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_helper.h"

#if defined(OS_CHROMEOS)
#include <sys/resource.h>
#include <sys/time.h>

#include "base/debug/alias.h"
#endif  // defined(OS_CHROMEOS)

#include "base/threading/thread_restrictions.h"

#if defined(CASTANETS)
#include "base/memory/shared_memory_tracker.h"
#if defined(OS_ANDROID)
#include <sys/mman.h>
#include "third_party/ashmem/ashmem.h"
#endif
#endif // defined(CASTANETS)

namespace base {

struct ScopedPathUnlinkerTraits {
  static const FilePath* InvalidValue() { return nullptr; }

  static void Free(const FilePath* path) {
    if (unlink(path->value().c_str()))
      PLOG(WARNING) << "unlink";
  }
};

// Unlinks the FilePath when the object is destroyed.
using ScopedPathUnlinker =
    ScopedGeneric<const FilePath*, ScopedPathUnlinkerTraits>;

#if !defined(OS_ANDROID)
bool CreateAnonymousSharedMemory(const SharedMemoryCreateOptions& options,
                                 ScopedFD* fd,
                                 ScopedFD* readonly_fd,
                                 FilePath* path) {
#if defined(OS_LINUX)
  // It doesn't make sense to have a open-existing private piece of shmem
  DCHECK(!options.open_existing_deprecated);
#endif  // defined(OS_LINUX)
  // Q: Why not use the shm_open() etc. APIs?
  // A: Because they're limited to 4mb on OS X.  FFFFFFFUUUUUUUUUUU
  FilePath directory;
  ScopedPathUnlinker path_unlinker;
  if (!GetShmemTempDir(options.executable, &directory))
    return false;

  fd->reset(base::CreateAndOpenFdForTemporaryFileInDir(directory, path));

  if (!fd->is_valid())
    return false;

  // Deleting the file prevents anyone else from mapping it in (making it
  // private), and prevents the need for cleanup (once the last fd is
  // closed, it is truly freed).
  path_unlinker.reset(path);

  if (options.share_read_only) {
    // Also open as readonly so that we can GetReadOnlyHandle.
    readonly_fd->reset(HANDLE_EINTR(open(path->value().c_str(), O_RDONLY)));
    if (!readonly_fd->is_valid()) {
      DPLOG(ERROR) << "open(\"" << path->value() << "\", O_RDONLY) failed";
      fd->reset();
      return false;
    }
  }
  return true;
}

bool PrepareMapFile(ScopedFD fd,
                    ScopedFD readonly_fd,
                    int* mapped_file,
                    int* readonly_mapped_file) {
  DCHECK_EQ(-1, *mapped_file);
  DCHECK_EQ(-1, *readonly_mapped_file);
  if (!fd.is_valid())
    return false;

  // This function theoretically can block on the disk, but realistically
  // the temporary files we create will just go into the buffer cache
  // and be deleted before they ever make it out to disk.
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  if (readonly_fd.is_valid()) {
    struct stat st = {};
    if (fstat(fd.get(), &st))
      NOTREACHED();

    struct stat readonly_st = {};
    if (fstat(readonly_fd.get(), &readonly_st))
      NOTREACHED();
    if (st.st_dev != readonly_st.st_dev || st.st_ino != readonly_st.st_ino) {
      LOG(ERROR) << "writable and read-only inodes don't match; bailing";
      return false;
    }
  }

  *mapped_file = HANDLE_EINTR(dup(fd.get()));
  if (*mapped_file == -1) {
    NOTREACHED() << "Call to dup failed, errno=" << errno;

#if defined(OS_CHROMEOS)
    if (errno == EMFILE) {
      // We're out of file descriptors and are probably about to crash somewhere
      // else in Chrome anyway. Let's collect what FD information we can and
      // crash.
      // Added for debugging crbug.com/733718
      int original_fd_limit = 16384;
      struct rlimit rlim;
      if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        original_fd_limit = rlim.rlim_cur;
        if (rlim.rlim_max > rlim.rlim_cur) {
          // Increase fd limit so breakpad has a chance to write a minidump.
          rlim.rlim_cur = rlim.rlim_max;
          if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
            PLOG(ERROR) << "setrlimit() failed";
          }
        }
      } else {
        PLOG(ERROR) << "getrlimit() failed";
      }

      const char kFileDataMarker[] = "FDATA";
      char buf[PATH_MAX];
      char fd_path[PATH_MAX];
      char crash_buffer[32 * 1024] = {0};
      char* crash_ptr = crash_buffer;
      base::debug::Alias(crash_buffer);

      // Put a marker at the start of our data so we can confirm where it
      // begins.
      crash_ptr = strncpy(crash_ptr, kFileDataMarker, strlen(kFileDataMarker));
      for (int i = original_fd_limit; i >= 0; --i) {
        memset(buf, 0, arraysize(buf));
        memset(fd_path, 0, arraysize(fd_path));
        snprintf(fd_path, arraysize(fd_path) - 1, "/proc/self/fd/%d", i);
        ssize_t count = readlink(fd_path, buf, arraysize(buf) - 1);
        if (count < 0) {
          PLOG(ERROR) << "readlink failed for: " << fd_path;
          continue;
        }

        if (crash_ptr + count + 1 < crash_buffer + arraysize(crash_buffer)) {
          crash_ptr = strncpy(crash_ptr, buf, count + 1);
        }
        LOG(ERROR) << i << ": " << buf;
      }
      LOG(FATAL) << "Logged for file descriptor exhaustion, crashing now";
    }
#endif  // defined(OS_CHROMEOS)
  }
  *readonly_mapped_file = readonly_fd.release();

  return true;
}

#elif defined(ANDROID) && defined(CASTANETS)
bool CreateAnonymousSharedMemory(const SharedMemoryCreateOptions& options,
                                 ScopedFD* fd,
                                 ScopedFD* readonly_fd,
                                 FilePath* path) {
  // "name" is just a label in ashmem. It is visible in /proc/pid/maps.
  fd->reset(ashmem_create_region(
      options.name_deprecated ? options.name_deprecated->c_str() : "",
      options.size));

  int flags = PROT_READ | PROT_WRITE | (options.executable ? PROT_EXEC : 0);
  int err = ashmem_set_prot_region(fd->get(), flags);
  if (err < 0) {
    DLOG(ERROR) << "Error " << err << " when setting protection of ashmem";
    return false;
  }

  readonly_fd->reset(dup(fd->get()));
  return true;
}
#endif  // !defined(OS_ANDROID)

#if defined(CASTANETS)
subtle::PlatformSharedMemoryRegion CreateAnonymousSharedMemoryIfNeeded(
    const UnguessableToken& guid,
    const SharedMemoryCreateOptions& option) {
  static base::Lock* lock = new base::Lock;
  base::AutoLock auto_lock(*lock);

  int fd = SharedMemoryTracker::GetInstance()->Find(guid);
  ScopedFD new_fd;
  ScopedFD readonly_fd;

  if (fd < 0) {
    FilePath path;
    if (!CreateAnonymousSharedMemory(option, &new_fd, &readonly_fd, &path))
      return subtle::PlatformSharedMemoryRegion();
    fd = new_fd.get();
    VLOG(1) << "Create anonymous shared memory for Castanets" << guid;
  }

#if !defined(OS_ANDROID)
  struct stat stat;
  CHECK(!fstat(fd, &stat));
  const size_t current_size = stat.st_size;
  if (current_size != option.size)
    CHECK(!HANDLE_EINTR(ftruncate(fd, option.size)));
#endif

  new_fd.reset(HANDLE_EINTR(dup(fd)));
  SharedMemoryTracker::GetInstance()->RemoveHolder(guid);

  subtle::PlatformSharedMemoryRegion::Mode mode =
      subtle::PlatformSharedMemoryRegion::Mode::kUnsafe;
  if (option.share_read_only) {
    mode = subtle::PlatformSharedMemoryRegion::Mode::kReadOnly;
    if (!readonly_fd.is_valid())
      readonly_fd.reset(HANDLE_EINTR(dup(fd)));
  }

  return subtle::PlatformSharedMemoryRegion::Take(
      subtle::ScopedFDPair(std::move(new_fd), std::move(readonly_fd)),
      mode, option.size, guid);
}
#endif // defined(CASTANETS)

}  // namespace base
