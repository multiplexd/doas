/*
 * Copyright (c) 2017 multiplex'd <multiplexd@gmx.com>
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

int shadowauth(const char *u, const char *p) {
   struct spwd *spw = NULL;
   char *res = NULL;
   
   if ((spw = getspnam(u)) != NULL) {
      // crypt can use the salt from a previous encrypted password
      res = crypt(p, spw->sp_pwdp); 
      if(strcmp(res, spw->sp_pwdp) != 0) {
	 return 1;
      }
   }

   memset(spw, 0, sizeof(struct spwd));
   return 0;
}
