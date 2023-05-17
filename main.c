#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>

// Store the command in a struct
struct command {
    char *name;
    char **args;
    int numArgs;
    char *input;
    char *output;
    int ampersand; // 0 if no ampersand, 1 if ampersand
};

// Store the latest process in a struct
struct process {
    int status;
    int exited;
};

void handleSIGINT(int sigum) {
    exit(EXIT_SUCCESS);
}

// Processes a line of user input and returns a command struct with the command name, arguments, input, output, and ampersand flag
struct command *processLine(char *currLine) {
    if (currLine == NULL) {
        return NULL;
    }

    struct command *curr = malloc(sizeof(struct command));
    char *saveptr;

    // Initialize the command
    curr->name = NULL;
    curr->args = NULL;
    curr->numArgs = 0;
    curr->input = NULL;
    curr->output = NULL;
    curr->ampersand = 0;

    // Get the command name
    char *token = strtok_r(currLine, " ", &saveptr);
    curr->name = calloc(strlen(token) + 1, sizeof(char));
    strcpy(curr->name, token);

    // Get the arguments
    int indexArg = 0;
    token = strtok_r(NULL, " ", &saveptr);

    // While there are still arguments to be read and the argument is not an input, output, or ampersand
    while (token != NULL && strcmp(token, "<") != 0 && strcmp(token, ">") != 0 && strcmp(token, "&") != 0) {
        // Store the argument in a new string
        char *arg = calloc(strlen(token) + 1, sizeof(char));
        strcpy(arg, token);

        // Add the argument to the array of arguments
        curr->args = realloc(curr->args, (indexArg + 1) * sizeof(char *));
        curr->args[indexArg] = arg;
        indexArg++;
        curr->numArgs++;

        // Get the next argument
        token = strtok_r(NULL, " ", &saveptr);
    }

    // If the next argument is an input, output, or ampersand, then process it
    if (token != NULL && strcmp(token, "<") == 0) {
        // Get the part after the space and store it as the input
        token = strtok_r(NULL, " ", &saveptr);
        curr->input = calloc(strlen(token) + 1, sizeof(char));
        strcpy(curr->input, token);
    } else if (token != NULL && strcmp(token, ">") == 0) {
        // Get the part after the space and store it as the output
        token = strtok_r(NULL, " ", &saveptr);
        curr->output = calloc(strlen(token) + 1, sizeof(char));
        strcpy(curr->output, token);
    } else if (token != NULL && strcmp(token, "&") == 0) {
        curr->ampersand = 1; // Set the ampersand flag to true
        return curr;         // Return the new command
    }

    token = strtok_r(NULL, " ", &saveptr);

    // If the next argument is an output, input, or ampersand, then process it
    if (token != NULL && strcmp(token, ">") == 0) {
        // Get the part after the space and store it as the output
        token = strtok_r(NULL, " ", &saveptr);
        curr->output = calloc(strlen(token) + 1, sizeof(char));
        strcpy(curr->output, token);
    } else if (token != NULL && strcmp(token, "<") == 0) {
        // Get the part after the space and store it as the input
        token = strtok_r(NULL, " ", &saveptr);
        curr->input = calloc(strlen(token) + 1, sizeof(char));
        strcpy(curr->input, token);
    } else if (token != NULL && strcmp(token, "&") == 0) {
        curr->ampersand = 1; // Set the ampersand flag to true
        return curr;         // Return the new command
    }

    token = strtok_r(NULL, " ", &saveptr);

    // If the next argument is an ampersand, then process it
    if (token != NULL && strcmp(token, "&") == 0) {
        curr->ampersand = 1; // Set the ampersand flag to true
    }

    return curr; // Return the new command
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
    char *output = calloc(newLength + 1, sizeof(char));

    int j = 0;
    for (int i = 0; i < length; i++) {
        if (input[i] == '$' && input[i + 1] == '$') {
            // Expand the variable
            sprintf(output + j, "%d", pid);
            j += pidSize;
            i++; // Skip the second '$'
        } else {
            // Copy the character
            output[j] = input[i];
            j++;
        }
    }

    output[j] = '\0';
    return output;
}

// Frees unneeded memory and sets the input to the output
void freeAndSet(char **input, char *output) {
    free(*input);
    *input = malloc(strlen(output) * sizeof(char));
    strcpy(*input, output);
    free(output);
}

// Expands variables in the command
struct command *expandVariables(struct command *curr, int pid) {
    struct command *expand = curr;

    // Expand the command name
    int expansions = countExpansions(expand->name);
    char *output = variableExpansion(expand->name, expansions, pid);
    freeAndSet(&(expand->name), output);

    // Expand the arguments
    for (int i = 0; i < expand->numArgs; i++) {
        expansions = countExpansions(expand->args[i]);
        output = variableExpansion(expand->args[i], expansions, pid);
        freeAndSet(&(expand->args[i]), output);
    }

    // Expand the input
    if (expand->input != NULL) {
        expansions = countExpansions(expand->input);
        output = variableExpansion(expand->input, expansions, pid);
        freeAndSet(&(expand->input), output);
    }

    // Expand the output
    if (expand->output != NULL) {
        expansions = countExpansions(expand->output);
        output = variableExpansion(expand->output, expansions, pid);
        freeAndSet(&(expand->output), output);
    }

    return expand;
}

// Function to execute exit
void executeExit() {
    exit(EXIT_SUCCESS);
}

// Function to execute CD
void executeCD(struct command *curr) {
    int result = 0;
    char currentDir[2048]; // Stores the current directory

    // If the user entered no arguments, then go to the home directory
    if (curr->numArgs == 0) {
        result = chdir(getenv("HOME"));
        if (result != 0) {
            perror("Failed to change directory");
            fflush(stdout);
            return;
        }
    // If the user entered one argument, then go to that directory
    } else {
        // If the user entered a relative path, then add the current directory
        if (curr->args[0][0] != '/') {
            if (getcwd(currentDir, sizeof(currentDir)) != NULL) {
                strcat(currentDir, "/");
                // Implement if there is a slash at the end, remove it
                strcat(currentDir, curr->args[0]);
                result = chdir(currentDir);
                if (result != 0) {
                    perror("Failed to change directory");
                    fflush(stdout);
                    return;
                }
            } else {
                perror("Failed to get current directory");
                fflush(stdout);
                return;
            }
        } else {
            result = chdir(curr->args[0]);
            if (result != 0) {
                perror("Failed to change directory");
                fflush(stdout);
                return;
            }
        }
    }
}

// Function to redirect input
int redirectInput(char *input) {
    // Open the input file
    int fd = open(input, O_RDONLY);

    // Check if the open failed
    if (fd == -1) {
        perror("Failed to open file");
        fflush(stdout);
        return -1;
    }

    // Redirect the input and check if it failed
    if (dup2(fd, 0) == -1) {
        perror("Failed to redirect input");
        fflush(stdout);
        return -1;
    }

    // Return its status
    return fd;
}

// Function to redirect output
int redirectOutput(char *output) {
    // Open the output file
    int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    // Check if the open failed
    if (fd == -1) {
        perror("Failed to open file");
        fflush(stdout);
        return -1;
    }

    // Redirect the output and check if it failed
    if (dup2(fd, 1) == -1) {
        perror("Failed to redirect output");
        fflush(stdout);
        return -1;
    }

    // Return its status
    return fd;
}

// Function to set status
void printStatus(struct process *proc) {
    if(proc == NULL) {
        printf("exit value 0\n");
        return;
    }

    if(proc->exited == 1) {
        printf("exit value %d\n", proc->status);
    } else {
        printf("terminated by signal %d\n", proc->status);
    }
}

// Function to execute a command
struct process *executeCommand(struct command *curr) {
    // Fork a new process
    int childStatus;
    pid_t spawnPid = fork();

    struct process *proc = malloc(sizeof(struct process));

    // If the fork failed, then print an error
    if (spawnPid == -1) {
        perror("Failed to fork");
        fflush(stdin);
        proc->status = 1;
        proc->exited = 1;
        return proc;
    } else if (spawnPid == 0) {
        // Child
        // Construct the argument array
        char *argv[curr->numArgs + 2];
        argv[0] = curr->name;

        for (int i = 0; i < curr->numArgs; i++) {
            argv[i + 1] = curr->args[i];
        }

        argv[curr->numArgs + 1] = NULL;

        // Redirect the input
        if (curr->input != NULL) {
            int inputFd = redirectInput(curr->input);

            // Check if redirection failed
            if(inputFd == -1) {
                close(inputFd);
                proc->status = 1;
                proc->exited = 1;
                return proc;
            }
        }

        // Redirect the output
        if (curr->output != NULL) {
            int outputFd = redirectOutput(curr->output);

            // Check if redirection failed
            if(outputFd == -1) {
                close(outputFd);
                proc->status = 1;
                proc->exited = 1;
                return proc;
            }
        }

        // Redirect the input and output if background process
        if (curr->ampersand == 1) {
            // Check if the input is not specified
            if(curr->input == NULL) {
                int inputFd = redirectInput("/dev/null");

                // Check if redirection failed
                if(inputFd == -1) {
                    close(inputFd);
                    proc->status = 1;
                    proc->exited = 1;
                    return proc;
                }
            }

            if(curr->output == NULL) {
                // Check if the output is not specified
                int outputFd = redirectOutput("/dev/null");

                // Check if redirection failed
                if(outputFd == -1) {
                    close(outputFd);
                    proc->status = 1;
                    proc->exited = 1;
                    return proc;
                }
            }
        }

        execvp(curr->name, argv);

        // If execvp returns, there was an error
        perror("execvp failed");
        fflush(stdout);
        proc->status = 1;
        proc->exited = 1;
        return proc;
    } else {
        // Parent
        if(curr->ampersand == 0) {
            waitpid(spawnPid, &childStatus, 0);

            if(WIFEXITED(childStatus)) {
                proc->status = WEXITSTATUS(childStatus);
                proc->exited = 1;
            } else {
                proc->status = WTERMSIG(childStatus);
                proc->exited = 0;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    char *userInput = NULL; // Stores the user input
    struct process *proc = NULL;

    // Set up signal handlers
    signal(SIGINT, handleSIGINT);

    // Set up for getline
    size_t len = 0;
    ssize_t read;

    // Loop until the user enters "exit"
    do {
        free(userInput); // Free the user input

        // Print the prompt and get the user input
        printf(": ");
        fflush(stdout);
        read = getline(&userInput, &len, stdin);

        // Check if the user entered only whitespace
        int onlyWhiteSpace = 1;
        for (size_t i = 0; i < strlen(userInput); i++) {
            if (!isspace(userInput[i])) {
                onlyWhiteSpace = 0;
                break;
            }
        }

        // If the user entered only whitespace, then jump to next iteration
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
                struct command *curr = processLine(userInput); // Save command in struct
                
                // If the command struct is valid, then execute it
                if (curr != NULL) {
                    // Expand variables
                    int pid = getpid();
                    struct command *expand = expandVariables(curr, pid);

                    // If the user types CD, execute the built in for it
                    if (strcmp(expand->name, "cd") == 0) {
                        executeCD(expand);
                    // If the user types exit, execute the built in for it
                    }
                    else if (strcmp(expand->name, "exit") == 0) {
                        executeExit();
                    // If the user types status, execute the built in for it
                    }
                    else if (strcmp(expand->name, "status") == 0) {
                        printStatus(proc);
                    // Otherwise, execute the command
                    }
                    else {
                        proc = executeCommand(expand);
                    }
                }
            }
        }

    } while (strcmp(userInput, "exit ") != 0);

    return 0;
}
