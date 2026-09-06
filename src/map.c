/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

#include "map.h"
#if !defined(BLR_NO_MMAP)
#ifdef BLR_WIN32
#include "win32.h"
#elif defined(HAVE_SYS_MMAN_H) && defined(HAVE_MMAP) && defined(HAVE_MUNMAP) && defined(HAVE_MSYNC)
#include <sys/mman.h>
#include <unistd.h>
#define BLR_POSIX_MAP 1
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#endif

int blr_no_mmap;

int bm_file(blr_map * m, void * handle, sz at, sz n, int write) {
  m->p = NULL;  m->len = n;  m->heap = 0;
  if (!n || blr_no_mmap) return 0;
#if defined(BLR_WIN32) && !defined(BLR_NO_MMAP)
  { uint64_t size = handle ? 0 : n, off = at;
    HANDLE h = CreateFileMappingA(handle ? handle : INVALID_HANDLE_VALUE, NULL,
                                  write ? PAGE_READWRITE : PAGE_READONLY,
                                  (DWORD) (size >> 32), (DWORD) size, NULL);
    if (!h) return 0;
    m->p = MapViewOfFile(h, write ? FILE_MAP_WRITE : FILE_MAP_READ,
                        (DWORD) (off >> 32), (DWORD) off, n);
    CloseHandle(h);
  }
#elif defined(BLR_POSIX_MAP)
  { int flags = write ? MAP_SHARED : MAP_PRIVATE, fd = -1;
    if (handle) fd = fileno((FILE *) handle);
#ifdef MAP_ANONYMOUS
    else flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    else return 0;
#endif
    if ((off_t) at < 0 || (uintmax_t) (off_t) at != at) return 0;
    m->p = mmap(NULL, n, PROT_READ | (write ? PROT_WRITE : 0), flags, fd, (off_t) at);
    if (m->p == MAP_FAILED) m->p = NULL;
  }
#endif
  return m->p != NULL;
}

void bm_alloc(blr_map * m, sz n) {
  if (bm_file(m, NULL, 0, n, 1)) return;
  m->p = xcalloc(n, 1);  m->heap = 1;
}

int bm_flush(blr_map * m) {
  if (!m->p || m->heap) return 1;
#if defined(BLR_WIN32) && !defined(BLR_NO_MMAP)
  return FlushViewOfFile(m->p, m->len) != 0;
#elif defined(BLR_POSIX_MAP)
  return msync(m->p, m->len, MS_SYNC) == 0;
#else
  return 1;
#endif
}

void bm_free(blr_map * m) {
  if (!m->p) return;
  if (m->heap) free(m->p);
#if defined(BLR_WIN32) && !defined(BLR_NO_MMAP)
  else if (!UnmapViewOfFile(m->p)) FATAL_CODE(BLR_EXIT_IO, "cannot unmap memory");
#elif defined(BLR_POSIX_MAP)
  else if (munmap(m->p, m->len)) FATAL_CODE(BLR_EXIT_IO, "cannot unmap memory");
#endif
  m->p = NULL;  m->len = 0;  m->heap = 0;
}
