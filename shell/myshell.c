#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>

extern char **getline();

/* This function kills the shell */
void sys_exit(void);  

/* This function will parse the input from the command line */
void parse_command(char** args);

/* This function excutes commands */
void sys_execute(char** args);

/* This function executes commands and prints them to file */
void sys_exe_redir(char** args, int in_flag, int out_flag);

/* This function executes command taking input from a file */
void sys_exe_pipe(char** args, int pipefd_in, int pipefd_out);

/* Get filename */
char* get_filename(char** args);


int main(){
  int itor;
  char** args;

  while(1){
    printf("=>");
    args = getline();
    if(args[0] == 0){ 
      continue;
    }
    for(itor = 0; args[itor] != NULL; itor++){
      printf("Argument %d: %s\n", itor, args[itor]);
    }
    parse_command(args); 

  }
}

void sys_exit(void){
  exit(EXIT_SUCCESS);
}

void parse_command(char **args){
  int break_flag = 0;
  int itor = 0;
  int inner_itor = 0;
  int pipe_itor = 0;
  int in_flag = 0;
  int out_flag = 0;
  int args_processed = 0;
  size_t num_commands = 0;
  char*** pipe_args = malloc(4096*sizeof(char**) ); 
  int arg_count = 0;
  int pipe_num = 0;
  int arg_length = 0;  
  int** pipefd = malloc(20*sizeof(int*) );


  /* hard code for cd */
  if(strcmp(args[0], "cd") == 0){
    if(chdir(args[1]) < 0){
      printf("Error: %s is not a valid directory\n", args[1]);
    }
  }
  else if(strcmp(args[0], "exit") == 0){
    sys_exit();
  }
  else{
    for(itor = 0; args[itor] != NULL; itor++) num_commands++;
    if(num_commands <= 2){
      sys_execute(args);
    }
    else{
      for(itor = 0; args[itor] != NULL; itor++){
        if(strcmp(args[itor],  ">") == 0){
          out_flag++;
        }
        if( strcmp(args[itor], "<") == 0){
          in_flag++;
        }
        if(strcmp(args[itor], "|") == 0){
          pipe_num++;
        }
      }
      if(out_flag > 0 && pipe_num == 0 && in_flag == 0){
        /* hack */ 
	pipe_num--;
        sys_exe_redir(args, in_flag, out_flag);
      }
      if(in_flag > 0 && pipe_num == 0 && out_flag == 0){
        /* hack */ 
	pipe_num--;
        sys_exe_redir(args, in_flag, out_flag);
      }
      itor = 0;
      for(pipe_itor = 0; pipe_itor <= pipe_num;pipe_itor++) {
        pipe_args[pipe_itor] = malloc(5*sizeof(char*) );
        pipefd[pipe_itor] = malloc(3*sizeof(int) ); 
	if(pipe_itor != pipe_num){
          if( pipe(pipefd[pipe_itor]) < 0){
	    printf("pipe failed\n"); 
          }
        }
	for(; args[itor] != NULL; itor++){
	  arg_count++; 
          if(strcmp(args[itor], "|") == 0){
            args_processed = arg_count;
	    if(pipe_itor == 0){
              printf("b:fd_in = %d fd_out = %d\n", pipefd[pipe_itor][0], pipefd[pipe_itor][1]);
	      sys_exe_pipe(pipe_args[pipe_itor], 0, pipefd[pipe_itor][1]);
            }else{
              printf("m:fd_in = %d fd_out = %d\n", pipefd[pipe_itor][0], pipefd[pipe_itor][1]);
	      sys_exe_pipe(pipe_args[pipe_itor], pipefd[pipe_itor-1][0], pipefd[pipe_itor][1]);
            }
	    itor++;
	    break;
	  }
	  else{
	    pipe_args[pipe_itor][itor - args_processed] = malloc(sizeof(char) );
	    for(inner_itor = 0; strlen(args[itor]) > inner_itor; inner_itor++){
	      pipe_args[pipe_itor][itor - args_processed][inner_itor] = '\0';
	      pipe_args[pipe_itor][itor - args_processed][inner_itor] = args[itor][inner_itor];
	    }
          }
        }
      }
      pipe_itor--;
      if(pipe_itor == pipe_num && pipe_num != -1){
        printf("e:fd_in = %d fd_out = %d\n", pipefd[pipe_itor][0], pipefd[pipe_itor][1]);
        sys_exe_pipe(pipe_args[pipe_itor], pipefd[pipe_itor-1][0], 1);  
      }
    }
  }
  free(pipe_args);
  free(pipefd);
}

void sys_execute(char **args){
  pid_t pid;
  int status;
  int itor = 0;
  for(itor = 0; args[itor] != NULL; itor++){
    printf("args[%d] = %s\n", itor, args[itor]);
    if(args[itor][1] < 32){
      printf("%s = NULL\n", args[itor]);
      args[itor] = NULL;
    }
  }

  /*fork child process */
  if ((pid = fork() ) < 0) {
    printf("ERROR: Forking failed");
    exit(1);
  }
  /* execute for the child */
  if(pid == 0){           
    /* calls the command */ 
    if( execvp(*args, args) < 0) {
      printf("ERROR: %s is not a valid command\n", args[0]);
      exit(1);
    }
  exit(0);

  /* for the parent */
  }
  else{
    while(wait(&status) != pid);
  }
}


void sys_exe_redir(char **args, int in_flag, int out_flag){
  pid_t pid;
  int status;
  size_t filedesc;
  printf("In redir\n");
  if(out_flag > 0){
    mode_t mode = S_IRWXU | S_IRGRP | S_IROTH;
    char *out_file = get_filename(args);
    filedesc = open(out_file, O_WRONLY | O_APPEND | O_CREAT, mode);
    if(filedesc < 0) printf("File wasn't opened\n");
  }
  if(in_flag > 0){
    char *in_file = get_filename(args);
    filedesc = open(in_file, O_RDONLY);
    if(filedesc < 0) printf("File wasn't opened\n");
  }

  if ((pid = fork() ) < 0) {
    printf("ERROR: Forking failed");
    exit(1);
  }
  if(pid == 0){
    if(out_flag > 0)
      if(dup2(filedesc, 1) < 0) printf("dup failed\n");

    if(in_flag > 0)
      if(dup2(filedesc, 0) < 0) printf("dup failed\n");

    if( execvp(*args, args) < 0) {
      printf("ERROR: %s is not a valid command\n", args[0]);
      exit(1);
    }
    exit(0);
  }
  else{
    while(wait(&status) != pid);
  }
}   

void sys_exe_pipe(char **args, int pipefd_in, int pipefd_out){
  pid_t pid;
  int status;
  int itor = 0;
  printf("fd_in = %d, fd_out = %d\n", pipefd_in, pipefd_out);
  if ((pid = fork() ) < 0) {
    printf("ERROR: Forking failed");
    exit(1);
  }
  if(pid == 0){           
    if(pipefd_in != 0){
      dup2(pipefd_in, 0);
      close(0);
    }
    if(pipefd_out != 1){
      dup2(pipefd_out, 1);
      close(1);
    }
    if( execvp(*args, args) < 0) {
      printf("ERROR: %s is not a valid command\n", args[0]);
      exit(1);
    }
    exit(0);
  }
  else{
    while(wait(&status) != pid);
  }
 
}

char* get_filename(char **args){
  int itor;
  char* filename;
  for(itor = 0; args[itor] != NULL; itor++){
    if(strcmp(args[itor], ">") == 0 || strcmp(args[itor], "<") == 0){
      filename = strcpy(args[itor+1], args[itor+1]);
      args[itor] = NULL;
      if(args[itor+1] != NULL){
        args[itor+1] = NULL;
      }
    }
  }
}

