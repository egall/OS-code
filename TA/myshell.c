#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>

extern char **get_line();

/* Function exits shell when called */
void sys_exit(void);


/* Function parses the string given 
 * from the command prompt          */
void parse_command(char**);

/* Function executes the input command */
void execute(char** command_list);

/* main(): takes input from keyboard
 * and passes it to parse_command   */
main() {
  int i;
  char *command_list[64]; 
  char **prompt_input;
  

  while(1) {
    /* Print my prompt */
    printf("egall@baltimore>");
    prompt_input = get_line();
    /* Don't hang on return */
    if(prompt_input[0] == 0) continue;
    /* Parse user input */
    parse_command(prompt_input);
    for(i = 0; prompt_input[i] != NULL; i++) {
      /* print out current arg */
      printf("Argument %d: %s\n", i, prompt_input[i]);
    }
//    execute(prompt_input);
  }
}

/* 
 * args: none
 * return: none                      */
void sys_exit(void){
    exit(EXIT_SUCCESS);
}

/*
 * args: Command string
 * return: None                      */
void execute(char** command_list){
  pid_t pid;
  int status;
  int tester;

  if(strcmp(command_list[0], "exit") == 0 ){
    sys_exit();
  }
  else if(strcmp(command_list[0], "cd") == 0){
    if(chdir(command_list[1]) < 0){
        printf("Error: %s is not a valid directory\n", command_list[1]);
    }
  }else{

    if( (pid = fork() ) < 0){
      printf("Error: Fork failed\n");
      exit(0);
    }
    else if(pid == 0){
      if(execvp(*command_list, command_list) < 0){
        printf("Error: execvp() failed\n");
        exit(0);
      } 
    }
    else {
      while(wait(&status) != pid);
    }
  }
   
}

/*
 * args: characters entered 
 * returns: none                     */
void parse_command(char** input){
  char* end;
  execute(input);
  /* Find the '\n', which marks the end of the input.
     Replace '\n' with '\0' to signify the new end   */
//  if ((end = strchr(input, '\n')) != NULL){
//    *end = '\0';
//  }
  
  
  /* Loop until end is found */
//  while(*input != '\0'){
    /* Replace white space and seperate out each word
       so they can be added to the command list      */
//  while(*input == ' ' || *input == '\t' || *input == '\n'){
//    *input++ = '\0';
//  }
  /* The next arg in command list is set to the start
     of the next word                                */
//  command_list++ = line;
  /* Starting at the head of the current arg, cycle through
     the next chars until the next whitespace char, which
     signifies the end of the arg                    */
//  while(*input != '\0' && *input != ' ' && *input != '\t' && *input != '\n'){
//    line++;
//  }   
//  *command_list = '\0';
//  if(strcmp(prompt_input[0], "exit") == 0) sys_exit();
}
