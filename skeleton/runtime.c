/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.3 $
 *    Last Modification: $Date: 2009/10/12 20:50:12 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.3  2009/10/12 20:50:12  jot836
 *    Commented tsh C files
 *
 *    Revision 1.2  2009/10/11 04:45:50  npb853
 *    Changing the identation of the project to be GNU.
 *
 *    Revision 1.1  2005/10/13 05:24:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l
{
  pid_t pid;
  bool done;
  struct bgjob_l* next;
  char *cmdline;
} bgjobL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

/************Function Prototypes******************************************/
/* run command */
static void
RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void
RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool
ResolveExternalCmd(commandT*, char**);
/* forks and runs a external program */
static void
Exec(commandT*, bool, bool);
/* runs a builtin command */
static void
RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool
IsBuiltIn(char*);
/* returns an array with the path environment elements */
static char** 
getpath(char*);
/* frees the above array */
static void 
freepath(char**);
/************External Declaration*****************************************/

/**************Implementation***********************************************/


/*
 * RunCmd
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *
 * returns: none
 *
 * Runs the given command.
 */
void
RunCmd(commandT* cmd)
{
  RunCmdFork(cmd, TRUE);
} /* RunCmd */


/*
 * RunCmdFork
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *   bool fork: whether to fork
 *
 * returns: none
 *
 * Runs a command, switching between built-in and external mode
 * depending on cmd->argv[0].
 */
void
RunCmdFork(commandT* cmd, bool fork)
{
  int i;
  
  if (cmd->argc <= 0)
    return;
  
  // check to see if any parts of command are env vars
  for (i = 0; i < cmd->argc; i++) {
    if (cmd->argv[i][0] == '$') {
      char *var = getenv(cmd->argv[i] + 1);
      if (var != NULL) {
	free(cmd->argv[i]);
	cmd->argv[i] = (char *)malloc(sizeof(char) * (1 + strlen(var)));
	strcpy(cmd->argv[i], var);
      } else {
	printf("%s: Undefined variable.\n", cmd->argv[i]);
	return;
      }
    }
  }

  if (IsBuiltIn(cmd->argv[0]))
    {
      RunBuiltInCmd(cmd);
    }
  else
    {
      RunExternalCmd(cmd, fork);
    }
} /* RunCmdFork */


/*
 * RunCmdBg
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *
 * returns: none
 *
 * Runs a command in the background.
 */
void
RunCmdBg(commandT* cmd)
{
  // TODO
} /* RunCmdBg */


/*
 * RunCmdPipe
 *
 * arguments:
 *   commandT *cmd1: the commandT struct for the left hand side of the pipe
 *   commandT *cmd2: the commandT struct for the right hand side of the pipe
 *
 * returns: none
 *
 * Runs two commands, redirecting standard output from the first to
 * standard input on the second.
 */
void
RunCmdPipe(commandT* cmd1, commandT* cmd2)
{
} /* RunCmdPipe */


/*
 * RunCmdRedirOut
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *   char *file: the file to be used for standard output
 *
 * returns: none
 *
 * Runs a command, redirecting standard output to a file.
 */
void
RunCmdRedirOut(commandT* cmd, char* file)
{
} /* RunCmdRedirOut */


/*
 * RunCmdRedirIn
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *   char *file: the file to be used for standard input
 *
 * returns: none
 *
 * Runs a command, redirecting a file to standard input.
 */
void
RunCmdRedirIn(commandT* cmd, char* file)
{
}  /* RunCmdRedirIn */

/*
 * IntFgProc
 *
 * arguments: none
 *
 * returns: none
 *
 * Send interrupt signal to the foreground process group.
 */
void
IntFgProc()
{
  if (fgpid != -1) 
    {
      kill(-fgpid, SIGINT);
      fgpid = -1;
    }
}

/*
 * RunExternalCmd
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *   bool fork: whether to fork
 *
 * returns: none
 *
 * Tries to run an external command.
 */
static void
RunExternalCmd(commandT* cmd, bool fork)
{
  char** path = getpath(cmd->argv[0]);
  bool bg = 0;
  if (strcmp(cmd->argv[cmd->argc - 1],"&") == 0) {
    free(cmd->argv[cmd->argc - 1]);
    cmd->argv[cmd->argc - 1] = NULL;
    cmd->argc = cmd->argc - 1;
    bg = 1;
  }
  if (ResolveExternalCmd(cmd, path))
    {
      Exec(cmd, fork, bg);
    }
  else
    {
      printf("./tsh-ref: line 1: %s: No such file or directory\n", cmd->argv[0]);
    }
  freepath(path);
}  /* RunExternalCmd */


/*
 * ResolveExternalCmd
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *
 * returns: bool: whether the given command exists
 *
 * Determines whether the command to be run actually exists.
 */
static bool
ResolveExternalCmd(commandT* cmd, char** path)
{
  struct stat buf;
  int stres;
  int i = 0;
  // look through paths to find an existing file,
  // then set the path field in cmd if successful
  while(path[i] != NULL) 
    {
      stres = stat(path[i], &buf);
      if (stres == 0) {
	cmd->path = malloc(sizeof(char) * (1 + strlen(path[i])));
	strcpy(cmd->path, path[i]);
	return 1;
      }
      i++;
    }   
  // failed to find anything in paths, so try relative path
  cmd->path = malloc(sizeof(char) * (1 + strlen(cmd->argv[0])));
  strcpy(cmd->path, cmd->argv[0]);
  return(stat(cmd->argv[0], &buf) == 0);
} /* ResolveExternalCmd */


/*
 * Exec
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *   bool forceFork: whether to fork
 *   bool bg: run in bg or not
 *
 * returns: none
 *
 * Executes an external command.
 */
static void
Exec(commandT* cmd, bool forceFork, bool bg)
{
  int pid;
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);

  // block sigchld signals until recording the new process id,
  // and then fork the foreground process
  sigprocmask(SIG_BLOCK, &mask, NULL);
  pid = fork();

  if (pid == 0) {
    // child process (change pg id so int signals are only sent to the shell)
    setpgid(0,0);
    execv(cmd->path, cmd->argv);
  } else {
    // parent process
    if (bg) {
      addbgjob(pid, cmd);
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
    } else {
      fgpid = pid;
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
      while(fgpid != -1) sleep(1);
    }
  }
} /* Exec */


/*
 * IsBuiltIn
 *
 * arguments:
 *   char *cmd: a command string (e.g. the first token of the command line)
 *
 * returns: bool: TRUE if the command string corresponds to a built-in
 *                command, else FALSE.
 *
 * Checks whether the given string corresponds to a supported built-in
 * command.
 */
static bool
IsBuiltIn(char* cmd)
{
  char *cmdtoks;
  if (strcmp(cmd, "cd") == 0)
    return TRUE;
  cmdtoks = (char *)malloc(sizeof(char) * (1 + strlen(cmd)));
  strcpy(cmdtoks, cmd);
  if (strtok(cmdtoks, "=") != NULL &&
      strtok(NULL, "=") != NULL &&
      strtok(NULL, "=") == NULL) {
    free(cmdtoks);
    return TRUE;
  }
  free(cmdtoks);
  return FALSE;
} /* IsBuiltIn */


/*
 * RunBuiltInCmd
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *
 * returns: none
 *
 * Runs a built-in command.
 */
static void
RunBuiltInCmd(commandT* cmd)
{
  char *cmdtoks = (char *)malloc(sizeof(char) * (1 + strlen(cmd->argv[0])));
  strcpy(cmdtoks, cmd->argv[0]);
  char *envvar;

  // cd command - defaults to homedir
  if (strcmp(cmd->argv[0], "cd") == 0) {
      if (cmd->argc > 1)
	chdir(cmd->argv[1]);
      else
	chdir(getenv("HOME"));
    }

  // do environment update if it has the right form
  envvar = strtok(cmdtoks, "=");
  if (envvar != NULL) {
    setenv(envvar, strtok(NULL, "="), TRUE);
  }
  free(cmdtoks);
} /* RunBuiltInCmd */


/*
 * CheckJobs
 *
 * arguments: none
 *
 * returns: none
 *
 * Checks the status of running jobs.
 */
void
CheckJobs()
{
  bgjobL *current;
  bgjobL *prev;
  bgjobL *temp;
  int i;
  current = bgjobs;
  prev = NULL;
  i = 1;
  while (current != NULL) {
    if (current->done) {
      if (prev == NULL) {
	bgjobs = current->next;
      } else {
	prev->next = current->next;
      }
      temp = current->next;
      printf("[%d]   Done                    %s\n", i, current->cmdline);
      free(current->cmdline);
      free(current);
      current = temp;
    } else {
      prev = current;
      current = current->next;
    }
    i++;
  }
} /* CheckJobs */

/*
 * getpath
 *
 * arguments: cmd, the command to exectue (appended to all path elements)
 *
 * returns: an array of path elements with cmd appended
 *
 * Returns an array of path strings, with cmd pasted on the end.
 */
char**
getpath(char* cmd)
{
  int i;
  char* d; 
  char* path;
  char* mypath;

  path = getenv("PATH");
  // copy it to avoid clobbering path when tokenizing
  mypath = (char *)malloc(sizeof(char) * (1 + strlen(path)));  
  strcpy(mypath, path);
  char** paths = (char**)malloc(sizeof(char *) * 100);

  // tokenize the path by ":" and build an array
  d = strtok(mypath, ":");
  i = 0;
  while (d != NULL && i < 100) {
    paths[i] = (char *)malloc(sizeof(char) * (2 + strlen(d) + strlen(cmd)));
    strcpy(paths[i], d);
    strcat(paths[i], "/");
    strcat(paths[i], cmd);
    i++;
    d = strtok(NULL, ":");
  }
  paths[i] = NULL;

  free(mypath);
  return paths;
}

/*
 * freepath
 *
 * arguments: path, a string array
 *
 * returns: void
 *
 * frees the strings in path and path
 */
void
freepath(char** path)
{
  int i = 0;
  while (path[i] != NULL)
    free(path[i++]);
  free(path);
}

/* add a bg job to the list */
void
addbgjob(pid_t pid, commandT* cmd)
{
  bgjobL *oldbgjobs = bgjobs;
  int i;
  int cmdlinelen = 1;
  bgjobs = (bgjobL *)malloc(sizeof(bgjobL));
  bgjobs->pid = pid;
  bgjobs->done = 0;
  bgjobs->next = oldbgjobs;
  for (i = 0; i < cmd->argc; i++) {
    cmdlinelen += strlen(cmd->argv[i]);
  }
  bgjobs->cmdline = (char *)malloc(sizeof(char) * (cmdlinelen + cmd->argc));
  strcpy(bgjobs->cmdline, cmd->argv[0]);
  for (i = 1; i < cmd->argc; i++) {
    strcat(bgjobs->cmdline, " ");
    strcat(bgjobs->cmdline, cmd->argv[i]);
  }
}
/* remove a bg job from the list */
void
removebgjob(pid_t pid)
{
  bgjobL *current;
  current = bgjobs;
  while (current != NULL) {
    if (current->pid == pid) {
      current->done = 1;
    }
    current = current->next;
  }
}
