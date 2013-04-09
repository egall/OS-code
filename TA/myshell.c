#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

extern char **get_line();

/* Function exits shell when called 
 * args: none
 * return: none 
 *                                */
void sys_exit(void){
    exit(EXIT_SUCCESS);
}

/* Function parses the string given
 * from the command prompt
 * args: characters entered 
 * returns: none 
 *                                */

void parse_command(char **args){

}
/* main(): takes input from keyboard
 * and passes it to parse_command
 *                                  */

main() {
  int i;
  char **args; 

  while(1) {
    /* Print my prompt */
    printf("egall@baltimore>");
    args = get_line();
    /* Don't hang on return */
    if(args[0] == 0) continue;
    /* Cycle through args */
    for(i = 0; args[i] != NULL; i++) {
      /* print out current arg */
      printf("Argument %d: %s\n", i, args[i]);
    }
    parse_command(args);
  }
}
