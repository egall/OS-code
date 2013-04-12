#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern char **get_line();

/* Function exits shell when called */
void sys_exit(void);


/* Function parses the string given 
 * from the command prompt          */
void parse_command(char**);

/* Function executes the input command */
void execute(char** command_list);

/* Function handles in pipes */
void execute_in(char** args, char* file);
/* Function handles out pipes */
void execute_out(char** args, char* file);

/* Function handles background processes */
void execute_background(char** command_list);

/* Function handles a full pipe */
void execute_pipe(char** command_list);

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
 * args: characters entered 
 * returns: none                     */
void parse_command(char** input){
  char* end;
  int itor;
  int pipe_found = 0;
  char* file;
  /* loop through input looking for redirects */
  for(itor = 0;( (itor < 64) && (input[itor] != NULL)); ++itor){
    if(strcmp(input[itor], ">") == 0){
      /* File will be after redir */
      file = input[itor+1];
      /* We already got the info we need, so get rid of redir from input */
      input[itor] = '\0';
      /* We already got the info we need, so get rid of file from input */
      input[itor+1] = '\0';
      execute_out(input, file);
      pipe_found = 1;
    }else if(strcmp(input[itor], "<") == 0){
      /* File will be after redir */
      file = input[itor+1];
      /* We already got the info we need, so get rid of redir from input */
      input[itor] = '\0';
      /* We already got the info we need, so get rid of file from input */
      input[itor+1] = '\0';
      execute_in(input, file);
      pipe_found = 1;
    }else if(strcmp(input[itor], "|") == 0){
      /* We already got the info we need, so get rid of redir from input */
      input[itor] = '\0';
      printf("Found a pipe\n");
      execute_pipe(input);
      pipe_found = 1;
    }else if(strcmp(input[itor], "&") == 0){
      execute_background(input);
    }
  }
  if(pipe_found == 0){ 
    execute(input);
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

  printf("executing the following command: %s\n", command_list[0]);
  /* Special case for exit */
  if(strcmp(command_list[0], "exit") == 0 ){
    sys_exit();
  }
  /* Special case for cd */
  else if(strcmp(command_list[0], "cd") == 0){
    if(chdir(command_list[1]) < 0){
        printf("Error: %s is not a valid directory\n", command_list[1]);
    }
  /* All other commands */
  }else{

    /* If fork() fails, -1 is returned, error out */
    if( (pid = fork() ) < 0){
      printf("Error: Fork failed\n");
      exit(0);
    }
    /* If this is the child process pid == 0 */
    else if(pid == 0){
      /* Execute command, error out if command fails */
      if(execvp(*command_list, command_list) < 0){
        printf("Error: execvp() failed\n");
        exit(0);
      } 
    }
    /* If it's the parent */
    else {
      /* Wait while for child to finish */
      while(wait(&status) != pid);
    }
  }
   
}


/* args: Command, filename
   returns: nothing                     */
void execute_in(char** command_list, char* file){
  printf("This is an input redirect\n Args = %s\nFile = %s\n", command_list[0], file);
  pid_t pid;
  int status;
    /* If fork() fails, -1 is returned, error out */
    if( (pid = fork() ) < 0){
      printf("Error: Fork failed\n");
      exit(0);
    }
    /* If this is the child process pid == 0 */
    else if(pid == 0){
      /* Execute command, error out if command fails */
      freopen(file, "r", stdin);
      if(execvp(*command_list, command_list) < 0){
        printf("Error: execvp() failed\n");
        exit(0);
      } 
      fclose(stdin);
    }
    /* If it's the parent */
    else {
      /* Wait while for child to finish */
      while(wait(&status) != pid);
    }
  
}

/* args: Command, filename
   returns: nothing                     */
void execute_out(char** command_list, char* file){
  printf("This is an output redirect\n Args = %s\nFile = %s\n", command_list[0], file);
  pid_t pid;
  int status;
    /* If fork() fails, -1 is returned, error out */
    if( (pid = fork() ) < 0){
      printf("Error: Fork failed\n");
      exit(0);
    }
    /* If this is the child process pid == 0 */
    else if(pid == 0){
      /* Execute command, error out if command fails */
      freopen(file, "w", stdout);
      if(execvp(*command_list, command_list) < 0){
        printf("Error: execvp() failed\n");
        exit(0);
      } 
      fclose(stdout);
    }
    /* If it's the parent */
    else {
      /* Wait while for child to finish */
      while(wait(&status) != pid);
    }
}

void execute_background(char** command_list){
  pid_t pid;
  int status;
  int tester;

  printf("executing the following command: %s\n", command_list[0]);
  /* Special case for exit */
  if(strcmp(command_list[0], "exit") == 0 ){
    sys_exit();
  }
  /* Special case for cd */
  else if(strcmp(command_list[0], "cd") == 0){
    if(chdir(command_list[1]) < 0){
        printf("Error: %s is not a valid directory\n", command_list[1]);
    }
  /* All other commands */
  }else{

    /* If fork() fails, -1 is returned, error out */
    if( (pid = fork() ) < 0){
      printf("Error: Fork failed\n");
      exit(0);
    }
    /* If this is the child process pid == 0 */
    else if(pid == 0){
      /* Execute command, error out if command fails */
      if(execvp(*command_list, command_list) < 0){
        printf("Error: execvp() failed\n");
        exit(0);
      } 
    }
    /* If it's the parent */
    else {
      /* Wait while for child to finish */
//      while(wait(&status) != pid);
    }
  }
}

/*
 * args: Command string
 * return: None                      */
void execute_pipe(char** command_list){
  printf("In pipe()\n");
  pid_t pid;
  int status;
  int fd[2];
  char string[] = "Hello world\n";
  char readbuf[80];
  int nbytes;
  char *cat_args[] = {"cat", "scores", NULL};
  char *grep_args[] = {"grep", "blah", NULL};

  pipe(fd);
    /* If fork() fails, -1 is returned, error out */
    if( (pid = fork() ) < 0){
      printf("Error: Fork failed\n");
      exit(0);
    }
    /* If this is the child process pid == 0 */
    else if(pid == 0){
      printf("child\n");
      /* Replace the stdinput side of the child with the pipe input */
      dup2(fd[0], 0);
      /* Close unused half of pipe */
      close(fd[1]);
      /* Execute command, error out if command fails */
//      if(execvp(*command_list, command_list) < 0){
      if(execvp("grep", grep_args) < 0){
        printf("Error: execvp() failed\n");
        exit(0);
      }
//      fclose(stdout);
    }
    /* If it's the parent */
    else {
      printf("parent\n");
      /* Replace standard output with output part of pipe */
      dup2(fd[0], 0);
      /* Parent Closes output side */
      close(fd[1]);
      execvp("cat", cat_args);
/*
      nbytes = read(fd[0], readbuf, sizeof(readbuf) );
      printf("Received:\n %s\n", readbuf);
      int i;
      for(i = 0; command_list[i] != NULL; i++) {
        printf("Argument %d: %s\n", i, command_list[i]);

      }
*/
      
      /* Wait while for child to finish */
//      while(wait(&status) != pid);
    }
}
