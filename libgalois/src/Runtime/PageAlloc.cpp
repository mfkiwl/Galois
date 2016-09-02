/** Numa-aware Page Allocators -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * Galois is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Galois.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @section Copyright
 *
 * Copyright (C) 2016, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section Description
 *
 * Numa small(page) allocations
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */

#include "Galois/Runtime/PageAlloc.h"
#include "Galois/Runtime/SimpleLock.h"
#include "Galois/Runtime/ErrorFeedBack.h"

#include <mutex>

#ifdef __linux__
#include <linux/mman.h>
#endif
#include <sys/mman.h>

//figure this out dynamically
const size_t hugePageSize = 2*1024*1024;
//protect mmap, munmap since linux has issues
static Galois::Runtime::SimpleLock allocLock;


static void* trymmap(size_t size, int flag) {
  std::lock_guard<Galois::Runtime::SimpleLock> lg(allocLock);
  const int _PROT = PROT_READ | PROT_WRITE;
  void* ptr = mmap(0, size, _PROT, flag, -1, 0);
  if (ptr == MAP_FAILED)
    ptr = nullptr;
  return ptr;
}
  // mmap flags
#if defined(MAP_ANONYMOUS)
static const int _MAP_ANON = MAP_ANONYMOUS;
#elif defined(MAP_ANON)
static const int _MAP_ANON = MAP_ANON;
#else
static_assert(0, "No Anonymous mapping");
#endif

static const int _MAP = _MAP_ANON | MAP_PRIVATE;
#ifdef MAP_POPULATE
static const int _MAP_POP = MAP_POPULATE | _MAP;
static const bool doHandMap = false;
#else
static const int _MAP_POP = _MAP;
static const bool doHandMap = true;
#endif
#ifdef MAP_HUGETLB
static const int _MAP_HUGE_POP = MAP_HUGETLB | _MAP_POP;
static const int _MAP_HUGE = MAP_HUGETLB | _MAP;
#else
static const int _MAP_HUGE_POP = _MAP_POP;
static const int _MAP_HUGE = _MAP;
#endif


unsigned Galois::Runtime::allocSize() {
  return hugePageSize;
}

void* Galois::Runtime::allocPages(unsigned num, bool preFault) {
  void* ptr = trymmap(num*hugePageSize, preFault ? _MAP_HUGE_POP : _MAP_HUGE);
  if (!ptr) {
    gWarn("Huge page alloc failed, falling back");
    ptr = trymmap(num*hugePageSize, preFault ? _MAP_POP : _MAP);
  }
  if (!ptr)
    gDie("Out of Memory");
  if (preFault && doHandMap)
    for (size_t x = 0; x < num*hugePageSize; x += 4096)
      static_cast<char*>(ptr)[x] = 0;
  return ptr;
}

void Galois::Runtime::freePages(void* ptr, unsigned num) {
  std::lock_guard<SimpleLock> lg(allocLock);
  if (munmap(ptr, num*hugePageSize) != 0)
    gDie("Unmap failed");
}

