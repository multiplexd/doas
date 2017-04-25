#include <crypt.h>
#include <shadow.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

/* The parsing of the salt done here is NOT portable! */

int shadowauth(const char *u, const char *p) {
   struct spwd *spw = NULL;
   char *salt = (char*) calloc(20, sizeof(char));
   char *res = NULL;
   
   if ((spw = getspnam(u)) != NULL) {
      strlcat(salt, spw->sp_pwdp, 21);

      res = crypt(p, salt);
      if(strcmp(res, spw->sp_pwdp) != 0) {
	 return 1;
      }
   }

   free(salt);
   memset(spw, 0, sizeof(struct spwd));
   return 0;
}
