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
  int jobid;
  state_t state;
  struct bgjob_l* next;
  char *cmdline;
} bgjobL;

typedef struct alias_l
{
  char* name;
  char* value;
  struct alias_l* next;
} aliasL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;
/* list of user-defined command aliases */
aliasL *aliasLst = NULL;

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
/* runs a sequence of cmds piped together */
void
RunCmdPipe(commandT*, bool, bool);
/* checks whether a command is a builtin command */
static bool
IsBuiltIn(char*);
/* returns an array with the path environment elements */
static char** 
getpath(char*);
/* frees the above array */
static void 
freepath(char**);
/* add a bg job to the list */
static bgjobL*
addbgjob(pid_t, commandT*, bool);
/* display the jobs list */
static void
showjobs();
/* foreground a specific job by job id */
static void
foregroundjob(int);
/* wait for the fg job to finish */
static void
waitforfg();
/* do bg built in command */
static void
dobg(int);
/* forks and runs a command connected to one end of a pipe */
void
ExecPipe(commandT*, int[2], int[2], pipeop_t);
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
 * splitPipeCmd
 *
 * arguments:
 *   char* cmdline: the command line to parse
 *
 * returns: the head of a linked list of cmd structs
 *
 * Parses a command line containing pipe symbols into a linked list of cmd structs
 */
commandT*
splitPipeCmd(char* cmdline) {
  int i;
  static bool pipeFound = FALSE;
  for (i = 0; i < strlen(cmdline); i++) {
    if (cmdline[i] == '|') {
      pipeFound = TRUE;
      // split cmd into two command structs
      char* first = (char*)malloc(sizeof(char)*(i+1));
      memset(first, '\0', (i+1)*sizeof(char));
      strncpy(first,cmdline,i-1);
      char* second = (char*)malloc(sizeof(char)*(strlen(cmdline)-i));
      strncpy(first,cmdline,i);
      strcpy(second,cmdline+i+1);
      
      commandT* pipeFrom = getCommand(first);
      pipeFrom->cmdline = (char *)malloc(sizeof(char) * (strlen(first) + 1));
      strcpy(pipeFrom->cmdline, first); 
      pipeFrom->pipeTo = splitPipeCmd(second);
      if (pipeFrom->pipeTo) {
        pipeFrom->pipeTo->cmdline = (char *)malloc(sizeof(char) * (strlen(second) + 1));
        strcpy(pipeFrom->pipeTo->cmdline, second);
      }
      free(first);
      free(second);
      return pipeFrom;
    }
  }
  if (pipeFound) {
    pipeFound = FALSE;
    return getCommand(cmdline);
  } else {
    return NULL;
  }
}
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
  aliasL* aliasIter;
  char* newCmdline;
  bool foundAlias = FALSE;
  commandT* pipeFrom = NULL;
  commandT* curCmd = NULL;
  
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
  pipeFrom = splitPipeCmd(cmd->cmdline);
  
  if (IsBuiltIn(cmd->argv[0]))
    {
      RunBuiltInCmd(cmd);
    }
  else
    {
      // check if any argv's are an alias
      for (i = 0; i < cmd->argc; ++i) {
        aliasIter = aliasLst;
        while (aliasIter) {
          if (strcmp(cmd->argv[i],aliasIter->name) == 0) {
            strcpy(cmd->argv[i],aliasIter->value);
            foundAlias = TRUE;
          }
          aliasIter = aliasIter->next;
        }
      }
      if (foundAlias) {
        // build new commandT
        newCmdline = (char*)malloc(sizeof(char) * 128);
        memset(newCmdline,'\0',128);
        for (i = 0; i < cmd->argc; ++i) {
          strcat(newCmdline,cmd->argv[i]);
          strcat(newCmdline," ");
        }
        
        cmd = getCommand(newCmdline);
        cmd->cmdline = (char *)malloc(sizeof(char) * (strlen(newCmdline) + 1));
        strcpy(cmd->cmdline, newCmdline); 
      }
      if (IsBuiltIn(cmd->argv[0]))
      {
        RunBuiltInCmd(cmd);
      }
      else {
        if (pipeFrom) {
          RunExternalCmd(pipeFrom, fork);
          curCmd = pipeFrom;
          // free all the commands in the linked list after RunExternalCmd is done using them
          while (pipeFrom) {
            curCmd = pipeFrom;
            pipeFrom = pipeFrom->pipeTo;
            freeCommand(curCmd);
          }
        } else {
          RunExternalCmd(cmd, fork);
        }
      }
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
 *   commandT *cmd1: the commandT struct representing the left hand side of the first pipe
 *   bool forceFork: whether to fork
 *   bool bg: whether the cmd should be backgrounded
 *
 * returns: none
 *
 * Runs a sequence of commands, piping output from one to input of the next.
 */
void
RunCmdPipe(commandT* cmd1, bool forceFork, bool bg)
{
  int pipeID[2];
  int pipeID2[2];
  pipeop_t op = WRITE;
  
  commandT* curCmd;
  
  for (curCmd = cmd1; curCmd != NULL; ) {
    if (op == WRITE) {
      pipe(pipeID);
      ExecPipe(curCmd, pipeID, pipeID2, WRITE);
      sleep(1);
      curCmd = curCmd->pipeTo;
      if (curCmd->pipeTo) {
        op = READWRITE;
      } else {
        op = READ;
      }
    } else if (op == READWRITE) {
      pipe(pipeID2);
      ExecPipe(curCmd, pipeID, pipeID2, READWRITE);
      sleep(1);
      fflush(stdout);
      close(pipeID[0]);
      close(pipeID[1]);
      
      pipeID[0] = pipeID2[0];
      pipeID[1] = pipeID2[1];
      curCmd = curCmd->pipeTo;
      if (curCmd->pipeTo) {
        op = READWRITE;
      } else {
        op = READ;
      }
    } else if (op == READ) {
      ExecPipe(curCmd, pipeID, pipeID2, READ);
      sleep(1);
      close(pipeID[0]);
      close(pipeID[1]);
      fflush(stdout);
      break;
    }
  }
  if (pipeID2[0] || pipeID2[1]) {
    close(pipeID2[0]);
    close(pipeID2[1]);
  }
  fflush(stdout);
} /* RunCmdPipe */

/*
 * ExecPipe
 *
 * arguments:
 *   commandT *cmd: the command to execute
 *   int pipeID: the first pipe
 *   int pipeID: the second pipe, if both input and output are going through pipes
 *   pipeop_t op: indicates what pipe ends should be used
 *
 * returns: none
 *
 * Executes a command, redirecting input/output to pipes as necessary.
 */
void
ExecPipe(commandT *cmd, int pipeID[2], int pipeID2[2], pipeop_t op)
{
  int pid, status;
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &mask, NULL);
  
  pid = fork();
  if (pid == 0) {
    setpgid(0,0);
    if (op == WRITE) {
      close(pipeID[0]);
      close(1);
      dup(pipeID[1]);
      close(pipeID[1]);
    }
    else if (op == READ) { // read end of pipe
      close(pipeID[1]);
      close(0);
      dup(pipeID[0]);
      close(pipeID[0]);
    } else if (op == READWRITE) {
      close(pipeID2[0]); // stdin of right pipe
      close(pipeID[1]); // stdout of left pipe
      close(0); // stdin
      dup(pipeID[0]);
      close(pipeID[0]);
      close(1); // stdout
      dup(pipeID2[1]);
      close(pipeID2[1]);
    }
    execv(cmd->path,cmd->argv);
    
  } else {
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    waitpid(pid,&status,WNOHANG);
  }
} /* ExecPipe */


/*
 * RedirIO
 *
 * arguments:
 *   commandT *cmd: the command to be run
 *
 * returns: none
 *
 * Extracts redirection operators from argv[] and adjusts input/output files accordingly
 */
void
RedirIO(commandT* cmd)
{
  int i, j, fid;
  char* file;
  char redirOp;
  // check argv for '<' or '>' operators
  for (i=0; i < (cmd->argc - 1); i++) {
    if ((strcmp(cmd->argv[i],">") == 0) || (strcmp(cmd->argv[i],"<") == 0)) {
      redirOp = cmd->argv[i][0];
      file = malloc(sizeof(char*)*strlen(cmd->argv[i+1])+1);
      strcpy(file,cmd->argv[i+1]);
      // remove the redirection from argv[]
      for (j = i; (j+2) < (cmd->argc); j++) {
        cmd->argv[j] = cmd->argv[j+2];
      }
      cmd->argv[j] = NULL;
      cmd->argv[j+1] = NULL;
      cmd->argc -= 2;
      i -= 1;
      // redirect stream to file*
      if (redirOp == '<') {
        fid = open(file, O_RDONLY);
        close(0);
        dup(fid);
        close(fid);
      } else if (redirOp == '>') {
        fid = open(file, O_WRONLY | O_CREAT);
        close(1);
        dup(fid);
        close(fid);
      }
      else {
        printf("%s: can't redirect to this file.",file);
      }
      free(file);
    }
  }
  return;
} /* RedirIO */

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
  bool allCmdsResolved = TRUE;
  if (strcmp(cmd->argv[cmd->argc - 1],"&") == 0) {
    free(cmd->argv[cmd->argc - 1]);
    cmd->argv[cmd->argc - 1] = NULL;
    cmd->argc = cmd->argc - 1;
    bg = 1;
  }
  if (ResolveExternalCmd(cmd, path))
    {
      if (cmd->pipeTo) {
        commandT* cmdCpy = cmd->pipeTo;
        while (cmdCpy != NULL) {
          freepath(path);
          path = getpath(cmdCpy->argv[0]);
          if (ResolveExternalCmd(cmdCpy, path) == FALSE) {
            allCmdsResolved = FALSE;
            break;
          }
          cmdCpy = cmdCpy->pipeTo;
        }
        if (allCmdsResolved) {
          RunCmdPipe(cmd, fork, bg);
        }
      }
      else {
        Exec(cmd, fork, bg);
      }
    }
  else
    {
      printf("/bin/bash: line 6: %s: command not found\n", cmd->argv[0]);
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
    // redirect I/O if necessary
    RedirIO(cmd);
    
    // exec the command
    execv(cmd->path, cmd->argv);
  } else {
    // parent process
    // add the job to the bg job list
    if (bg) {
      addbgjob(pid, cmd, TRUE);
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
    } else {
      // record that this is a fg job
      fgpid = pid;
      (addbgjob(pid, cmd, FALSE))->state = FG;
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
      waitforfg(pid);
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
  if (strcmp(cmd, "jobs") == 0)
    return TRUE;
  if (strcmp(cmd, "fg") == 0)
    return TRUE;
  if (strcmp(cmd, "bg") == 0)
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
  if ((strcmp(cmd,"alias") == 0) || (strcmp(cmd,"unalias") == 0)) {
    return TRUE;
  }
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
  // jobs command
  if (strcmp(cmd->argv[0], "jobs") == 0)
    showjobs();
  if (strcmp(cmd->argv[0], "fg") == 0)
    foregroundjob(atoi(cmd->argv[1]));
  if (strcmp(cmd->argv[0], "bg") == 0)
    dobg(atoi(cmd->argv[1]));

  // do environment update if it has the right form
  envvar = strtok(cmdtoks, "=");
  if (envvar != NULL && strcmp(envvar,cmd->argv[0])) {
    setenv(envvar, strtok(NULL, "="), TRUE);
  }
  if (strcmp(cmd->argv[0], "alias") == 0) {
    RunAliasCmd(cmd,FALSE);
  }
  if (strcmp(cmd->argv[0],"unalias") == 0) {
    RunAliasCmd(cmd,TRUE);
  }
  free(cmdtoks);
} /* RunBuiltInCmd */

void
RunAliasCmd(commandT* cmd, bool unalias)
{
  aliasL *curAlias, *prevAlias, *newAlias;
  char* cmdtoks;
  
  if (unalias) {
    prevAlias = NULL;
    for (curAlias = aliasLst; curAlias != NULL; curAlias = curAlias->next) {
      if (strcmp(cmd->argv[1],curAlias->name) == 0) {
        if (prevAlias) {
          prevAlias->next = curAlias->next;
        } else {
          aliasLst = curAlias->next;
        }
        free(curAlias);
        return;
      }
      prevAlias = curAlias;
    }
    printf("/bin/bash: line %d: unalias: %s: not found\n",3,cmd->argv[1]);
    return;
  }
  else if (cmd->argc > 1) {
    cmdtoks = (char *)malloc(sizeof(char) * (1 + strlen(cmd->argv[1])));
    strcpy(cmdtoks, cmd->argv[1]);
    
    newAlias = (aliasL *)malloc(sizeof(aliasL));
    newAlias->name = (char*)malloc(sizeof(char) * 80);
    newAlias->value = (char*)malloc(sizeof(char) * 80);
    strcpy(newAlias->name,strtok(cmdtoks,"="));
    strcpy(newAlias->value,strtok(NULL,"="));
    newAlias->next = NULL;
    free(cmdtoks);
    
    // check if the alias exists already; aliasLst is sorted alphabetically
    curAlias = aliasLst;
    prevAlias = NULL;
    while (curAlias) {
      if (strcmp(newAlias->name,curAlias->name) == 0) {
        if (prevAlias) {
          prevAlias->next = newAlias;
        } else {
          aliasLst = newAlias;
        }
          newAlias->next = curAlias->next;
        free(curAlias->name);
        free(curAlias->value);
        free(curAlias);
        return;
      }
      // alias doesn't exist yet, so insert it at the correct position
      else if (strcmp(newAlias->name,curAlias->name) < 0) {
        if (prevAlias) {
          prevAlias->next = newAlias;
        } else {
          aliasLst = newAlias;
        }
        newAlias->next = curAlias;
        return;
      }
        prevAlias = curAlias;
        curAlias = curAlias->next;
    }
    // if alias doesn't exist yet, insert it in the list in alphabetical order
    if (prevAlias) {
      prevAlias->next = newAlias;
    } else {
      aliasLst = newAlias;
    }
  }
  else {  // print out list of aliases
    for (curAlias = aliasLst; curAlias != NULL; curAlias = curAlias->next) {
      printf("alias %s='%s'\n",curAlias->name,curAlias->value);
    }
  }
} /* RunAliasCmd */

/*
 * CheckJobs
 *
 * arguments: none
 *
 * returns: none
 *
 * Checks the status of running jobs, and delete jobs from the list if necessary.
 */
void
CheckJobs()
{
  bgjobL *current;
  bgjobL *prev;
  bgjobL *temp;
  current = bgjobs;
  prev = NULL;
  while (current != NULL) {
    // delete bg and fg jbos that have finished
    if (current->state == DONE || current->state == FGDONE) {
      if (prev == NULL) {
	bgjobs = current->next;
      } else {
	prev->next = current->next;
      }
      temp = current->next;
      // if it was a bg job, report it finished
      if (current->state == DONE) 
	printf("[%d]   %-24s%s\n", current->jobid, "Done", current->cmdline);
      fflush(stdout);
      free(current->cmdline);
      free(current);
      current = temp;
    } else {
      prev = current;
      current = current->next;
    }
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

/*
 * addbgjob
 *
 * arguments: process id, command struct, boolean
 *
 * returns: pointer to a bgjobL
 *
 * adds the job to the bgjobs list, records necessary info
 */
bgjobL *
addbgjob(pid_t pid, commandT* cmd, bool isbg)
{
  bgjobL *lastbgjob = bgjobs;
  bgjobL *newjob = (bgjobL *)malloc(sizeof(bgjobL));
  newjob->next = NULL;
  int maxjobid = 0;
  // add the job to the beginning of the list
  // (bg jobs is in descending order of age)
  if (lastbgjob == NULL) {
    bgjobs = newjob;
  } else {
    maxjobid = lastbgjob->jobid;
    while (lastbgjob->next != NULL) {
      if (lastbgjob->jobid > maxjobid)
	maxjobid = lastbgjob->jobid;
      lastbgjob = lastbgjob->next;
    }
    lastbgjob->next = newjob;
  }
  // record everying in newjob and return it
  newjob->pid = pid;
  newjob->state = RUNNING;
  newjob->jobid = maxjobid + 1;
  newjob->cmdline = (char *)malloc(sizeof(char) * (1 + strlen(cmd->cmdline)));
  strcpy(newjob->cmdline, cmd->cmdline);
  int cmdlinelen = strlen(newjob->cmdline);
  // drop the " &" if it's a bg jobx
  if (isbg)
    newjob->cmdline[cmdlinelen - 2] = '\0';
  return newjob;
}

/*
 * updatebgjob
 *
 * arguments: process id, job state
 *
 * returns: void
 *
 * update the state of a job in the bgjobs list
 */
void
updatebgjob(pid_t pid, state_t newstate)
{
  bgjobL *current;
  current = bgjobs;
  while (current != NULL) {
    if (current->pid == pid) {
      // fg jobs are special
      if (current->state == FG && newstate == DONE) {
	current->state = FGDONE;
      } else {
	current->state = newstate;
      }
    }
    current = current->next;
  }
}

/*
 * showjobs
 *
 * arguments: none
 *
 * returns: void
 *
 * print out running jobs and their state
 */
static void
showjobs()
{
  bgjobL *job = bgjobs;
  while (job != NULL) {
    if (job->state != FGDONE) {
      state_t state = job->state;
      const char* msg =
	(state == DONE ? "Done" :
	 (state == RUNNING ? "Running" :
	  (state == STOPPED ? "Stopped" : "slfaksjd!")));
      printf("[%d]   %-24s%s%s\n", 
	     job->jobid, 
	     msg, 
	     job->cmdline,
	     (job->state == RUNNING ? " &" : ""));
    }
    job = job->next;
  }
  fflush(stdout);
}

/*
 * foregroundjob
 *
 * arguments: the job id
 *
 * returns: void
 *
 * move a stopped bg job to the fg
 */
static void
foregroundjob(int jobid)
{
  bgjobL *job = bgjobs;
  while (job != NULL) {
    if (job->jobid == jobid) {
      // update state, send continue signal, and wait
      fgpid = job->pid;
      job->state = FG;
      kill(-fgpid, SIGCONT);
      waitforfg(job->pid);
    }
    job = job->next;
  }
}

/*
 * dobg
 *
 * arguments: the job id
 *
 * returns: void
 *
 * start a stopped bg job
 */
static void
dobg(int jobid)
{
  bgjobL *job = bgjobs;
  while (job != NULL) {
    if (job->state == STOPPED && job->jobid == jobid) {
      kill(job->pid, SIGCONT);
      job->state = RUNNING;
    }
    job = job->next;
  }
}

/* wait for the fg job to finish */
static void
waitforfg(pid_t id)
{
  while(fgpid ==id) sleep(1);
}

/*
 * StopFgProc
 *
 * arguments: none
 *
 * returns: void
 *
 * stend sigtstp to the fg process group, and record the new state
 */
void
StopFgProc()
{
  if (fgpid == -1)
    return;
  bgjobL *job = bgjobs;
  while (job != NULL) {
    if (job->pid == fgpid)
      break;
    job = job->next;
  }
  job->state = STOPPED;
  kill(-fgpid, SIGTSTP);
  fgpid = -1; 
  printf("[%d]   %-24s%s\n", job->jobid, "Stopped", job->cmdline);
}
