// knot/lock.hpp — decode the glibc lock a futex address belongs to (x86-64).
//
//  pthread_mutex_t (40 B) = std::mutex        pthread_rwlock_t (56 B) = std::shared_mutex
//   +0  __lock   futex word  <- waiters        +8  __wrphase_futex   <- blocked READERS wait here
//   +8  __owner  owner TID                     +12 __writers_futex   <- blocked WRITERS wait here
//   +16 __kind   flags                         +24 __cur_writer      writer TID
//
//  __kind flags: 0x03 type (1 = recursive), 0x10 robust, 0x20 PI, 0x40 PP, 0x80 shared.
//  glibc sets 0x80 on robust mutexes too (robust reads 0x90).
//
//  A pthread_cond_t waiter blocks on cond+40/+44 (__g_signals): it holds no
//  lock and blocks nobody, so it must NEVER become a wait-for edge.
#pragma once

