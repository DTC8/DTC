/*
* Copyright [2021] JD.com, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#pragma once
#ifndef ROCKSDB_LITE

#include <memory>

#include "rocksdb/status.h"

namespace rocksdb {

// TransactionDBMutex and TransactionDBCondVar APIs allows applications to
// implement custom mutexes and condition variables to be used by a
// TransactionDB when locking keys.
//
// To open a TransactionDB with a custom TransactionDBMutexFactory, set
// TransactionDBOptions.custom_mutex_factory.

class TransactionDBMutex {
 public:
  virtual ~TransactionDBMutex() {}

  // Attempt to acquire lock.  Return OK on success, or other Status on failure.
  // If returned status is OK, TransactionDB will eventually call UnLock().
  virtual Status Lock() = 0;

  // Attempt to acquire lock.  If timeout is non-negative, operation may be
  // failed after this many microseconds.
  // Returns OK on success,
  //         TimedOut if timed out,
  //         or other Status on failure.
  // If returned status is OK, TransactionDB will eventually call UnLock().
  virtual Status TryLockFor(int64_t timeout_time) = 0;

  // Unlock Mutex that was successfully locked by Lock() or TryLockUntil()
  virtual void UnLock() = 0;
};

class TransactionDBCondVar {
 public:
  virtual ~TransactionDBCondVar() {}

  // Block current thread until condition variable is notified by a call to
  // Notify() or NotifyAll().  Wait() will be called with mutex locked.
  // Returns OK if notified.
  // Returns non-OK if TransactionDB should stop waiting and fail the operation.
  // May return OK spuriously even if not notified.
  virtual Status Wait(std::shared_ptr<TransactionDBMutex> mutex) = 0;

  // Block current thread until condition variable is notified by a call to
  // Notify() or NotifyAll(), or if the timeout is reached.
  // Wait() will be called with mutex locked.
  //
  // If timeout is non-negative, operation should be failed after this many
  // microseconds.
  // If implementing a custom version of this class, the implementation may
  // choose to ignore the timeout.
  //
  // Returns OK if notified.
  // Returns TimedOut if timeout is reached.
  // Returns other status if TransactionDB should otherwis stop waiting and
  //  fail the operation.
  // May return OK spuriously even if not notified.
  virtual Status WaitFor(std::shared_ptr<TransactionDBMutex> mutex,
                         int64_t timeout_time) = 0;

  // If any threads are waiting on *this, unblock at least one of the
  // waiting threads.
  virtual void Notify() = 0;

  // Unblocks all threads waiting on *this.
  virtual void NotifyAll() = 0;
};

// Factory class that can allocate mutexes and condition variables.
class TransactionDBMutexFactory {
 public:
  // Create a TransactionDBMutex object.
  virtual std::shared_ptr<TransactionDBMutex> AllocateMutex() = 0;

  // Create a TransactionDBCondVar object.
  virtual std::shared_ptr<TransactionDBCondVar> AllocateCondVar() = 0;

  virtual ~TransactionDBMutexFactory() {}
};

}  // namespace rocksdb

#endif  // ROCKSDB_LITE
