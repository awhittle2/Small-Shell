#include <stdio.h> // For printf, getline, perror, and fflush
#include <stdlib.h> // For malloc and free
#include <string.h> // For strtok_r
#include <unistd.h> // For fork, execvp, and chdir
#include <fcntl.h> // For open
#include <ctype.h> // For isspace
#include <signal.h> // For signal handling
#include <sys/wait.h> // For parent pid waiting
#include <errno.h> // For process checking

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
    pid_t pid;
    int status;
    int exitStatus;
    int exited;
};

// Store the latest process in a struct
struct process2 {
    pid_t pid;
    int exitStatus;
    int exited;
    struct process2 *next;
};

struct background {
    struct process *proc;
    struct background *prev;
    struct background *next;
};

struct process2 *terminated = NULL;
int foregroundOnly = 0;
int notForegroundOnly = 0;
int activated = 0;
int inProcess = 0;
void handleSIGINT(int sigum) {
    if(inProcess == 1) {
        char *message = "terminated by signal 2\n";
        write(STDOUT_FILENO, message, 24);
        fflush(stdout);
    }
}

void stopHandleSig(int sig) {
    if(activated == 0) {
        foregroundOnly = 1;
        activated = 1;
    } else {
        notForegroundOnly = 1;
        activated = 0;
    }
}

void childHandleSig(int sig) {
    pid_t pid;
    int status;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        struct process2 *new = malloc(sizeof(struct process));
        new->pid = pid;
 
        if(WIFEXITED(status)) {
            new->exitStatus = WEXITSTATUS(status);
            new->exited = 1;
        } else {
            new->exitStatus = WTERMSIG(status);
            new->exited = 0;
        }

        new->next = terminated;
        terminated = new;
    }
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

void killChild(pid_t pid) {
    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
        kill(pid, SIGKILL);
    }
}


// Function to execute a command
struct process *executeCommand(struct command *curr) {
    if(curr->ampersand == 1) {
        signal(SIGINT, SIG_IGN);
    } else {
        signal(SIGINT, handleSIGINT);
    }
    
    // Fork a new process
    int childStatus;
    pid_t spawnPid = fork();

    struct process *proc = malloc(sizeof(struct process));

    // If the fork failed, then print an error
    if (spawnPid == -1) {
        inProcess = 0;
        perror("Failed to fork");
        fflush(stdin);
        proc->exitStatus = 1;
        proc->exited = 1;
        exit(1);
    } else if (spawnPid == 0) {
        // Child
        // Construct the argument array
        inProcess = 1;
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
                proc->exitStatus = 1;
                proc->exited = 1;
                exit(1);
            }
        }

        // Redirect the output
        if (curr->output != NULL) {
            int outputFd = redirectOutput(curr->output);

            // Check if redirection failed
            if(outputFd == -1) {
                close(outputFd);
                proc->exitStatus = 1;
                proc->exited = 1;
                exit(1);
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
                    proc->exitStatus = 1;
                    proc->exited = 1;
                    exit(1);
                }
            }

            if(curr->output == NULL) {
                // Check if the output is not specified
                int outputFd = redirectOutput("/dev/null");

                // Check if redirection failed
                if(outputFd == -1) {
                    close(outputFd);
                    proc->exitStatus = 1;
                    proc->exited = 1;
                    exit(1);
                }
            }
        }

        execvp(curr->name, argv);

        // If execvp returns, there was an error
        perror("execvp failed");
        fflush(stdout);
        proc->exitStatus = 1;
        proc->exited = 1;
        inProcess = 0;
        exit(1);
    } else {
        // Parent
        inProcess = 0;
        proc->pid = spawnPid;
        proc->status = childStatus;

        if(curr->ampersand == 0 || activated == 1) {
            waitpid(spawnPid, &childStatus, 0);          

            if(WIFEXITED(childStatus)) {
                proc->exitStatus = WEXITSTATUS(childStatus);
                if(proc->exitStatus != 0) {
                    killChild(spawnPid);
                }
                proc->exited = 1;
            } else {
                proc->exitStatus = WTERMSIG(childStatus);
                proc->exited = 0;
            }
            return proc;
        } else {
            printf("background pid is %d\n", spawnPid);
            fflush(stdout);
            waitpid(spawnPid, &childStatus, WNOHANG);         

            return proc;
        }
    }
}

void buildList(struct background **list, struct process *proc) {
    if(*list == NULL) {
        (*list) = malloc(sizeof(struct background));
        (*list)->proc = malloc(sizeof(struct process));
        (*list)->proc = proc;
        (*list)->prev = NULL;
        (*list)->next = NULL;
    } else {
        struct background *curr = *list;
        while(curr->next != NULL) {
            curr = curr->next;
        }

        struct background *new = malloc(sizeof(struct background));
        new->proc = proc;
        new->prev = curr;
        new->next = NULL;
        curr->next = new;
    }
}

void freeCommand(struct command *curr) {
    if(curr != NULL) {
        free(curr->name);
        if(curr->args != NULL) {
            for(int i = 0; i < curr->numArgs; i++) {
                free(curr->args[i]);
            }
            free(curr->args);
        }
        if(curr->input != NULL) {
            free(curr->input);
        }
        if(curr->output != NULL) {
            free(curr->output);
        }
        free(curr);
    }
}

void freeProcess(struct process *curr) {
    if(curr != NULL) {
        free(curr);
    }
}

void freeProcess2(struct process2 *curr) {
    if(curr != NULL) {
        struct process2 *temp;
        while(curr != NULL) {
            temp = curr;
            curr = curr->next;
            free(temp);
        }
    }
}

void freeBackgroundList(struct background *list) {
    if(list != NULL) {
        struct background *temp;
        while(list != NULL) {
            temp = list;
            list = list->next;
            freeProcess(temp->proc);
            free(temp);
        }
    }
}

void killChildren(struct background *list) {
    struct background *curr = list;

    while(curr != NULL) {
        kill(curr->proc->pid, SIGTERM);
        curr = curr->next;
    }
}

// Function to execute exit
void executeExit(struct background *list) {
    killChildren(list);
    freeBackgroundList(list);
    exit(EXIT_SUCCESS);
}

// This function remove process that have exited or been terminated
void removeProcesses(struct background **list) {
    struct background *curr = *list;
    struct process2 *curr2 = NULL;
    struct process2 *currT = NULL;

    currT = terminated;
    terminated = NULL;
    //freeProcess2(terminated);

    while(curr != NULL) {
        curr2 = currT;
        while(curr2 != NULL) {
            if(curr->proc->pid == curr2->pid) {
                if(curr->prev == NULL) {
                    if(curr->next == NULL) {
                        (*list) = NULL;
                    } else {
                        curr->next->prev = NULL;
                        (*list) = curr->next;
                    }
                } else {
                    if(curr->next == NULL) {
                        curr->prev->next = NULL;
                    } else {
                        curr->prev->next = curr->next;
                        curr->next->prev = curr->prev;
                    }
                }

                if(curr2->exited == 1) {
                    printf("background pid %d is done: exit value %d\n", curr2->pid, curr2->exitStatus);
                    fflush(stdout);
                } else {
                    printf("background pid %d is done: terminated by signal %d\n", curr2->pid, curr2->exitStatus);
                    fflush(stdout);
                }
            }

            curr2 = curr2->next;
        }
        curr = curr->next;
    }

    // freeProcess2(currT);
    // freeProcess2(curr2);
    // freeBackgroundList(curr);
}

int main(int argc, char *argv[]) {
    char *userInput = NULL; // Stores the user input
    struct process *currProc = malloc(sizeof(struct process));
    struct process *bgProc = malloc(sizeof(struct process));
    struct background *list = NULL;

    // Set up signal handlers
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, stopHandleSig);
    signal(SIGCHLD, childHandleSig);

    // Set up for getline
    size_t len = 0;
    ssize_t read;

    // Loop until the user enters "exit"
    do {

        removeProcesses(&list);

        if(foregroundOnly == 1) {
            printf("Entering foreground-only mode (& is now ignored)\n");
            fflush(stdout);
            foregroundOnly = 0;
        } else if(notForegroundOnly == 1) {
            printf("Exiting foreground-only mode\n");
            fflush(stdout);
            notForegroundOnly = 0;
        }

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
                        freeCommand(expand);
                        free(userInput);
                        freeProcess(currProc);
                        freeProcess(bgProc);
                        // Need to impliment function to kill all children
                        //freeBackgroundList(list);
                        executeExit(list);
                    // If the user types status, execute the built in for it
                    }
                    else if (strcmp(expand->name, "status") == 0) {
                       if(currProc == NULL) {
                        printf("exit value 0\n");
                       } else {
                            if(currProc->exited == 1) {
                                printf("exit value %d\n", currProc->exitStatus);
                            } else {
                                printf("terminated by signal %d\n", currProc->exitStatus);
                            }
                       }
                    // Otherwise, execute the command
                    }
                    else {
                        if(expand->ampersand == 0) {
                            currProc = executeCommand(expand);
                        } else {
                            bgProc = executeCommand(expand);
                            buildList(&list, bgProc);
                        }
                    }

                    freeCommand(expand);
                    //free(userInput);
                }
            }
        }

    } while (strcmp(userInput, "exit ") != 0);

    free(userInput);
    freeProcess(currProc);
    freeProcess(bgProc);
    freeBackgroundList(list);

    return 0;
}