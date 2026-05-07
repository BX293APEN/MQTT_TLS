/*
 * cifra - embedded cryptography library
 * Written in 2014 by Joseph Birr-Pixton <jpixton@gmail.com>
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to the
 * public domain worldwide. This software is distributed without any
 * warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication
 * along with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#ifndef TASSERT_H
#define TASSERT_H

/* Tiny assert
 * -----------
 *
 * This is an assert(3) definition which doesn't include any
 * strings, but just branches to abort(3) on failure.
 */

/* PENQUIC: assert を pen_abort() ベースで統一。
 * penlib.h が pen_abort() を提供するため <stdlib.h> は不要。
 * penlib.h より後に include されるため #undef してから再定義することで
 * "assert redefined" 警告を抑制する。                                  */
#ifdef FULL_FAT_ASSERT
# include <assert.h>
#else
# ifdef assert
#  undef assert
# endif
# ifdef NDEBUG
#  define assert(expr) ((void)(expr))
# else
#  define assert(expr) do { if (!(expr)) pen_abort(); } while (0)
# endif
#endif

#endif
