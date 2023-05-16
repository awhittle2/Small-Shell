#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>

// Store the command in a struct
struct command {
    char *name;
    char **args;
    int numArgs;
    char *input;
    char *output;
    int ampersand; // 0 if no ampersand, 1 if ampersand
};

// Store the last running process in a struct
struct process {
    int pid;
    struct command *currCommand;
    char *exitStatus;
    char *termSignal;
};

// Linked list of running processes
struct runningProcess {
    struct process *currProcess;
    struct runningProcess *next;
};

void handleSIGINT(int sigum) {
    // Do nothing
}

// Processes a line of user input and returns a command struct with the command name, arguments, input, output, and ampersand flag
struct command *processLine(char *currLine) {
    if (currLine == NULL) {
        return NULL;
    }

    struct command *currCommand = malloc(sizeof(struct command));
    char *saveptr;

    // Initialize the command
    currCommand->name = NULL;
    currCommand->args = NULL;
    currCommand->numArgs = 0;
    currCommand->input = NULL;
    currCommand->output = NULL;
    currCommand->ampersand = 0;

    // Get the command name
    char *token = strtok_r(currLine, " ", &saveptr);
    currCommand->name = calloc(strlen(token) + 1, sizeof(char));
    strcpy(currCommand->name, token);

    // Get the arguments
    int indexArg = 0;
    token = strtok_r(NULL, " ", &saveptr);

    // While there are still arguments to be read and the argument is not an input, output, or ampersand
    while (token != NULL && strcmp(token, "<") != 0 && strcmp(token, ">") != 0 && strcmp(token, "&") != 0) {
        // Store the argument in a new string
        char *arg = calloc(strlen(token) + 1, sizeof(char));
        strcpy(arg, token);

        // Add the argument to the array of arguments
        currCommand->args = realloc(currCommand->args, (indexArg + 1) * sizeof(char *));
        currCommand->args[indexArg] = arg;
        indexArg++;
        currCommand->numArgs++;

        // Get the next argument
        token = strtok_r(NULL, " ", &saveptr);
    }

    // If the next argument is an input, output, or ampersand, then process it
    if (token != NULL && strcmp(token, "<") == 0) {
        // Get the part after the space and store it as the input
        token = strtok_r(NULL, " ", &saveptr);
        currCommand->input = calloc(strlen(token) + 1, sizeof(char));
        strcpy(currCommand->input, token);
    } else if (token != NULL && strcmp(token, ">") == 0) {
        // Get the part after the space and store it as the output
        token = strtok_r(NULL, " ", &saveptr);
        currCommand->output = calloc(strlen(token) + 1, sizeof(char));
        strcpy(currCommand->output, token);
    } else if (token != NULL && strcmp(token, "&") == 0) {
        currCommand->ampersand = 1; // Set the ampersand flag to true
    }

    token = strtok_r(NULL, " ", &saveptr);

    // If the next argument is an output, input, or ampersand, then process it
    if (token != NULL && strcmp(token, ">") == 0) {
        // Get the part after the space and store it as the output
        token = strtok_r(NULL, " ", &saveptr);
        currCommand->output = calloc(strlen(token) + 1, sizeof(char));
        strcpy(currCommand->output, token);
    } else if (token != NULL && strcmp(token, "<") == 0) {
        // Get the part after the space and store it as the input
        token = strtok_r(NULL, " ", &saveptr);
        currCommand->input = calloc(strlen(token) + 1, sizeof(char));
        strcpy(currCommand->input, token);
    } else if (token != NULL && strcmp(token, "&") == 0) {
        currCommand->ampersand = 1; // Set the ampersand flag to true
    }

    token = strtok_r(NULL, " ", &saveptr);

    // If the next argument is an ampersand, then process it
    if (token != NULL && strcmp(token, "&") == 0) {
        currCommand->ampersand = 1; // Set the ampersand flag to true
    }

    return currCommand; // Return the new command
}

// Counts the number of expansions in the input
int countExpansions(char *input) {
    int expansions = 0;
    int length = strlen(input);

    for (int i = 0; i < length; i++) {
        if (input[i] == '$' && input[i + 1] == '$') {
            expansions++;
        }
    }

    return expansions;
}

// Performs the expansion of the variable
char *variableExpansion(char *input, int expansions, int pid) {
    // Calculate the new length of the command name
    int length = strlen(input);
    int pidSize = snprintf(NULL, 0, "%d", pid);

    int newLength = length + (pidSize - 2) * expansions;
    char *newOutput = calloc(newLength + 1, sizeof(char));

    int j = 0;
    for (int i = 0; i < length; i++) {
        if (input[i] == '$' && input[i + 1] == '$') {
            // Expand the variable
            sprintf(newOutput + j, "%d", pid);
            j += pidSize;
            i++; // Skip the second '$'
        } else {
            // Copy the character
            newOutput[j] = input[i];
            j++;
        }
    }

    newOutput[j] = '\0';

    return newOutput;
}

// Frees unneeded memory and sets the input to the output
void freeAndSet(char **input, char *output) {
    free(*input);
    *input = malloc(strlen(output) * sizeof(char));
    strcpy(*input, output);
    free(output);
}

// Expands variables in the command
struct command *expandVariables(struct command *currCommand, int pid) {
    struct command *expandCommand = currCommand;

    // Expand the command name

    // Count the number of variable expansions and then expand
    int expansions = countExpansions(expandCommand->name);
    char *newOutput = variableExpansion(expandCommand->name, expansions, pid);
    freeAndSet(&(expandCommand->name), newOutput);

    // Expand the arguments

    for (int i = 0; i < expandCommand->numArgs; i++) {
        // Count the number of variable expansions and then expand
        expansions = countExpansions(expandCommand->args[i]);
        newOutput = variableExpansion(expandCommand->args[i], expansions, pid);
        freeAndSet(&(expandCommand->args[i]), newOutput);
    }

    // Expand the input

    if (expandCommand->input != NULL) {
        // Count the number of variable expansions and then expand
        expansions = countExpansions(expandCommand->input);
        newOutput = variableExpansion(expandCommand->input, expansions, pid);
        freeAndSet(&(expandCommand->input), newOutput);
    }

    // Expand the output

    if (expandCommand->output != NULL) {
        // Count the number of variable expansions and then expand
        expansions = countExpansions(expandCommand->output);
        newOutput = variableExpansion(expandCommand->output, expansions, pid);
        freeAndSet(&(expandCommand->output), newOutput);
    }

    return expandCommand;
}

// Function to kill all running processes
void killAll(struct runningProcess *head) {
    struct runningProcess *curr = head;
    struct runningProcess *next = NULL;

    while(curr != NULL) {
        int pid = curr->currProcess->pid;
        int result = kill(pid, SIGTERM);

        if(result == -1) {
            perror("Failed to kill process");
            fflush(stdin);
        } else {
            printf("Killed process %d\n", head->currProcess->pid);
            fflush(stdin);
        }

        next = curr->next;
        free(curr->currProcess);
        free(curr);

        curr = next;
    }
}

// Function to execute exit
void executeExit(struct runningProcess *list){
    // TO DO: Free memory
    killAll(list);
    exit(EXIT_SUCCESS);
}

// Function to execute CD
// Note: This function ignores input redirection, output redirection, foreground/background, and additional arguements
int executeCD(struct command *currCommand) {
    int result = 0;
    char currentDir[2048]; // Stores the current directory

    // If the user entered no arguments, then go to the home directory
    if (currCommand->numArgs == 0) {
        result = chdir(getenv("HOME"));
        if (result != 0) {
            perror("Failed to change directory");
            fflush(stdin);
            return 1;
        }
        // If the user entered one argument, then go to that directory
    } else {
        // If the user entered a relative path, then add the current directory
        if (currCommand->args[0][0] != '/') {
            if (getcwd(currentDir, sizeof(currentDir)) != NULL) {
                strcat(currentDir, "/");
                // Implement if there is a slash at the end, remove it
                strcat(currentDir, currCommand->args[0]);
                result = chdir(currentDir);
                if (result != 0) {
                    perror("Failed to change directory");
                    fflush(stdin);
                    return 1;
                }
            } else {
                perror("Failed to get current directory");
                fflush(stdin);
                return 1;
            }
        } else {
            result = chdir(currCommand->args[0]);
            if (result != 0) {
                perror("Failed to change directory");
                fflush(stdin);
                return 1;
            }
        }
    }

    return 0;
}

// Function to execute status
int executeStatus(struct process *lastProcess) {
    // If there is no previous process, then the exit status is 0
    if(lastProcess == NULL) {
        printf("exit value 0\n");
        fflush(stdin);
    } else {
        // If there was an exit status, then print it
        if(lastProcess->exitStatus != NULL) {
            printf("exit value %s\n", lastProcess->exitStatus);
            fflush(stdin);
        // Else, print the termination signal
        } else {
            printf("terminated by signal %s\n", lastProcess->termSignal);
            fflush(stdin);
        }
    }

    return 0;
}

// Function to execute a command
int executeCommand(struct command *curr) {
    // Fork a new process
    int spawnPid = fork();

    // If the fork failed, then print an error
    if (spawnPid == -1) {
        perror("Failed to fork");
        fflush(stdin);
        return 1;
    } else if (spawnPid == 0) {
        // Child
        printf("Child pid: %d\n", getpid());
        fflush(stdin);

        // Execute the command
        execvp(curr->name, curr->args);
    } else {
        // Parent
    }
    return 0;
}

// Function to redirect input
int redirectInput(char *input) {
    int fd = open(input, O_RDONLY);

    if (fd == -1) {
        perror("Failed to open file");
        fflush(stdin);
        return 1;
    }

    if (dup2(fd, 0) == -1) {
        perror("Failed to redirect input");
        fflush(stdin);
        return 1;
    }

    if (close(fd) == -1) {
        perror("Failed to close file");
        fflush(stdin);
        return 1;
    }

    return 0;
}

// Function to redirect output
int redirectOutput(char *output) {
    int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd == -1) {
        perror("Failed to open file");
        fflush(stdin);
        return 1;
    }

    if (dup2(fd, 1) == -1) {
        perror("Failed to redirect output");
        fflush(stdin);
        return 1;
    }

    if (close(fd) == -1) {
        perror("Failed to close file");
        fflush(stdin);
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    char *userInput = NULL; // Stores the user input
    int builtInStatus = 0;  // Stores the exit status of the last built in command

    // Struct to store the last completed foreground process
    struct process *lastProcess = NULL;

    // Linked list to store current running processes
    struct runningProcess *list = NULL;

    signal(SIGINT, handleSIGINT);

    // Set up for getline
    size_t len = 0;
    ssize_t read;

    int started = 0;

    // Loop until the user enters "exit"
    while (started == 0 || strcmp(userInput, "exit ") != 0) {
        started = 1;

        // Prompt the user for input
        printf(": ");
        fflush(stdin);
        read = getline(&userInput, &len, stdin);

        // Check if the user entered only whitespace
        int onlyWhiteSpace = 1;
        for (size_t i = 0; i < strlen(userInput); i++) {
            if (!isspace(userInput[i])) {
                onlyWhiteSpace = 0;
                break;
            }
        }

        // If the user entered only whitespace, then continue
        if (onlyWhiteSpace == 1) {
            continue;
            // If they didn't, then process the input
        } else {
            // Replace the new line character with a space
            if (read > 0 && userInput[strlen(userInput) - 1] == '\n')
                userInput[strlen(userInput) - 1] = ' ';

            // If the user entered a comment, then continue
            if (userInput[0] == '#') {
                continue;
                // If the user typed a command, then make the command struct
            } else {
                // Save command as struct
                struct command *currCommand = processLine(userInput);

                // Uncomment this code to see the break down of the command
                // if(currCommand != NULL) {
                //     printf("Command: %s\n", currCommand->name);

                //     if(currCommand->args != NULL) {
                //         printf("Arguments: \n");
                //         for(int i = 0; i < currCommand->numArgs; i++) {
                //             printf(" Arg %d: %s\n", i+1, currCommand->args[i]);
                //         }
                //     }

                //     if(currCommand->input != NULL) {
                //         printf("Input: %s\n", currCommand->input);
                //     }

                //     if(currCommand->output != NULL) {
                //         printf("Output: %s\n", currCommand->output);
                //     }

                //     printf("Ampersand: %d\n", currCommand->ampersand);
                // }

                if (currCommand != NULL) {
                    // Expand variables
                    int pid = getpid();
                    struct command *expandCommand = expandVariables(currCommand, pid);

                    // Uncomment this code to see the break down of the command
                    // if(expandCommand != NULL) {
                    //     printf("Command: %s\n", expandCommand->name);

                    //     if(expandCommand->args != NULL) {
                    //         printf("Arguments: \n");
                    //         for(int i = 0; i < expandCommand->numArgs; i++) {
                    //             printf(" Arg %d: %s\n", i+1, expandCommand->args[i]);
                    //         }
                    //     }

                    //     if(expandCommand->input != NULL) {
                    //         printf("Input: %s\n", expandCommand->input);
                    //     }

                    //     if(expandCommand->output != NULL) {
                    //         printf("Output: %s\n", expandCommand->output);
                    //     }

                    //     printf("Ampersand: %d\n", expandCommand->ampersand);
                    // }

                    // If the user types CD, execute the built in for it
                    if (strcmp(expandCommand->name, "cd") == 0) {
                        builtInStatus = executeCD(expandCommand);
                        // If the user types exit, execute the built in for it
                    } else if (strcmp(expandCommand->name, "exit") == 0) {
                        executeExit(list);
                        // If the user types status, execute the built in for it
                    } else if (strcmp(expandCommand->name, "status") == 0) {
                        builtInStatus = executeStatus(lastProcess);
                        // Otherwise, execute the command
                    } else {
                        executeCommand(expandCommand);
                    }

                    // if(expandCommand != NULL) {
                    //     // free(expandCommand->name);
                    //     // // Free args
                    //     // if(expandCommand->input != NULL) {
                    //     //     free(expandCommand->input);
                    //     // }
                    //     // if(expandCommand->output != NULL) {
                    //     //     free(expandCommand->output);
                    //     // }
                    //     free(expandCommand);
                    // }
                }

                if(currCommand != NULL) {
                    free(currCommand->name);
                    // Free args
                    if(currCommand->input != NULL) {
                        free(currCommand->input);
                    }
                    if(currCommand->output != NULL) {
                        free(currCommand->output);
                    }
                    free(currCommand);
                }
            }
        }
    }

    return 0;
}