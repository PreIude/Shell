#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <glob.h>
#include <sys/wait.h>
#include "squish_run.h"
#include "squish_tokenize.h"

/*max line size*/
#define MAX_TOKEN 1028

/**
 * Print a prompt if the input is coming from a TTY
 */
static void prompt(FILE *pfp, FILE *ifp)
{
	if (isatty(fileno(ifp))) {
		fputs(PROMPT_STRING, pfp);
	}
}

/**
 * Actually do the work
 */
int execFullCommandLine(
		FILE *ofp,
		char ** const tokens,
		int nTokens,
		int verbosity)
{
	int i,j;
	
	int index=0;
	int numberOfCommands=1;
	int workingIndex=0;
	int currentIndex=0;
	char currentDirectory[MAX_TOKEN];
	int status = 0;
	int stat_loc = 0;

	char*output = NULL;
	char*input = NULL;

	int out=0;
	int in=0;

	int thePipe[2];

	pid_t child;

	//check number of commands that will be ran
	for (i=0;i<nTokens;i++){
		if (strcmp("|",tokens[i])==0){
			numberOfCommands++;
		}
	}

	//malloc list of exec commands
	char *** commandSet= malloc(numberOfCommands*sizeof(char**));
	//malloc execvp command array
	for (i=0;i<numberOfCommands;i++){
		commandSet[i]=malloc(1*sizeof(char*));
	}
	//iterate through tokens to populate command list
	for (i=0;i<nTokens;i++){
		//if a pipe is found then break into a new set and increment
		if ((strcmp("|",tokens[i])==0)){
			commandSet[workingIndex]=realloc(commandSet[workingIndex],(index+2)*sizeof(char*));
			commandSet[workingIndex][index]= malloc(128*sizeof(char));
			commandSet[workingIndex][index]=NULL;
			workingIndex++;
			index=0;
		}
		else if ((strcmp(">",tokens[i])==0)){ //output redirect found
			if (tokens[i+1]!=NULL && (strcmp(">",tokens[i+1])!=0)){
				out=1;
				output=malloc(strlen(tokens[i+1])+1);
				strcpy(output,tokens[i+1]);
			}
			else{
				printf("error: invalid syntax\n");
				return 0;
			}
			i++;
			continue;
		}else if ((strcmp("<",tokens[i])==0)){ //input redirect found
			if (tokens[i+1]!=NULL && (strcmp("<",tokens[i+1])!=0)){
				in=1;
				input=malloc(strlen(tokens[i+1])+1);
				strcpy(input,tokens[i+1]);
			}else{
				printf("error: invalid syntax\n");
				return 0;
			}
			i++;
			continue;
		}
		else{
			glob_t g;
			memset(&g, 0, sizeof(glob_t));
			status=glob(tokens[i], 0, NULL, &g);
			//globbing passed
			if (status==0){
				//add globenized into working set
				for (j=0;j<g.gl_pathc;j++){
					commandSet[workingIndex]=realloc(commandSet[workingIndex],(index+2)*sizeof(char*));
					commandSet[workingIndex][index+1]=NULL;
					commandSet[workingIndex][index]= malloc(128*sizeof(char));
					strcpy(commandSet[workingIndex][index++],g.gl_pathv[j]);
				}
				globfree (&g);
			//add token to working set
			}else{
				commandSet[workingIndex]=realloc(commandSet[workingIndex],(index+2)*sizeof(char*));
				commandSet[workingIndex][index+1]=NULL;
				commandSet[workingIndex][index]= malloc(128*sizeof(char));
				strcpy(commandSet[workingIndex][index++],tokens[i]);
			}
		}		
	}

	if (verbosity > 0) {
		fprintf(stderr, " + ");
		fprintfTokens(stderr, tokens, 1);
	}
	
	int reading[numberOfCommands];
    int writing[numberOfCommands];

    for(i=0; i < numberOfCommands; i++){
        reading[i] = -1;
        writing[i] = -1;
    }

    for(i=0; i < numberOfCommands-1; i++){
        int thePipe[2];
        pipe(thePipe);
        reading[i+1] = thePipe[0];
        writing[i] = thePipe[1];
    }

	//iterate through list of commands
    for(i = 0; i < numberOfCommands;i++){ 
		//print command list of current exec process
		for (j=0;commandSet[i][j]!=NULL;j++){
			printf("\"%s\" ",commandSet[i][j]);
		}
		printf("\n");

		//builtin functionality for exit and cd
		if (strcmp(commandSet[i][0], "exit")==0){
			printf("exited -- success(%d)\n",WEXITSTATUS(0));
			for (i=0;commandSet[currentIndex][i]!=NULL;i++){
				free(commandSet[currentIndex][i]);
			}
			free(commandSet[i]);
			free(commandSet);
			free(output);
			free(input);
			exit(0);
		}
		else if (strcmp(commandSet[i][0], "cd") == 0) {
			if (commandSet[currentIndex][1]!=NULL){
				char *gdir = getcwd(currentDirectory, MAX_TOKEN);
				char *dir = strcat(gdir, "/");
				char *to = strcat(dir, commandSet[currentIndex][1]);
				chdir(to);
				continue;
			}
		}	
		
        child=fork();
		//check if fork was created  
        if (child < 0){
			exit(errno);
        }
        else{
			//child process
			if (child == 0){               
                if(writing[i] != -1){
                    close(1);
                    dup2(writing[i],1);
                }
                if(reading[i] != -1){
                    close(0);
                    dup2(reading[i],0);
                }
				//check if there is input redirection
				if(in){
      				in=0;
					if (freopen(input, "r", stdin)==NULL){
						exit(errno);
					}
				}
				//check if there is output redirection
    			if(out && i==numberOfCommands-1){
					out=0;
					if (freopen(output, "w+", stdout)==NULL){
						exit(errno);
					}
				}
				//run current list of commands
                if(execvp(commandSet[i][0],commandSet[i]) == -1){
                    perror("Error");
                    exit(errno);
                }
            }
			//parent process
            else{
				if(i > 0) {
                    close(reading[i]);
                }
                close(writing[i]);
				//wait for child
                wait(&stat_loc);
				//free structures
				for (j=0;commandSet[i][j]!=NULL;j++){
					free(commandSet[i][j]);
				}
				free(commandSet[i]);
				if (WEXITSTATUS(stat_loc)==0){
					printf("Child(%d) exited -- success (%d)\n",child,WEXITSTATUS(stat_loc));
				}
				else{
					printf("Child(%d) exited -- failure (%d)\n",child,WEXITSTATUS(stat_loc));
				}
            }
        }
    }
	free(commandSet);
	return 1;
}

/**
 * Load each line and perform the work for it
 */
int
runScript(
		FILE *ofp, FILE *pfp, FILE *ifp,
		const char *filename, int verbosity
	)
{
	char linebuf[LINEBUFFERSIZE];
	char *tokens[MAXTOKENS];
	int lineNo = 1;
	int nTokens, executeStatus = 0;

	fprintf(stderr, "SHELL PID %ld\n", (long) getpid());

	prompt(pfp, ifp);
	while ((nTokens = parseLine(ifp,
				tokens, MAXTOKENS,
				linebuf, LINEBUFFERSIZE, verbosity - 3)) > 0) {
		lineNo++;

		if (nTokens > 0) {

			executeStatus = execFullCommandLine(ofp, tokens, nTokens, verbosity);

			if (executeStatus < 0) {
				fprintf(stderr, "Failure executing '%s' line %d:\n    ",
						filename, lineNo);
				fprintfTokens(stderr, tokens, 1);
				return executeStatus;
			}
		}
		prompt(pfp, ifp);
	}

	return (0);
}


/**
 * Open a file and run it as a script
 */
int
runScriptFile(FILE *ofp, FILE *pfp, const char *filename, int verbosity)
{
	FILE *ifp;
	int status;

	ifp = fopen(filename, "r");
	if (ifp == NULL) {
		fprintf(stderr, "Cannot open input script '%s' : %s\n",
				filename, strerror(errno));
		return -1;
	}

	status = runScript(ofp, pfp, ifp, filename, verbosity);
	fclose(ifp);
	return status;
}