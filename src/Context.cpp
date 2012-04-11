/*
Galois, a framework to exploit amorphous data-parallelism in irregular
programs.

Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS SOFTWARE
AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR ANY
PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF PERFORMANCE, AND ANY
WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF DEALING OR USAGE OF TRADE.
NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH RESPECT TO THE USE OF THE
SOFTWARE OR DOCUMENTATION. Under no circumstances shall University be liable
for incidental, special, indirect, direct or consequential damages or loss of
profits, interruption of business, or related expenses which may arise from use
of Software or Documentation, including but not limited to those resulting from
defects in Software and/or Documentation, or loss or inaccuracy of data of any
kind.
*/
#include "Galois/Runtime/Context.h"
#include "Galois/Runtime/ll/SimpleLock.h"

//! Global thread context for each active thread
static __thread GaloisRuntime::SimpleRuntimeContext* thread_cnx = 0;

static GaloisRuntime::LL::SimpleLock<true> ConflictLock;

void GaloisRuntime::clearConflictLock() {
  //ConflictLock.unlock();
}

void GaloisRuntime::setThreadContext(GaloisRuntime::SimpleRuntimeContext* n)
{
  thread_cnx = n;
}

GaloisRuntime::SimpleRuntimeContext* GaloisRuntime::getThreadContext() 
{
  return thread_cnx;
}

void GaloisRuntime::doAcquire(GaloisRuntime::Lockable* C) {
  SimpleRuntimeContext* cnx = getThreadContext();
  if (cnx)
    cnx->acquire(C);
}

unsigned GaloisRuntime::SimpleRuntimeContext::cancel_iteration() {
  //FIXME: not handled yet
  return commit_iteration();
}

unsigned GaloisRuntime::SimpleRuntimeContext::commit_iteration() {
  unsigned numLocks = 0;
  while (locks) {
    //ORDER MATTERS!
    Lockable* L = locks;
    locks = L->next;
    L->next = 0;
    //__sync_synchronize();
    LL::compilerBarrier();
    L->Owner.unlock_and_clear();

    ++numLocks;
  }

  return numLocks;
}

void GaloisRuntime::SimpleRuntimeContext::acquire(GaloisRuntime::Lockable* L) {
  bool suc = L->Owner.try_lock();
  if (suc) {
    assert(!L->Owner.getValue());
    assert(!L->next);
    L->Owner.setValue(this);
    L->next = locks;
    locks = L;
  } else {
    if (L->Owner.getValue() != this) {
      //ConflictLock.lock();
      // SimpleRuntimeContext* other = L->Owner.getValue();
      // int rid = -1;
      // int rb = 0;
      // if (other) rid = other->ID;
      // if (other) rb = other->bored;
      // std::cerr << "C: " << rid << "(" << rb << ") != " << ID << "(" << bored << ") " << L << "\n";
      throw -1; // Conflict
    }
  }
}

