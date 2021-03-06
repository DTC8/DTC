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

#include "da_hashkit.h"

uint32_t
hash_murmur(const char *key, size_t length)
{
    /*
     * 'm' and 'r' are mixing constants generated offline.  They're not
     * really 'magic', they just happen to work well.
     */

    const unsigned int m = 0x5bd1e995;
    const uint32_t seed = (0xdeadbeef * (uint32_t)length);
    const int r = 24;


    /* Initialize the hash to a 'random' value */

    uint32_t h = seed ^ (uint32_t)length;

    /* Mix 4 bytes at a time into the hash */

    const unsigned char * data = (const unsigned char *)key;

    while (length >= 4) {
        unsigned int k = *(unsigned int *)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        length -= 4;
    }

    /* Handle the last few bytes of the input array */

    switch(length) {
    case 3:
        h ^= ((uint32_t)data[2]) << 16;

    case 2:
        h ^= ((uint32_t)data[1]) << 8;

    case 1:
        h ^= data[0];
        h *= m;

    default:
        break;
    };

    /*
     * Do a few final mixes of the hash to ensure the last few bytes are
     * well-incorporated.
     */

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
}
