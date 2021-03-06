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
#ifndef __ARCH_I386_ATOMIC8__
#define __ARCH_I386_ATOMIC8__

#include <sys/cdefs.h>
__BEGIN_DECLS

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef volatile int64_t atomic8_t;

#define ATOMIC_INIT(i)                                                         \
  { (i) }

/**
 * atomic8_read - read atomic8 variable
 * @v: pointer of type atomic8_t
 *
 * Atomically reads the value of @v.
 */
#if __WORDSIZE == 64
#define atomic8_read(v) (*(v))
#else
#define atomic8_read(v) atomic8_add_return(0, v)
#endif

/**
 * atomic8_set - set atomic8 variable
 * @v: pointer of type atomic8_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
#if __WORDSIZE == 64
#define atomic8_set(v, i) (*(v) = (i))
#else
#define atomic8_set(v, i) (*(v) = (i))
#endif

static __inline__ int64_t atomic8_clear(atomic8_t *v) {
  return __sync_fetch_and_and(v, 0);
}
/**
 * atomic8_add - add integer to atomic8 variable
 * @i: integer value to add
 * @v: pointer of type atomic8_t
 *
 * Atomically adds @i to @v.
 */
static __inline__ int64_t atomic8_add(int64_t i, atomic8_t *v) {
  return __sync_fetch_and_add(v, i);
}

/**
 * atomic8_sub - subtract the atomic8 variable
 * @i: integer value to subtract
 * @v: pointer of type atomic8_t
 *
 * Atomically subtracts @i from @v.
 */
static __inline__ int64_t atomic8_sub(int64_t i, atomic8_t *v) {
  return __sync_fetch_and_sub(v, i);
}

/**
 * atomic8_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic8_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
static __inline__ int atomic8_sub_and_test(int64_t i, atomic8_t *v) {
  return __sync_sub_and_fetch(v, i) == 0;
}

/**
 * atomic8_inc - increment atomic8 variable
 * @v: pointer of type atomic8_t
 *
 * Atomically increments @v by 1.
 */
static __inline__ int64_t atomic8_inc(atomic8_t *v) {
  return __sync_fetch_and_add(v, 1);
}

/**
 * atomic8_dec - decrement atomic8 variable
 * @v: pointer of type atomic8_t
 *
 * Atomically decrements @v by 1.
 */
static __inline__ int64_t atomic8_dec(atomic8_t *v) {
  return __sync_fetch_and_sub(v, 1);
}

/**
 * atomic8_dec_and_test - decrement and test
 * @v: pointer of type atomic8_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static __inline__ int atomic8_dec_and_test(atomic8_t *v) {
  return __sync_sub_and_fetch(v, 1) == 0;
}

/**
 * atomic8_inc_and_test - increment and test
 * @v: pointer of type atomic8_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
static __inline__ int atomic8_inc_and_test(atomic8_t *v) {
  return __sync_add_and_fetch(v, 1) == 0;
}

/**
 * atomic8_add_negative - add and test if negative
 * @v: pointer of type atomic8_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
static __inline__ int atomic8_add_negative(int64_t i, atomic8_t *v) {
  return __sync_add_and_fetch(v, i) < 0;
}

/**
 * atomic8_add_return - add and return
 * @v: pointer of type atomic8_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns @i + @v
 */
static __inline__ int64_t atomic8_add_return(int64_t i, atomic8_t *v) {
  return __sync_add_and_fetch(v, i);
}

static __inline__ int64_t atomic8_sub_return(int64_t i, atomic8_t *v) {
  return __sync_sub_and_fetch(v, i);
}

#define atomic8_inc_return(v) (atomic8_add_return(1, v))
#define atomic8_dec_return(v) (atomic8_sub_return(1, v))

__END_DECLS
#endif
