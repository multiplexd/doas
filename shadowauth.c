/*
 * Copyright (c) 2017 multiplexd <multi@in-addr.xyz>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <crypt.h>
#include <shadow.h>
#include <stdlib.h>
#include <string.h>

#include "bsd-compat/compat.h"

int shadowauth(const char *u, const char *p) {
   struct spwd *spw = NULL;
   char *res = NULL;

   if ((spw = getspnam(u)) == NULL) {
      return 1;
   }

   if ((res = crypt(p, spw->sp_pwdp)) == NULL) {
      explicit_bzero(spw, sizeof(struct spwd));
      return 1;
   }

   if (strcmp(res, spw->sp_pwdp) != 0) {
      explicit_bzero(spw, sizeof(struct spwd));
      explicit_bzero(res, strlen(res));
      return 1;
   }

   explicit_bzero(spw, sizeof(struct spwd));
   explicit_bzero(res, strlen(res));
   
   return 0;
}
