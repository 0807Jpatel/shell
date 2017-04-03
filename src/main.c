#include "sfish.h"
#include "debug.h"
#include <signal.h>
#define checkforNull(program) if(!prog){ printf("Error in redirect\n"); return;}

/*
 * As in previous hws the main function must be in its own file!
 */
extern int currnetChild;
extern int latest_alarm;
void sf_cd(char* path);
void sf_pwd();
char* getHeadTitle(char* currentPath, char* ret);
void runForgroundProgram(char* cmd, char** envp);
void redirect(char* cmd, int in, int out, char** envp, int redirectboth, int fromPipe);
void pipeOperator(char* cmd, char** envp);
int only_spaces(char *input);
void alramHandler(int x);
void childHandler(int x, siginfo_t *siginfo, void *context);
void ctrlZHandler(int x);
void hereDoc(char* cmd, char** envp);
void wellEasy(int x);
void execute(char* cmd, char** envp);
void this();
void new_stdoutFD();
char currPath[1024];
char path_id[1024];


int main(int argc, char const *argv[], char* envp[]) {
    /* DO NOT MODIFY THIS. If you do you will get a ZERO. */
    rl_catch_signals = 0;
    /* This is disable readline's default signal handlers, since you are going to install your own.*/
    new_stdoutFD();
    char *cmd;
    sf_pwd(currPath, 1024, 0);

    struct sigaction action;
    action.sa_sigaction = childHandler; /* Note use of sigaction, not handler */
    sigfillset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO; /* Note flag - otherwise NULL in function */
    sigaction (SIGCHLD, &action, NULL);

    if ( signal(SIGALRM, alramHandler) == SIG_ERR)
        printf("signal error");
    if ( signal(SIGTSTP, SIG_IGN) == SIG_ERR)
        printf("signal error");
    if ( signal(SIGUSR2, wellEasy) == SIG_ERR)
        printf("signal error");

    while ((cmd = readline(getHeadTitle(currPath, path_id))) != NULL) {
        if (strcmp(cmd, "") == 0 || only_spaces(cmd))
            continue;
        if (strcmp(cmd, "exit") == 0)
            break;

        char* cmdcpy = calloc(1, strlen(cmd));
        strcpy(cmdcpy, cmd);
        char* token = strtok(cmdcpy, " ");
        if (strcmp(token, "alarm") == 0) {
            int time = atoi(strtok(NULL, " "));
            if (!time)
                printf("Time cannot be 0");
            else {
                alarm(time);
                latest_alarm = time;
            }
        }
        else {
            char* cmdcpy = calloc(1, strlen(cmd));
            strcpy(cmdcpy, cmd);
            char* token = strtok(cmdcpy, " ");
            if (strcmp(token, "cd") == 0) {
                sf_cd(strtok(NULL, " "));
                sf_pwd(currPath, 1024, 0);
            } else {
                int out = 0, in = 0;
                if (strchr(cmd, '>'))
                    out = 1;
                if (strchr(cmd, '<'))
                    in = 1;
                if (strchr(cmd, '|')) {
                    pipeOperator(cmd, envp);
                }else if (strstr(cmd, "&>")) {
                    redirect(cmd, in, out, envp, 1, 0);
                }else if (strstr(cmd, "<<")){
                    int pid = 0;
                    currnetChild++;
                    if((pid = fork()) == 0){
                        hereDoc(cmd, envp);
                    }
                    while(currnetChild > 0);
                }else if (out || in) {
                    redirect(cmd, in, out, envp, 0, 0);
                } else {
                    runForgroundProgram(cmd, envp);
                }

            }
        }
        free(cmdcpy);
    }

    /* Don't forget to free allocated memory, and close file descriptors. */
    free(cmd);

    return EXIT_SUCCESS;
}


void hereDoc(char* cmd, char** envp){
    char* cmdcpy = calloc(1, strlen(cmd));
    strcpy(cmdcpy, cmd);
    char* prog = NULL, *endWord = NULL;
    prog = strtok(cmdcpy, "<<");
    checkforNull(prog);
    endWord = strtok(NULL, "<<");
    checkforNull(endWord);
    endWord = strtok(endWord, " ");
    char* input;
    char* save = malloc(1024);
    int totallength = 0;
    int maxcap = 1024;
    int wheretocpy = 0;
    while((input = readline("")) != NULL){
      if(strcmp(input, endWord) == 0){
        break;
      }
      totallength += strlen(input);
      if(totallength >= 1024){
        save = realloc(save ,2 * maxcap);
        maxcap *= 2;
      }
      strcpy(save+wheretocpy, input);
      wheretocpy += strlen(input);
    }
    int pipefds[2];
    pipe(pipefds);
    int pid = 0;
    int stdin_save = dup(STDIN_FILENO);
    if((pid = fork()) == 0){
        dup2(pipefds[1], 1);
        close(pipefds[0]);
        printf("%s", save);
        close(pipefds[1]);
        close(pipefds[0]);
        exit(0);
    }else{
        dup2(pipefds[0], 0);
        close(pipefds[1]);
        execute(prog, envp);
        close(pipefds[0]);
        close(pipefds[1]);
        exit(0);
    }
    dup2(stdin_save, STDIN_FILENO);
    close(stdin_save);
    close(pipefds[0]);
    close(pipefds[1]);
    free(save);
    free(input);
}

