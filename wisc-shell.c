#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>

#define MAX_LINE 512
#define MAX_ARGS 64

typedef struct AliasNode {
    char *name;
    char *command;
    struct AliasNode *next;
} Alias;

Alias *alias_list = NULL;

// Forward Declarations
void execute_command(char *cmd);
char** parse_command(char *cmd, int *arg_count);
void printAliases(struct AliasNode* head);
char* findAlias(struct AliasNode* head, char *name);
void addOrUpdateAlias(struct AliasNode** head, char *name, char *command);
void set_environment_variable(char *var, char *value);
void unset_environment_variable(char *var);
void substitute_environment_variables(char **argv, int arg_count);

int main(int argc, char *argv[]) {
    char line[MAX_LINE];
    FILE *input = stdin;
    int interactive = 1;

    // Check Batch Status
    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (!input) {
            fprintf(stderr, "Error: could not open batch file\n");
            exit(1);
        }
        interactive = 0;
    }

    // Interactive loop
    while (1) {
        if (interactive) {
            printf("wish> ");
        }

        if (!fgets(line, MAX_LINE, input)) {
            break; // EOF or error
        }

        line[strcspn(line, "\n")] = 0;

        if (!interactive) {
            printf("%s\n", line); // Echo the command in batch mode
        }

        // Trim leading whitespace
        char *trimmed_line = line;
        while (isspace(*trimmed_line)) {
            trimmed_line++;
        }

        if (*trimmed_line == '\0') {
            continue;
        }

        if (strcmp(trimmed_line, "exit") == 0) {
            break;
        }

        execute_command(trimmed_line);
    }

    if (!interactive) {
        fclose(input);
    }

    return 0;
}

char** parse_command(char *cmd, int *arg_count) {
    char **argv = malloc(MAX_ARGS * sizeof(char*));
    char *token = strtok(cmd, " ");
    int count = 0;

    while (token != NULL) {
        argv[count] = token;
        count++;
        token = strtok(NULL, " ");
    }
    argv[count] = NULL; // Null-terminate the array of arguments
    *arg_count = count;
    return argv;
}

void handle_alias(char **argv, int arg_count) {
    if (arg_count == 1) {
        // Print all aliases
        printAliases(alias_list);
    } else if (arg_count == 2) {
        // Print specific alias
        char *alias_command = findAlias(alias_list, argv[1]);
        if (alias_command) {
            printf("%s='%s'\n", argv[1], alias_command);
        } else {
            fprintf(stderr, "Error: alias not found\n");
        }
    } else {
        // Create or update alias
        // Join all parts of the command for the alias
        int len = 0;
        for (int i = 2; i < arg_count; i++) {
            len += strlen(argv[i]) + 1;
        }

        char *command = malloc(len * sizeof(char));
        command[0] = '\0';

        for (int i = 2; i < arg_count; i++) {
            strcat(command, argv[i]);
            if (i < arg_count - 1) {
                strcat(command, " ");
            }
        }

        addOrUpdateAlias(&alias_list, argv[1], command);
        free(command);
    }
}

void handle_export(char **argv, int arg_count) {
    if (arg_count == 2) {
        char *var = strtok(argv[1], "=");
        char *value = strtok(NULL, "=");

        if (var && value) {
            set_environment_variable(var, value);
        } else {
            fprintf(stderr, "Error: invalid export format\n");
        }
    } else {
        fprintf(stderr, "Error: invalid export format\n");
    }
}

void handle_unset(char **argv, int arg_count) {
    int error = 0;
    if (arg_count > 1) {
        for (int i = 1; i < arg_count; i++) {
            if (unsetenv(argv[i]) != 0) {
                error = 1;
            }
        }
        if (error) {
            fprintf(stderr, "unset: environment variable not present\n");
        }
    } else {
        fprintf(stderr, "Error: invalid unset format\n");
    }
}

void substitute_environment_variables(char **argv, int arg_count) {
    for (int i = 0; i < arg_count; i++) {
        if (argv[i][0] == '$') {
            char var_name[MAX_LINE];
            strcpy(var_name, argv[i] + 1);

            char *var_value = getenv(var_name);
            if (!var_value) {
                var_value = "";
            }

            argv[i] = strdup(var_value);
        }
    }
}

void execute_command(char *cmd) {
    int arg_count;
    char **argv = parse_command(cmd, &arg_count);

    // Substitute environment variables
    substitute_environment_variables(argv, arg_count);

    // Check for built-in commands
    if (strcmp(argv[0], "alias") == 0) {
        handle_alias(argv, arg_count);
        free(argv);
        return;
    } else if (strcmp(argv[0], "export") == 0) {
        handle_export(argv, arg_count);
        free(argv);
        return;
    } else if (strcmp(argv[0], "unset") == 0) {
        handle_unset(argv, arg_count);
        free(argv);
        return;
    }

    // Check if the command is an alias
    char *alias_command = findAlias(alias_list, argv[0]);
    if (alias_command != NULL) {
        // Construct the full command with alias replacement and additional arguments
        char full_cmd[MAX_LINE] = {0};
        strcat(full_cmd, alias_command);

        for (int i = 1; i < arg_count; i++) {
            strcat(full_cmd, " ");
            strcat(full_cmd, argv[i]);
        }

        // Re-parse the full command
        free(argv);
        argv = parse_command(full_cmd, &arg_count);

        // Substitute environment variables again after alias replacement
        substitute_environment_variables(argv, arg_count);
    }

    // Check for redirection
    int redir_fd = -1;
    for (int i = 0; i < arg_count; i++) {
        if (strcmp(argv[i], ">") == 0) {
            if (i + 1 < arg_count) {
                redir_fd = open(argv[i + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                if (redir_fd < 0) {
                    fprintf(stderr, "Redirection error\n");
                    free(argv);
                    return;
                }
                argv[i] = NULL; // End the command before '>'
                arg_count = i; // Update arg_count to the position of '>'
                break;
            } else {
                fprintf(stderr, "Redirection error\n");
                free(argv);
                return;
            }
        }
    }

    // Fork process
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        if (redir_fd != -1) {
            dup2(redir_fd, STDOUT_FILENO); // Redirect stdout
            close(redir_fd);
        }

        if (execvp(argv[0], argv) == -1) {
            fprintf(stderr, "Error: command not found\n");
        }
        free(argv);
        exit(1);
    } else if (pid < 0) {
        // Fork failed
        fprintf(stderr, "Error: fork failed\n");
    } else {
        // Parent process
        if (redir_fd != -1) {
            close(redir_fd);
        }
        wait(NULL);
    }

    free(argv);
}

struct AliasNode* createAliasNode(char *name, char *command) {
    struct AliasNode* newNode = (struct AliasNode*)malloc(sizeof(struct AliasNode));
    newNode->name = strdup(name); // Duplicate the name string
    newNode->command = strdup(command); // Duplicate the command string
    newNode->next = NULL;
    return newNode;
}

// Function to add or update an alias
void addOrUpdateAlias(struct AliasNode** head, char *name, char *command) {
    struct AliasNode* current = *head;

    // Check if the alias already exists, and update it
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            free(current->command); // Free the old command string
            current->command = strdup(command); // Update with the new command
            return;
        }
        current = current->next;
    }

    // If the alias does not exist, add it at the beginning
    struct AliasNode* newNode = createAliasNode(name, command);
    newNode->next = *head;
    *head = newNode;
}

void printAliases(struct AliasNode* head) {
    // Temporary stack to reverse the order of printing
    int count = 0;
    Alias *current = head;

    // Count the number of aliases
    while (current != NULL) {
        count++;
        current = current->next;
    }

    // Create a stack array
    Alias **stack = malloc(count * sizeof(Alias*));
    current = head;
    int i = 0;

    // Push all aliases onto the stack
    while (current != NULL) {
        stack[i] = current;
        i++;
        current = current->next;
    }

    // Pop and print all aliases from the stack
    for (int j = count - 1; j >= 0; j--) {
        printf("%s='%s'\n", stack[j]->name, stack[j]->command);
    }

    free(stack);
}

char* findAlias(struct AliasNode* head, char *name) {
    struct AliasNode* current = head;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current->command;
        }
        current = current->next;
    }
    return NULL; // Alias not found
}

void freeAliasList(struct AliasNode* head) {
    struct AliasNode* current = head;
    struct AliasNode* next;

    while (current != NULL) {
        next = current->next;
        free(current->name);
        free(current->command);
        free(current);
        current = next;
    }
}

void set_environment_variable(char *var, char *value) {
    setenv(var, value, 1);
}

void unset_environment_variable(char *var) {
    if (unsetenv(var) != 0) {
        fprintf(stderr, "unset: environment variable not present\n");
    }
}
