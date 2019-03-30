// mymysh.c ... a small shell
// Started by John Shepherd, September 2018
// Completed by Michael Wang (z5016071), September/October 2018

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <glob.h>
#include <assert.h>
#include <fcntl.h>
#include "history.h"

// This is defined in string.h
// BUT ONLY if you use -std=gnu99
//extern char *strdup(char *);

// Function forward references

void trim(char *);
char **tokenise(char *, char *);
char **fileNameExpand(char **);
void freeTokens(char **);
char *findExecutable(char *, char **);
int isExecutable(char *);
int shellBuiltIn(char *, char *);
int redirection(FILE **, char **);
void pwd(void);
int cd(char *);
int errorPath(char, char *);
int pathExists(char *);
int isDir(char *);
int readPerm(char *);
int writePerm(char *);
void printExe(char *exe);
void printReturn(int);
void errorExit(char *);
void tokenMemoryErrorCheck(char **, char *);
void prompt(void);


// Global Constants

#define MAXLINE 200

// Global Data
/* none ... unless you want some */


// Main program
// Set up enviroment and then run main loop
// - read command, execute command, repeat

int main(int argc, char *argv[], char *envp[])
{
   pid_t pid;   // pid of child process
   int p[2];   // pipe file descriptor
   int stat;   // return status of child
   int built_in;  // return status of shellBuiltIn
   int redirect;  // return status of redirection
   char **path;   // array of directory names
   char **tok_line;  // tokenised command line
   char *exe;  // name of command
   char file_str[MAXLINE];   // stores each line of a file
   FILE *fp;   // file pointer for input/output redirection
   int cmdNo;  // command number
   int seqNo;  // sequence number in HISTFILE
   int i;       // generic index

   // set up command PATH from environment variable
   for (i = 0; envp[i] != NULL; i++) {
      if (strncmp(envp[i], "PATH=", 5) == 0) break;
   }
   if (envp[i] == NULL)
      path = tokenise("/bin:/usr/bin",":");
   else
      // &envp[i][5] skips over "PATH=" prefix
      path = tokenise(&envp[i][5],":");
#ifdef DBUG
   for (i = 0; path[i] != NULL;i++)
      printf("path[%d] = %s\n",i,path[i]);
#endif

   // initialise command history
   // - use content of ~/.mymysh_history file if it exists

   cmdNo = initCommandHistory();

   // main loop: print prompt, read line, execute command

   char line[MAXLINE];
   prompt();
   while (fgets(line, MAXLINE, stdin) != NULL) {
      // remove leading/trailing space
      trim(line);

      // ignore if empty command
      if (strcmp(line, "") == 0) { prompt(); continue; }

      // handle ! history substitution
      if (line[0] == '!') {
         // check if valid history substitution
         if (((sscanf(line, "!%d", &seqNo) == 1) || line[1] == '!') && (line[1] != ' ')) {
            // set seqNo to previous cmdNo if "!!" entered
            if (line[1] == '!') seqNo = cmdNo-1;
            // set line to previous command from history
            if (getCommandFromHistory(seqNo) != NULL) {
               strcpy(line, getCommandFromHistory(seqNo));
               printf("%s\n", line);
            } else {
               printf("No command #%d\n", seqNo);
               prompt();
               continue;
            }
         } else {
            printf("Invalid history substitution\n");
            prompt();
            continue;
         }
      }

      // tokenise the command line
      tok_line = tokenise(line, " ");

      // handle *?[~ filename expansion
      tok_line = fileNameExpand(tok_line);

      // handle shell built-ins
      built_in = shellBuiltIn(tok_line[0], tok_line[1]);
      if (built_in == 1) { freeTokens(tok_line); break; } // terminate shell if "exit" command
      if (built_in == 2) { freeTokens(tok_line); prompt(); continue; } // continue if cd to invalid dir
      
      // handle program execution
      if (!built_in) {
         // check for input/output redirections
         if ((redirect = redirection(&fp, tok_line)) < 0) {
            freeTokens(tok_line);
            prompt();
            continue; 
         }

         // check if executable is found
         if ((exe = findExecutable(tok_line[0], path)) == NULL) {
            printf("%s: Command not found\n", tok_line[0]);
            free(exe);
            freeTokens(tok_line);
            prompt();
            continue;
         }

         // create a pipe
         if (pipe(p) == -1)
            errorExit("pipe() failed");
         
         // create a child process
         pid = fork();
         if (pid > 0) { // parent process
            // sort out any redirections
            if (redirect > 0) {
               if (redirect == 1) {
                  close(p[0]);
                  // write from file to p[1]
                  while (fgets(file_str, MAXLINE, fp) != NULL)
                     write(p[1], file_str, strlen(file_str));
                  close(p[1]);
               }
               fclose(fp);
            }
            
            // parent shell process waits for child to complete
            wait(&stat);
            
            // print command return status
            printReturn(stat);
         } else if (pid == 0) { // child process
            // print pathname of command executable
            printExe(exe);
            
            // sort out input redirection
            if (redirect == 1) {
               // copy p[0] to stdin
               if (dup2(p[0], 0) < 0)
                  errorExit("dup2() failed");
               close(p[1]);
            }
            
            // sort out output redirection
            if (redirect == 2) {
               // copy fileno(fp) to stdout and stderr
               if (dup2(fileno(fp), 1) < 0 || dup2(fileno(fp), 2) < 0)
                  errorExit("dup2() failed");
               close(fileno(fp));
            }
            
            // execute exe
            if (execve(exe, tok_line, envp) == -1) {
               fprintf(stderr, "%s: unknown type of executable\n", exe);
               exit(255);
            }
         } else {
            errorExit("fork() failed");
         }
         free(exe);
      }
      // add to command history
      addToCommandHistory(line, cmdNo);

      // free memory allocated to tok_line
      freeTokens(tok_line);

      // print another prompt
      prompt();

      // iterate cmdNo
      cmdNo++;
   }
   // free memory allocated to path
   freeTokens(path);
   
   // save and clean up CommandHistory
   saveCommandHistory();
   cleanCommandHistory();
   
   printf("\n");
   return(EXIT_SUCCESS);
}

// fileNameExpand: expand any wildcards in command-line args
// - returns a possibly larger set of tokens
char **fileNameExpand(char **tokens)
{
   int i, j, N = 1;
   char **tokens_exp;
   glob_t globbuf;

   if (tokens[0] == NULL) return tokens;
   
   // set up tokens_exp
   tokens_exp = malloc(2*sizeof(char *));
   tokenMemoryErrorCheck(tokens_exp, "malloc()");
   tokens_exp[0] = strdup(tokens[0]);
   
   // loop through tokens
   for (i = 1; tokens[i] != NULL; i++) {
      // initialise globbuf and allocate additional memory to tokens_exp
      if (glob(tokens[i], GLOB_NOCHECK | GLOB_TILDE, NULL, &globbuf) == 0) {
         tokens_exp = realloc(tokens_exp, (N+1+globbuf.gl_pathc)*sizeof(char *));
         tokenMemoryErrorCheck(tokens_exp, "realloc()");
         // copy matched pathnames to tokens_exp
         for (j = 0; j < globbuf.gl_pathc; j++) {
            tokens_exp[N] = strdup(globbuf.gl_pathv[j]);
            N++;
         }
      } else {
         freeTokens(tokens_exp);
         globfree(&globbuf);
         errorExit("glob() failed");
      }
      globfree(&globbuf);
   }
   // set final array element to NULL
   tokens_exp[N] = NULL;
   
   // free memory allocated to tokens
   freeTokens(tokens);
   return tokens_exp;
}

// shellBuiltIn: Handle shell built-in commands
// - return 1 if "exit" command, 2 if cd fails, 3 if other shell built-in command, 0 otherwise
int shellBuiltIn(char *cmd, char *arg)
{
   // "exit" command
   if (strcmp(cmd, "exit") == 0)
      return 1;
   // "h" or "history" command
   if ((strcmp(cmd, "h") == 0) || (strcmp(cmd, "history") == 0)) {
      showCommandHistory(stdout);
      return 3;
   }
   // "pwd" command
   if (strcmp(cmd, "pwd") == 0) {
      pwd();
      return 3;
   } 
   // "cd" command
   if (strcmp(cmd, "cd") == 0)
      return cd(arg);
   return 0;
}

// redirect: check for input/out redirections
// - return 1 if '<', 2 if '>', 0 if no redirections, -1 if redirect caused an error
int redirection(FILE **fp, char **tokens) {
   // if '<' or '>' is the first array element
   if (strcmp(tokens[0], "<") == 0 || strcmp(tokens[0], ">") == 0) {
      printf("Invalid i/o redirection\n");
      return -1;
   }
   
   // loop through tokens
   for (int i = 1; tokens[i] != NULL; i++) {
      // check if command line contains the tokens '<' or '>'
      if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0) {
         // redirect is the last token
         if (tokens[i+1] == NULL) {
            printf("Invalid i/o redirection\n");
            return -1;
         } 
         // redirect is the second last token
         if (tokens[i+2] == NULL) {
            if (strcmp(tokens[i], "<") == 0) { // handle input redirection
               // error check on last token
               if (errorPath('<', tokens[i+1]))
                  return -1;
               // check if last token is directory
               if (isDir(tokens[i+1])) {
                  free(tokens[i]);
                  free(tokens[i+1]);
                  tokens[i] = NULL;
                  return 0;
               }
               // open fp for reading
               *fp = fopen(tokens[i+1], "r");
               free(tokens[i+1]);
               free(tokens[i]);
               tokens[i] = NULL;
               return 1;
            } else { // handle output redirection
               // check if last token is directory
               if (isDir(tokens[i+1])) { 
                  printf("Output redirection: Is a directory\n");
                  return -1;
               }
               // open fp for writing
               if ((*fp = fopen(tokens[i+1], "w")) == NULL)
                  // error check on last token
                  if (errorPath('>', tokens[i+1]))
                     return -1;
               free(tokens[i+1]);
               free(tokens[i]);
               tokens[i] = NULL;
               return 2;
            }
         }
         // redirect token in an invalid token location
         printf("Invalid i/o redirection\n");
         return -1;
      }
   }
   return 0;
}

// findExecutable: look for executable in PATH
char *findExecutable(char *cmd, char **path)
{
   char executable[MAXLINE];
   executable[0] = '\0';
   if (cmd[0] == '/' || cmd[0] == '.') {
      strcpy(executable, cmd);
      if (!isExecutable(executable))
         executable[0] = '\0';
   } 
   else {
      int i;
      for (i = 0; path[i] != NULL; i++) {
         sprintf(executable, "%s/%s", path[i], cmd);
         if (isExecutable(executable)) break;
      }
      if (path[i] == NULL) executable[0] = '\0';
   }
   if (executable[0] == '\0')
      return NULL;
   else
      return strdup(executable);
}

// isExecutable: check whether this process can execute a file
int isExecutable(char *cmd)
{
   struct stat s;
   // must be accessible
   if (stat(cmd, &s) < 0)
      return 0;
   // must be a regular file
   //if (!(s.st_mode & S_IFREG))
   if (!S_ISREG(s.st_mode))
      return 0;
   // if it's owner executable by us, ok
   if (s.st_uid == getuid() && s.st_mode & S_IXUSR)
      return 1;
   // if it's group executable by us, ok
   if (s.st_gid == getgid() && s.st_mode & S_IXGRP)
      return 1;
   // if it's other executable by us, ok
   if (s.st_mode & S_IXOTH)
      return 1;
   return 0;
}

// tokenise: split a string around a set of separators
// create an array of separate strings
// final array element contains NULL
char **tokenise(char *str, char *sep)
{
   // temp copy of string, because strtok() mangles it
   char *tmp;
   // count tokens
   tmp = strdup(str);
   int n = 0;
   strtok(tmp, sep); n++;
   while (strtok(NULL, sep) != NULL) n++;
   free(tmp);
   // allocate array for argv strings
   char **strings = malloc((n+1)*sizeof(char *));
   assert(strings != NULL);
   // now tokenise and fill array
   tmp = strdup(str);
   char *next; int i = 0;
   next = strtok(tmp, sep);
   strings[i++] = strdup(next);
   while ((next = strtok(NULL,sep)) != NULL)
      strings[i++] = strdup(next);
   strings[i] = NULL;
   free(tmp);
   return strings;
}

// freeTokens: free memory associated with array of tokens
void freeTokens(char **toks)
{
   for (int i = 0; toks[i] != NULL; i++)
      free(toks[i]);
   free(toks);
}

// trim: remove leading/trailing spaces from a string
void trim(char *str)
{
   int first, last;
   first = 0;
   while (isspace(str[first])) first++;
   last  = strlen(str)-1;
   while (isspace(str[last])) last--;
   int i, j = 0;
   for (i = first; i <= last; i++) str[j++] = str[i];
   str[j] = '\0';
}

// pwd: print current working directory
void pwd(void)
{
   char wd[MAXLINE];
   if (getcwd(wd, sizeof(wd)) != NULL)
      printf("%s\n", wd);
   else
      errorExit("getcwd() failed");
}

// cd: change directories and print new working directory
int cd(char *arg)
{
   char wd[MAXLINE];
   // get current working directory
   if (getcwd(wd, sizeof(wd)) != NULL) {
      // set wd to the new working directory
      if (arg == NULL) {
         strcpy(wd, getenv("HOME"));
      } else if (arg[0] == '/') {
         strcpy(wd, arg);
      } else {
         strcat(wd, "/");
         strcat(wd, arg);
      }
      // change to new working directory
      if (chdir(wd) == 0) {
         pwd();
      } else {
         printf("%s: No such file or directory\n", arg);
         return 2;
      }
   } else {
      errorExit("getcwd() failed");
   }
   return 3;
}

int errorPath(char c, char *path) {
   // handle input redirection path errors
   if (c == '<') {
      // check if path exists
      if (!pathExists(path)) {
         printf("Input redirection: No such file or directory\n");
         return 1;
      }
      // check for read permissions
      if (!readPerm(path)) {
         printf("Input redirection: Permission denied\n");
         return 1;
      }
   }
   // handle output redirection path errors
   if (c == '>') {
      // check for write permissions
      char wd[MAXLINE];
      if (getcwd(wd, sizeof(wd)) != NULL) {
         if (!writePerm(wd)) {
            printf("Output redirection: Permission denied\n");
            return 1;       
         }
      } else {
         errorExit("getcwd() failed");
      }
      // check if path exists
      if (!pathExists(path)) {
         printf("Output redirection: No such file or directory\n");
         return 1;
      }
   }
   return 0;
}

// pathExists: check if a path exists
// - return 1 if exists, 0 otherwise
int pathExists(char *path) {
   if (access(path, F_OK) != -1)
      return 1;
   return 0;
}

// isDir: check if path is a directory
// - return non-zero if path is a directory, 0 if path is not a directory
int isDir(char *path) {
   struct stat s;
   if (stat(path, &s) != 0)
      return 0;
   return S_ISDIR(s.st_mode);
}

// readPerm: check if path has read permissions
// - return 1 if has read permissions, 0 otherwise
int readPerm(char *path) {
   if (access(path, R_OK) != -1)
      return 1;
   return 0;
}

// writePerm: check if path has write permissions
// - return 1 if has write permissions, 0 otherwise
int writePerm(char *path) {
   if (access(path, W_OK) != -1)
      return 1;
   return 0;
}

// printExe: print full command pathname
void printExe(char *exe)
{
   printf("Running %s ...\n--------------------\n", exe);
}

// printReturn: print the command's return status
void printReturn(int stat)
{
   printf("--------------------\nReturns %d\n", WEXITSTATUS(stat));
}

// errorExit: print error message and exits the program
void errorExit(char *msg)
{
   perror(msg);
   exit(EXIT_FAILURE);
}

// tokenMemoryErrorCheck: exit program if memory allocation fails
void tokenMemoryErrorCheck(char **tokens, char *func)
{
   char msg[20];
   sprintf(msg, "%s failed\n", func);
   if (tokens == NULL) errorExit(msg);
}

// prompt: print a shell prompt
// done as a function to allow switching to $PS1
void prompt(void)
{
   printf("mymysh$ ");
}
