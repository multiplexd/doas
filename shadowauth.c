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
