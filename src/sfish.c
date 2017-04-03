#include "sfish.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "sio.h"
#define checkforNull(program) if(!prog){ printf("Error in redirect\n"); return;}
typedef void (*sighandler_t)(int);
extern int latest_alarm;

char* path_id_for_alarm;
int currnetChild = 0; //number of current running children
int lastChildPID = 0; //last reaped child
int printtothisfd = 1; //all error printed to this fd

void new_stdoutFD() { //returns new file descriptor that is dup of STDOUT, this file descriptor is used to print shell errors
  printtothisfd = dup(STDOUT_FILENO);
}

void sf_pwd(char* currentPath, int size, int x) { //print pwd to std.
  if (getcwd(currentPath, size) == NULL)
    dprintf(printtothisfd, "Error has occured while pwd");
  if (x)
    printf("%s\n", currentPath);
}

void Chdir(char* path) {//Wrapper around chdir
  char curr[1024];
  sf_pwd(curr, 1024, 0);
  if (chdir(path) == -1)
    dprintf(printtothisfd, "No such file or directory");
  else
    setenv("OLDPWD", curr, 1);
}

void sf_cd(char* path) { //implements cd using Chdir function
  if (path != NULL) {
    if (strcmp(path, "-") == 0)
      Chdir(getenv("OLDPWD"));
    else
      Chdir(path);
  } else {
    Chdir(getenv("HOME"));
  }
}

char* getHeadTitle(char* currentPath, char* ret) {//returns appropriate user prompt, sets ret to to user prompt
  sprintf(ret, "%s\033[22;34m<%s> $ \x1b[0m", "<jappatel>:", currentPath);
  path_id_for_alarm = ret;
  return ret;
}

char** strtoArr(char* input, char** arr) { //takes in string and creates array of words
  char* currWord = strtok(input, " ");
  int words = 0;
  while (currWord) {
    arr = realloc(arr, sizeof(char*) * ++words);
    arr[words - 1] = currWord;
    currWord = strtok(NULL, " ");
  }
  arr = realloc(arr, sizeof(char*) * ++words);
  arr[words - 1] = NULL;
  return arr;
}

char* findinPath(char* name, char* fullPath) {//finds the path for given file, in $PATH section of envp. if path has '/' returns same path.
  if (strchr(name, '/')) {
    strcpy(fullPath, name);
    return fullPath;
  }
  char* PATH = getenv("PATH");
  char* cpy = calloc(1, strlen(PATH));
  strcpy(cpy, PATH);
  char* token = strtok(cpy, ":");
  while (token) {
    strcpy(fullPath, token);
    strcat(fullPath, "/");
    strcat(fullPath, name);
    struct stat buffer;
    if (stat(fullPath, &buffer) == 0)
      return fullPath;
    token = strtok(NULL, ":");
  }
  return NULL;
}

void redirect(char* cmd, int in, int out, char** envp, int redirectboth, int frompipe);
void execute(char* cmd, char** envp) { //tokenize the command and runs execve
  int in =0, out = 0;
  if (strchr(cmd, '<'))
    in = 1;
  if (strchr(cmd, '>'))
    out = 1;
  if (strstr(cmd, "&>")) {
    redirect(cmd, in, out, envp, 1, 1);
  } else if (out || in) {
    redirect(cmd, in, out, envp, 0, 1);
  }

  char* cmdcpy = calloc(1, strlen(cmd));
  strcpy(cmdcpy, cmd);
  char* token = strtok(cmdcpy, " ");
  if (strcmp(token, "pwd") == 0) {
    char currentPath[1024];
    sf_pwd(currentPath, 1024, 1);
    exit(0);
  } else if (strcmp(token, "help") == 0) { //help printed to stdout, insted of printtothisfd to support redirection
    printf("Commands that can be ran are cd, pwd, pipe, redirect\n");
    exit(0);
  } else {
    char fullPath[1024];
    findinPath(token, fullPath);
    char** arr = NULL;
    strcpy(cmdcpy, cmd);
    arr = strtoArr(cmdcpy, arr);
    if (execve(fullPath, arr, envp) < 0) {
      printf("%s Command Not Found\n", cmd);
      exit(0);
    }
  }
  free(cmdcpy);
}

void runForgroundProgram(char* cmd, char** envp) { //run program and wait for it to die before continuing.
  int pid = 0;
  currnetChild++;
  if ((pid = fork()) == 0)
    execute(cmd, envp);
  while (currnetChild > 0); //waiting for child to die
}

int findn(char* cmd) { //returns number before > or >> redirector, if no number it returns 1
  while (*cmd != '>' && *cmd != '\0')
    cmd++;
  cmd -= 1;
  if (*cmd == ' ') {return 1;}
  return *cmd - '0';
}

void redirect(char* cmd, int in, int out, char** envp, int redirectboth, int frompipe) {// redirection supported >, <, n>, &>, n>>, &>>, >>
  char* cmdcpy = calloc(1, strlen(cmd));
  strcpy(cmdcpy, cmd);
  char* prog = NULL, *outputfile = NULL, *inputfile = NULL;
  int stdout_save = 0, stdin_save = 0, stderr_save = 0, temp_out = 0, temp_in = 0;
  int n = findn(cmd); //find the number before > or >>
  if (redirectboth) { //cases used to tokenize the cmd according to wich ever redirect function is picked
    prog = strtok(cmdcpy, "&>");
    checkforNull(prog);
    outputfile = strtok(NULL, "&>");
    checkforNull(outputfile)
  } else if (in && out) {
    prog = strtok(cmdcpy, "<");
    checkforNull(prog);
    inputfile = strtok(NULL, "<");
    checkforNull(inputfile);
    inputfile = strtok(inputfile, ">");
    checkforNull(inputfile);
    outputfile = strtok(NULL, ">");
    checkforNull(!outputfile)
  } else if (in) {
    prog = strtok(cmdcpy, "<");
    checkforNull(prog);
    inputfile = strtok(NULL, "<");
    checkforNull(inputfile);
  } else if (out) {
    prog = strtok(cmdcpy, ">");
    checkforNull(prog);
    outputfile = strtok(NULL, ">");
    checkforNull(outputfile)
  }
  if (in) { //save and open new input file for stdin
    inputfile = strtok(inputfile, " ");
    stdin_save = dup(STDIN_FILENO);
    temp_in = open(inputfile, O_RDONLY);
    dup2(temp_in, STDIN_FILENO);
    close(temp_in);
  }
  if (out) { //save and open new output file for appropriate file descriptor
    outputfile = strtok(outputfile, " ");
    stdout_save = dup(n);
    if (strstr(cmd, ">>")) // if >> is used open the file in append mode
      temp_out = open(outputfile, O_RDWR | O_APPEND | O_CREAT, 0644);
    else
      temp_out = open(outputfile, O_RDWR | O_TRUNC | O_CREAT, 0644);
    dup2(temp_out, n);
    if (redirectboth) {
      stderr_save = dup(STDERR_FILENO);
      dup2(temp_out, STDERR_FILENO);
    }
    close(temp_out);
  }
  if(frompipe)
    execute(prog, envp);
  else
    runForgroundProgram(prog, envp); //running program in forground
  if (in) { //reset inputs
    dup2(stdin_save, STDIN_FILENO);
    close(stdin_save);
  }
  if (out) { //reset output
    dup2(stdout_save, n);
    close(stdout_save);
  }
  if (redirectboth) { //if both stdout and std err were redirected close stderr
    dup2(stderr_save, STDERR_FILENO);
    close(stderr_save);
  }
  free(cmdcpy);
}


int get_pipe(char* input) { //returns number of pipe in the command
  int x = 0;
  while (*input != '\0') {
    if (*input == '|')
      x++;
    input++;
  }
  return x;
}

void pipeOperator(char* cmd, char** envp) { //operation for any number of pipes
  char* cmdcpy = calloc(1, strlen(cmd));
  strcpy(cmdcpy, cmd);
  int numPipe = get_pipe(cmd);
  int pipefds[2 * numPipe];
  for (int x = 0; x < numPipe; x++) { //initializing pipes for all the programs
    pipe(&(pipefds[2 * x]));
  }
  char* prog1;
  int pid = 0, stdout_save = dup(STDOUT_FILENO), stdin_save = dup(STDIN_FILENO);
  for (int x = 0; x < numPipe + 1; x++) {
    if (x == 0)
      prog1 = strtok(cmdcpy, "|");
    else
      prog1 = strtok(NULL, "|");
    if (prog1 == NULL) {
      dprintf(printtothisfd, "Error in piping\n");
      break;
    }
    currnetChild++;
    if ((pid = fork()) == 0) {
      if (x != 0) { //read from appropriate fd
        dup2(pipefds[(x - 1) * 2], 0);
        close(pipefds[(x - 1) * 2]);
      }
      if (x != numPipe) { //write to appropriate fd
        dup2(pipefds[x * 2 + 1], 1);
        close(pipefds[x * 2 + 1]);
      }
      for (int x = 0; x < numPipe * 2 + 1; x++) // close all the fds in array
        close(pipefds[x]);
      execute(prog1, envp);
      exit(0);
    }
  }
  for (int x = 0; x < numPipe * 2 + 1; x++)
    close(pipefds[x]);
  while (currnetChild > 0); //wait for all childrens to die
  //reset fds
  dup2(stdout_save, STDOUT_FILENO);
  close(stdout_save);
  dup2(stdin_save, STDIN_FILENO);
  close(stdin_save);
  free(cmdcpy);
}

int only_spaces(char* input) { // checks if the string is filled with bunch of spaces
  while (isspace(*input))
    input++;
  return *input == '\0' ? 1 : 0;
}

void alramHandler(int x) { //this handler is installed for Alarm Signal in main
  sio_puts(printtothisfd, "\x1b[32m\nAlarm ");
  sio_putl(printtothisfd, latest_alarm);
  sio_puts(printtothisfd, " went off\n\x1b[0m");
  if (currnetChild == 0)
    sio_puts(printtothisfd, path_id_for_alarm);
}

void childHandler(int x, siginfo_t *siginfo, void *context) { //this handler is installed for SIGCHLD in main, recudes the counter of currentChild
  pid_t pid;
  while ((pid = wait(NULL)) > 0) {
    sio_puts(printtothisfd, "\x1b[32mChild with pid ");
    sio_putl(printtothisfd,  pid);
    sio_puts(printtothisfd, " had died. It took ");
    sio_putl(printtothisfd, (int)(((1000 * (double)(siginfo->si_stime + siginfo->si_utime)))/sysconf(_SC_CLK_TCK)));
    sio_puts(printtothisfd, " milliseconds\n\x1b[0m");

    currnetChild--;
  }
}

void wellEasy(int x) { //this handle kill -s SIGUSR2 pid, prints and continues
  sio_puts(printtothisfd, "\x1b[32mWell that was easy.\n\x1b[0m");
}
