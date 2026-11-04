// fable_2 - Portable host memory probing for the diagnostics
//
// The probes and dumpers under src/diagnostics read guest memory from host
// threads. The guest arena is commit-on-demand, so an address can be reserved
// but not readable yet; touching it would raise a host fault that the SDK's
// guest fault handler intercepts far from any guest context. On Windows the
// probes used VirtualQuery and SEH (__try/__except) to stay safe. This header
// offers the same three primitives on every host:
//
//   QueryRegion(p, r)   - the region containing p (base, size, protection)
//   IsReadable(p, n)    - can n bytes at p be read without faulting?
//   SafeCopy(d, s, n)   - copy n bytes, returning false instead of faulting
//
// Linux: QueryRegion parses /proc/self/maps; IsReadable/SafeCopy use
// process_vm_readv on our own pid, which reports EFAULT for an unreadable
// (e.g. PROT_NONE reserved) page instead of delivering SIGSEGV.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#endif

namespace fable2::hostmem {

struct Region {
  uintptr_t base = 0;   // first byte of the region
  size_t size = 0;      // region size in bytes
  bool committed = false;  // backed by memory (not just a reservation)
  bool readable = false;
  bool writable = false;
};

#ifdef _WIN32

inline bool QueryRegion(const void* p, Region& r) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (::VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
  r.base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
  r.size = mbi.RegionSize;
  r.committed = mbi.State == MEM_COMMIT;
  const DWORD prot = mbi.Protect;
  const bool guard = (prot & (PAGE_GUARD | PAGE_NOACCESS)) != 0;
  r.readable = r.committed && !guard &&
               (prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
  r.writable = r.committed && !guard &&
               (prot & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                        PAGE_EXECUTE_WRITECOPY)) != 0;
  return true;
}

inline bool IsReadable(const void* p, size_t n = 1) {
  uintptr_t cur = reinterpret_cast<uintptr_t>(p);
  const uintptr_t end = cur + (n ? n : 1);
  while (cur < end) {
    Region r;
    if (!QueryRegion(reinterpret_cast<const void*>(cur), r) || !r.readable) return false;
    cur = r.base + r.size;
  }
  return true;
}

inline bool SafeCopy(void* dst, const void* src, size_t n) {
  if (!IsReadable(src, n)) return false;
  std::memcpy(dst, src, n);
  return true;
}

inline bool IsWritable(const void* p, size_t n = 1) {
  uintptr_t cur = reinterpret_cast<uintptr_t>(p);
  const uintptr_t end = cur + (n ? n : 1);
  while (cur < end) {
    Region r;
    if (!QueryRegion(reinterpret_cast<const void*>(cur), r) || !r.writable) return false;
    cur = r.base + r.size;
  }
  return true;
}

// Make [p, p+n) read/write (commit it if it is only reserved).
inline bool CommitReadWrite(void* p, size_t n) {
  void* r = VirtualAlloc(p, n, MEM_COMMIT, PAGE_READWRITE);
  if (!r) r = VirtualAlloc(p, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  return r != nullptr;
}

#else  // POSIX (Linux)

inline bool QueryRegion(const void* p, Region& r) {
  const uintptr_t a = reinterpret_cast<uintptr_t>(p);
  std::FILE* f = std::fopen("/proc/self/maps", "r");
  if (!f) return false;
  char line[512];
  uintptr_t prev_end = 0;
  bool found = false;
  while (std::fgets(line, sizeof(line), f)) {
    char* rest = nullptr;
    const uintptr_t lo = std::strtoull(line, &rest, 16);
    if (!rest || *rest != '-') continue;
    const uintptr_t hi = std::strtoull(rest + 1, &rest, 16);
    if (a < lo) {
      // Address falls in an unmapped gap: report the gap as uncommitted, so
      // a region walk can step over it like a Windows MEM_FREE region.
      r.base = prev_end;
      r.size = lo - prev_end;
      r.committed = r.readable = r.writable = false;
      found = true;
      break;
    }
    if (a < hi) {
      while (*rest == ' ') ++rest;
      r.base = lo;
      r.size = hi - lo;
      r.readable = rest[0] == 'r';
      r.writable = rest[1] == 'w';
      // Linux has no separate commit state; a PROT_NONE mapping is the
      // equivalent of a reserved-but-uncommitted Windows region.
      r.committed = r.readable || r.writable || rest[2] == 'x';
      found = true;
      break;
    }
    prev_end = hi;
  }
  std::fclose(f);
  return found;
}

inline bool SafeCopy(void* dst, const void* src, size_t n) {
  if (n == 0) return true;
  iovec local{dst, n};
  iovec remote{const_cast<void*>(src), n};
  const ssize_t got = ::process_vm_readv(::getpid(), &local, 1, &remote, 1, 0);
  return got == static_cast<ssize_t>(n);
}

inline bool IsReadable(const void* p, size_t n = 1) {
  // Protection is page-granular, so probing one byte per page is enough.
  const uintptr_t page = static_cast<uintptr_t>(::sysconf(_SC_PAGESIZE));
  uintptr_t cur = reinterpret_cast<uintptr_t>(p);
  const uintptr_t end = cur + (n ? n : 1);
  uint8_t byte;
  while (cur < end) {
    if (!SafeCopy(&byte, reinterpret_cast<const void*>(cur), 1)) return false;
    cur = (cur & ~(page - 1)) + page;
  }
  return true;
}

// Is every page of [p, p+n) mapped writable? Walks /proc/self/maps regions,
// so it is meant for occasional checks before a write, not hot loops.
inline bool IsWritable(const void* p, size_t n = 1) {
  uintptr_t cur = reinterpret_cast<uintptr_t>(p);
  const uintptr_t end = cur + (n ? n : 1);
  while (cur < end) {
    Region r;
    if (!QueryRegion(reinterpret_cast<const void*>(cur), r) || !r.writable) return false;
    if (r.base + r.size <= cur) return false;  // no forward progress
    cur = r.base + r.size;
  }
  return true;
}

// Make [p, p+n) read/write: the POSIX analogue of committing a reserved
// range is lifting its PROT_NONE protection (page-aligned).
inline bool CommitReadWrite(void* p, size_t n) {
  const uintptr_t page = static_cast<uintptr_t>(::sysconf(_SC_PAGESIZE));
  const uintptr_t lo = reinterpret_cast<uintptr_t>(p) & ~(page - 1);
  const uintptr_t hi = (reinterpret_cast<uintptr_t>(p) + n + page - 1) & ~(page - 1);
  return ::mprotect(reinterpret_cast<void*>(lo), hi - lo, PROT_READ | PROT_WRITE) == 0;
}

#endif

}  // namespace fable2::hostmem
