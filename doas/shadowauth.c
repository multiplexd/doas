#include <crypt.h>
#include <shadow.h>

int shadowauth(const char *u, const char *p) {
   struct spwd *spw = NULL;
   char *salt = (char*) malloc(sizeof(char) * 11);

   if ((spw = getspnam(u)) != NULL) {
      strncat(salt, spw->sp_pwdp, 11);

      if(strcmp(crypt(p, salt), spw->sp_pwdp) != 0) {
	 return 1;
      }
   }

   free(salt);
   memset(spw, 0, sizeof(struct spwd));
   return 0;
}
