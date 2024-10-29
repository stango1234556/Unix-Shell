#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> 
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h> 
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#define MAX_INPUT 1000
#define MAX_ARGS 100
#define MAX_JOBS 100

char current_working_dir[MAX_INPUT];
char *input = NULL;
size_t len = 0;

//Ignore signals
void signal_handler(int signum){
    signum = signum;
}

//Ignored signals
void setup_signal_handlers(){
    signal(SIGINT, signal_handler);  
    signal(SIGQUIT, signal_handler); 
    signal(SIGTSTP, signal_handler); 
}

void print_prompt(){
    //Get the current working directory
    if(getcwd(current_working_dir, sizeof(current_working_dir)) != NULL){
        // Check if in root directory
        if(strcmp(current_working_dir, "/") == 0){
            printf("[nyush /]$ ");
        } 
        else{
            //Increment past the last '/' 
            char *directory_name = strrchr(current_working_dir, '/');
            directory_name++; 
            printf("[nyush %s]$ ", directory_name);
        }
        //Flush STDOUT
        fflush(stdout);
    } 
    else{
        perror("Error: cannot get directory");
    }
}

//Parsing input
void parse_input(char *input, char **args){
    char *token = strtok(input, " \n");
    int i = 0;
    while(token != NULL){
        args[i++] = token;
        token = strtok(NULL, " \n");
    }
    //Null-terminate the args array
    args[i] = NULL; 
}

//Check if valid command
int is_valid_command(char **args){
    int input_redirected = 0;
    int output_redirected = 0;

    //Check for leading pipes or multiple pipes without commands
    if(args[0] != NULL && strcmp(args[0], "|") == 0){
        fprintf(stderr, "Error: invalid command\n");
        return 0;
    }

    for(int i = 0; args[i] != NULL; i++){
        //Check for input redirection
        if(strcmp(args[i], "<") == 0){
            if(input_redirected || args[i + 1] == NULL){
                fprintf(stderr, "Error: invalid command\n");
                return 0;
            }
            //Mark input redirection as used
            input_redirected = 1; 
            //Skip the next arg (filename expected)
            i++; 
        } 
        //Check for output redirection
        else if(strcmp(args[i], ">") == 0 || strcmp(args[i], ">>") == 0){
            if (output_redirected || args[i + 1] == NULL) {
                fprintf(stderr, "Error: invalid command\n");
                return 0;
            }
            //Mark output redirection as used
            output_redirected = 1; 
            //Skip the next arg (filename expected)
            i++; 
        } 
        //Check for pipes
        else if(strcmp(args[i], "|") == 0){
            if(args[i + 1] == NULL || strcmp(args[i + 1], "|") == 0){
                //No command after pipe or another pipe follows
                fprintf(stderr, "Error: invalid command\n");
                return 0;
            }
        }
    }

    return 1; 
}

//Prepare for redirection
int input_fd = -1;
int output_fd = -1;
char *input_file = NULL;
char *output_file = NULL;
int append;

//Handle command execution with redirection
void execute_single_command(char **args){
    if(!is_valid_command(args)){
        return;
    }
    int input_fd_local = -1;
    int output_fd_local = -1;

    //Parse for input and output redirection
    for(int i = 0; args[i] != NULL; i++){
        if(strcmp(args[i], "<") == 0){
            input_file = args[i + 1];
            //Split args
            args[i] = NULL; 
        } 
        else if(strcmp(args[i], ">") == 0){
            output_file = args[i + 1];
            append = 0;
            //Split args
            args[i] = NULL;
        } 
        else if(strcmp(args[i], ">>") == 0){
            output_file = args[i + 1];
            append = 1;
            //Split args
            args[i] = NULL; 
        }
    }

    //Open input file if provided (https://www.geeksforgeeks.org/input-output-system-calls-c-create-open-close-read-write/)
    if(input_file){
        input_fd_local = open(input_file, O_RDONLY);
        if(input_fd_local < 0){
            fprintf(stderr, "Error: invalid file\n");
            return;
        }
        //(https://www.geeksforgeeks.org/dup-dup2-linux-system-call/)
        dup2(input_fd_local, STDIN_FILENO);
    }

    //Open output file if provided (https://stackoverflow.com/questions/18415904/what-does-mode-t-0644-mean) (https://www.guru99.com/linux-redirection.html)
    if(output_file){
        if(append){
            output_fd_local = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        } 
        else{
            output_fd_local = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }
        if(output_fd_local < 0){
            fprintf(stderr, "Error: invalid file\n");
            return;
        }
        dup2(output_fd_local, STDOUT_FILENO);
    }

    execvp(args[0], args);
    fprintf(stderr, "Error: invalid program\n");
    exit(EXIT_FAILURE);
}

//Function to check if a file exists and is executable
int is_executable(const char *path){
    struct stat buf;
    return (stat(path, &buf) == 0 && (buf.st_mode & S_IXUSR));
}

//Handle multiple pipes
void execute_command(char **args){
    if(!is_valid_command(args)){
            return;
        }
    if(args[0] == NULL){
        return; 
    }

    //Handle built-in commands
    if(strcmp(args[0], "exit") == 0){
        //Arg after exit
        if(args[1] != NULL){
            fprintf(stderr, "Error: invalid command\n");
            return; 
        }
        exit(0);
    } 
    else if(strcmp(args[0], "cd") == 0){
        //No arg or extra arg
        if(args[1] == NULL || args[2] != NULL){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        //No directory
        if(chdir(args[1]) != 0){
            fprintf(stderr, "Error: invalid directory\n");
        }
        return;
    }

    //Count the number of commands separated by pipes
    int pipe_count = 0;
    for(int i = 0; args[i] != NULL; i++){
        if(strcmp(args[i], "|") == 0){
            pipe_count++;
        }
    }

    //Prepare pipes (https://man7.org/linux/man-pages/man2/pipe.2.html)
    int pipe_fds[2 * pipe_count];
    for(int i = 0; i < pipe_count; i++){
        if(pipe(pipe_fds + i * 2) < 0){
            perror("pipe error");
            return;
        }
    }

    int cmd_index = 0; 
    int cmd_number = 0;

    while (1){
        //Check if we've reached the end of commands
        char *cmd_args[MAX_ARGS];
        int j = 0;

        //Collect args for the current command
        while(args[cmd_index] != NULL && strcmp(args[cmd_index], "|") != 0){
            cmd_args[j++] = args[cmd_index++];
        }

        //Null-terminate the command args
        cmd_args[j] = NULL; 
        //Fork and execute the current command
        pid_t pid = fork();
        if(pid == 0){
            //Redirect input from the previous pipe (if not the first command)
            if(cmd_number > 0){
                dup2(pipe_fds[(cmd_number - 1) * 2], STDIN_FILENO);
            }

            //Redirect output to the next pipe (if not the last command)
            if(args[cmd_index] != NULL){
                dup2(pipe_fds[cmd_number * 2 + 1], STDOUT_FILENO);
            }

            //Close all pipe file descriptors
            for(int i = 0; i < 2 * pipe_count; i++){
                close(pipe_fds[i]);
            }
            //Execute the command
            execute_single_command(cmd_args); 
            exit(EXIT_FAILURE);
        } 
        else if (pid < 0){
            perror("fork error");
        }

        //Move to the next command
        cmd_number++;
        if(args[cmd_index] == NULL){
            break; 
        }
        //Skip the pipe character
        cmd_index++; 
    }

    //Close all pipe file descriptors in the parent
    for(int i = 0; i < 2 * pipe_count; i++){
        close(pipe_fds[i]);
    }

    //Wait for all child processes to finish
    for(int i = 0; i <= cmd_number; i++){
        wait(NULL);
    }
}

int main(){
    char input[MAX_INPUT];
    char *args[MAX_ARGS];

    //Set up signal handling
    setup_signal_handlers(); 

    while(1){
        print_prompt();
        if(fgets(input, sizeof(input), stdin) == NULL){
            //Exit on EOF
            break; 
        }
        parse_input(input, args);
        execute_command(args);
    }
    return 0;
}