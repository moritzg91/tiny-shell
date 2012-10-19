/***************************************************************************
 *  Title: tsh
 * -------------------------------------------------------------------------
 *    Purpose: A simple shell implementation 
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.4 $
 *    Last Modification: $Date: 2009/10/12 20:50:12 $
 *    File: $RCSfile: tsh.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
#define __MYSS_IMPL__

/************System include***********************************************/
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

/************Private include**********************************************/
#include "tsh.h"
#include "io.h"
#include "interpreter.h"
#include "runtime.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

#define BUFSIZE 80

/************Global Variables*********************************************/

/************Function Prototypes******************************************/
/* handles SIGINT and SIGSTOP signals */
static void 
sig(int);
/* reaps all finished child processes */
static void 
reap_children();
/* processes the tshrc file */
static void 
process_tshrc();
/************External Declaration*****************************************/

/**************Implementation***********************************************/

/*
 * main
 *
 * arguments:
 *   int argc: the number of arguments provided on the command line
 *   char *argv[]: array of strings provided on the command line
 *
 * returns: int: 0 = OK, else error
 *
 * This sets up signal handling and implements the main loop of tsh.
 */
int
main(int argc, char *argv[])
{
  /* Initialize command buffer */
  char* cmdLine = malloc(sizeof(char*) * BUFSIZE);

  /* shell initialization */
  if (signal(SIGINT, sig) == SIG_ERR)
    PrintPError("SIGINT");
  if (signal(SIGTSTP, sig) == SIG_ERR)
    PrintPError("SIGTSTP");
  if (signal(SIGCHLD, sig) == SIG_ERR)
    PrintPError("SIGCHLD");

  fgpid = -1;

  process_tshrc();

  while (!forceExit) /* repeat forever */
    {
      /* print prompt */
      //printf("tsh>");
      
      /* read command line */
      getCommandLine(&cmdLine, BUFSIZE);

      /* checks the status of background jobs */
      CheckJobs();

      if (strcmp(cmdLine, "exit") == 0) {
	//printf("quitting...\n");
        forceExit = TRUE;
	continue;
      }
 
      /* interpret command and line
       * includes executing of commands */
      Interpret(cmdLine);

    }

  /* shell termination */
  free(cmdLine);
  
  return 0;
} /* main */

/*
 * sig
 *
 * arguments:
 *   int signo: the signal being sent
 *
 * returns: none
 *
 * This should handle signals sent to tsh.
 */
static void
sig(int signo)
{
  if (signo == SIGINT) 
    IntFgProc();
  if (signo == SIGCHLD)
    reap_children();
} /* sig */

/*
 * reap_children
 *
 * arguments: none
 *
 * returns: none
 *
 * Reap all stoped child processes.
 */
static void
reap_children()
{
  int status = 0;
  int pid = 0;
  do 
    {
      pid = waitpid(-1, &status, WNOHANG | WUNTRACED);
      // if fg process has stopped, update fgpid
      if (pid == fgpid)
	fgpid = -1;
    } while (pid > 0);
} /*reap_children */

/*
 * process_tshrc
 *
 * arguments: none
 *
 * returns: none
 *
 * Read and process the tshrc file.
 */
static void 
process_tshrc()
{
  char *homedir;
  char *fpath;
  struct stat st;
  FILE *tshrc_file;
  const char *fname = ".tshrc";

  // set up path to ~/.tshrc
  homedir = getenv("HOME");
  fpath = (char *)malloc(2 + sizeof(char) * (strlen(homedir) + strlen(fname)));
  strcpy(fpath, homedir);
  strcat(fpath, "/");
  strcat(fpath, fname);

  // if ~/.tshrc exists, read it and interpret non-comment lines
  if (stat(fpath, &st) == 0) {
    tshrc_file = fopen(fpath, "r");
    char line[128];
    while (fgets(line, sizeof(line), tshrc_file) != NULL) {
      if (line[0] != '#') {
	Interpret(line);
      }
    }
    fclose(tshrc_file);
  }
  free(fpath);
}
