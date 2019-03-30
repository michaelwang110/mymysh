// COMP1521 18s2 mymysh ... command history
// Implements an abstract data object

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"

// This is defined in string.h
// BUT ONLY if you use -std=gnu99
//extern char *strdup(const char *s);

// Command History
// array of command lines
// each is associated with a sequence number

#define MAXHIST 20
#define MAXSTR  200

#define HISTFILE ".mymysh_history"

typedef struct _history_entry {
   int   seqNumber;
   char *commandLine;
} HistoryEntry;

typedef struct _history_list {
   int nEntries;
   HistoryEntry commands[MAXHIST];
} HistoryList;

// Helper Function prototypes
static void histFilePath(char *);
static void mallocMemoryCheck(char *);


HistoryList CommandHistory;

// initCommandHistory()
// - initialise the data structure
// - read from .history if it exists

int initCommandHistory()
{
   FILE *fp;               // stores file pointer from fopen
   char fileName[MAXSTR];  // path of HISTFILE
   char line[MAXSTR];      // each line in HISTFILE
   char cmdLine[MAXSTR];   // stores the command lines from HISTFILE
   int seqNo;              // stores the sequence numbers from HISTFILE
   int i;                  // generic index
   
   // set up HISTFILE path
   histFilePath(fileName);
   
   // open file for reading
   if ((fp = fopen(fileName, "r")) == NULL)
      return 1;
   
   // initialise nEntries, i and seqNo to 0
   CommandHistory.nEntries = i = seqNo = 0;
   
   // loop through each line in HISTFILE
   while (fgets(line, MAXSTR, fp) != NULL) {
      // parse each line to extract seqNo and command
      sscanf(line, " %3d  %[^\n]s", &seqNo, cmdLine);
      CommandHistory.commands[i].seqNumber = seqNo;
      CommandHistory.commands[i].commandLine = strdup(cmdLine);
      mallocMemoryCheck(CommandHistory.commands[i].commandLine);
      i++;
      CommandHistory.nEntries = i;
   }
   fclose(fp);
   return (seqNo + 1);
}

// addToCommandHistory()
// - add a command line to the history list
// - overwrite oldest entry if buffer is full

void addToCommandHistory(char *cmdLine, int seqNo)
{
   int numEntries = CommandHistory.nEntries;
   // handle full buffer
   if (numEntries == MAXHIST) {
      // loop through buffer and remove oldest entry
      for (int i = 0; i < MAXHIST-1; i++) {
         CommandHistory.commands[i].seqNumber++;
         free(CommandHistory.commands[i].commandLine);
         CommandHistory.commands[i].commandLine = strdup(CommandHistory.commands[i+1].commandLine);
         mallocMemoryCheck(CommandHistory.commands[i].commandLine);
      }
      free(CommandHistory.commands[MAXHIST-1].commandLine);
      numEntries--;
      CommandHistory.nEntries--;
   }
   // add command line to history list
   CommandHistory.commands[numEntries].seqNumber = seqNo;
   CommandHistory.commands[numEntries].commandLine = strdup(cmdLine);
   mallocMemoryCheck(CommandHistory.commands[numEntries].commandLine);
   CommandHistory.nEntries++;
}

// showCommandHistory()
// - display the list of command lines

void showCommandHistory(FILE *outf)
{
   for (int i = 0; i < CommandHistory.nEntries; i++) {
      fprintf(
         outf,
         " %3d  %s\n",
         CommandHistory.commands[i].seqNumber,
         CommandHistory.commands[i].commandLine
      );
   }
}

// getCommandFromHistory()
// - get the command line for specified command
// - returns NULL if no command with this number

char *getCommandFromHistory(int cmdNo)
{
   for (int i = 0; i < CommandHistory.nEntries; i++)
      if (CommandHistory.commands[i].seqNumber == cmdNo)
         return CommandHistory.commands[i].commandLine;
   return NULL;
}

// saveCommandHistory()
// - write history to $HOME/.mymysh_history

void saveCommandHistory()
{
   char fileName[MAXSTR];
   histFilePath(fileName);
   FILE *fp = fopen(fileName, "w");
   showCommandHistory(fp);
   fclose(fp);
}

// cleanCommandHistory
// - release all data allocated to command history

void cleanCommandHistory()
{
   for (int i = 0; i < CommandHistory.nEntries; i++)
      free(CommandHistory.commands[i].commandLine);
}

// Helper Functions

// histFilePath()
// - assign HISTFILE path to fileName

static void histFilePath(char *fileName)
{
   strcpy(fileName, getenv("HOME"));
   strcat(fileName, "/");
   strcat(fileName, HISTFILE);
}

// mallocMemoryCheck()
// - print error message if malloc failed to allocate memory i.e. NULL

static void mallocMemoryCheck(char *str)
{
   if (str == NULL) {
      fprintf(stderr, "Failed to allocate memory using malloc.\n");
      exit(0);
   }
}
