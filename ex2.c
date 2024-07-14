#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>

#define MAX_STR 1025
int isAmperSand = 0; // flag to know if we in background command case
int cmdSuccess = 0;
int lastCmdSuccess = 0; // flag to know if the last cmd succeeded
int fileNameExist = 0; // flag to know if in redirection errors case the file exists so need to add 1 to the arguments
int stdErr = 0; // flag to know if we in redirection errors case
char filename[MAX_STR];
int isOpened = 0; // flag for redirection errors case in conditional commands, so I can recognize if file created already
int andOrFlag = 0; // flag to know if we in conditional commands case
int returnAmper = 0; // flag for conditional command like ls && sleep 50& that if a flag is 1, so I know its background command
typedef struct Command {
    char* command;
    char* operator; // "&&" or "||"
} Command;

typedef struct Job {
    int pid;
    int jobId;
    char command[MAX_STR];
    struct Job *next;
} Job;
Job *backgroundList = NULL;
int jobCount = 0;

typedef struct Node {
    char *key;
    char *value;
    struct Node *next;
} Node;

void addJob(Job **jobList, int pid, const char *command) {
    Job *newJob = (Job *)malloc(sizeof(Job));
    if (!newJob) {
        perror("malloc");
        return;
    }
    newJob->pid = pid;
    newJob->jobId = ++jobCount;  // Increment jobCount and assign to jobId
    strncpy(newJob->command, command, MAX_STR);
    newJob->next = NULL;

    if (*jobList == NULL) {
        *jobList = newJob;
    }
    else {
        Job *lastJob = *jobList;
        while (lastJob->next != NULL) {
            lastJob = lastJob->next;
        }
        lastJob->next = newJob;
    }
}

void deleteJob(Job **jobList, int pid) {
    Job *current = *jobList;
    Job *prev = NULL;

    while (current != NULL) {
        if (current->pid == pid) {
            if (prev == NULL) { // Job to delete is the first job in the list
                *jobList = current->next;
            } else { // Job to delete is not the first
                prev->next = current->next;
            }
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

void removeEndSpaces(char *str) {
    if (str == NULL) return; // Handle NULL pointer if passed

    int end = strlen(str) - 1; // Start at the last character of the string

    // Move backwards through the string until a non-space character is found
    while (end >= 0 && isspace(str[end])) {
        end--;
    }

    // Null-terminate the string right after the last non-space character
    str[end + 1] = '\0';
}

void printJobs(Job *jobList) {
    Job *current = jobList;
    while (current) {
        printf("[%d]               %s &\n", current->jobId, current->command);
        current = current->next;
    }
}

void freeJobList(Job *jobList) {
    Job* current = jobList;
    Job* nextJob;
    while (current != NULL) {
        nextJob = current->next;
        free(current);
        current = nextJob;
    }
    jobList = NULL;
}

void insert(Node **head, const char *key, const char *value) {
    Node *new_node = (Node *)malloc(sizeof(Node));
    if (!new_node) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    new_node->key = strdup(key);
    new_node->value = strdup(value);
    new_node->next = *head;
    *head = new_node;
}

char *search(Node *head, const char *key) { // Search for a value by key and return as string
    Node *current = head;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return strdup(current->value);
        }
        current = current->next;
    }
    return NULL;
}

int aliasExists(Node *head, const char *key) { // check if alias word already exists
    Node *current = head;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return 1; // key exist
        }
        current = current->next;
    }
    return 0; // key doesnt exist
}

void delete(Node **head, const char *key) {
    Node *current = *head;
    Node *prev = NULL;

    while (current) {
        if (strcmp(current->key, key) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                *head = current->next;
            }
            free(current->key);
            free(current->value);
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

void free_Alias_List(Node *head) {
    Node *current = head;
    while (current) {
        Node *temp = current;
        current = current->next;
        free(temp->key);
        free(temp->value);
        free(temp);
    }
    head = NULL;
}

void print_Alias_List(Node *head) {
    Node *current = head;
    while (current) {
        printf("%s='%s'\n", current->key, current->value);
        current = current->next;
    }
}

int countQuotes(const char command[], int size, int* counter) { // counting how many quotes commands I had
    for (int i = 0; i < size; ++i) {
        if (command[i] == '\'' || command[i] == '\"'){
            (*counter)++;
            break;
        }
    }
    return (*counter);
}

void checkSpaces(char *str) { // function created mainly to check if command starting with spaces before
    int index = 0, i;

    // Find the index of the first non-space character
    while (isspace(str[index])) {
        index++;
    }

    // Shift all characters to the left
    for (i = 0; str[index] != '\0'; i++, index++) {
        str[i] = str[index];
    }
    str[i] = '\0';
}

void findAmper(char* command, int size){
    checkSpaces(command);
    removeEndSpaces(command);
    if (command[0] == '&'){
        return;
    }
    if (command[size-1] == '&'){
        command[size-1] = '\0';
        isAmperSand = 1;
    }
}

int checkIfExit(char* args[], int numOfQuotes){ // check if the command is exit_shell
    if (args[0] != NULL && strcmp(args[0], "exit_shell") == 0) {
        printf("%d", numOfQuotes);
        return 1;
    }
    return 0;
}

void extractRedirectionFilename(char *command, char *filenameBuffer, int* fileExist) { // extract the file name from 2> command
    char copy[MAX_STR];
    strcpy(copy,command);
    char *ptr = strstr(copy, "2>"); // locate the 2>
    if (ptr) {
        ptr += 2; // Move past "2>" to start of filename
        while (*ptr == ' ') ptr++; // Skip any spaces between "2>" and the filename

        char *end = ptr;
        while (*end && !isspace(*end)) end++; // Find the end of the filename
        *end = '\0'; // Null-terminate the filename

        strcpy(filenameBuffer, ptr); // Copy the filename into the buffer
    }
    else {
        filenameBuffer[0] = '\0'; // Ensure the buffer is empty if no redirection is found
    }
    if (filenameBuffer[0] != '\0'){
        (*fileExist)++;
    }
}

void handler(int num){
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0){
        deleteJob(&backgroundList, pid);  // Delete the job from the list if its terminated
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            cmdSuccess++;
            lastCmdSuccess = 1;  // Command executed successfully (problem if sleep long I get answer in delay
        }

        else {
            lastCmdSuccess = 0;  // Command failed
        }
    }
}

void executeCommand(char *args[], int* success, char* command, const int* argsCount) {
    pid_t pid;
    pid = fork();
    signal(SIGCHLD, handler);
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    else if (pid == 0) {

        if(stdErr){
            int fd;
            if (!isOpened) {
                fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            }
            else{
                fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0666);

            }
            if (fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }

            if (dup2(fd, STDERR_FILENO) == -1) {
                close(fd);
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(fd);
        }
        // Check argument count before executing the command
        if (*argsCount > 5) {
            if (stdErr) {
                fprintf(stderr, "ERR\n");
                exit(EXIT_FAILURE);
            }
            else{
                fprintf(stdout, "ERR\n");
                lastCmdSuccess = 0;
               // printf("ERR\n");
                exit(EXIT_FAILURE);

            }
        }
        else{
        execvp(args[0], args);
        perror("exec");
        exit(EXIT_FAILURE);
        }
    }

    else {
        if(stdErr){
            isOpened = 1;
        }
        if (isAmperSand == 0) { // normal command
            pause();
            *success = lastCmdSuccess;  // Set success to the last command's success status
        }
        else{ // background command
            findAmper(command, strlen(command));
            removeEndSpaces(command);
            addJob(&backgroundList, pid, command);  // Add a background job
          //  isAmperSand = 0;
            *success = lastCmdSuccess;  // Assume success for background processes
        }
    }
}

void aliasCommand(Node** alias, char* args[], const int* i, int* activeAlias){
    if (*i == 1) {
        print_Alias_List(*alias);
        cmdSuccess++;
    }
    else if (*i >= 3) {
        if (!aliasExists(*alias, args[1])) { // if the alias word isn't in use
            (*activeAlias)++;
        }

        else {
            delete(alias, args[1]);
        }

        insert(alias, args[1], args[2]);
        cmdSuccess++;
    }
}

void unaliasCommand(Node **alias, char *args[], const int* argsCounter, int *activeAlias) {
    if (*argsCounter < 2) {
        printf("ERR\n");
    }

    else if (*argsCounter > 1) {
        if (aliasExists(*alias, args[1])) {
            delete(alias, args[1]);
            (*activeAlias)--;
            cmdSuccess++;
        }

        else {
            printf("ERR\n");
        }
    }
}

void parseCommand(char *command, char *args[], int *arg_count, int size, Node* alias) {
    int inDoubleQuotes = 0; // 0 - no quotes, 1 - single quote, 2 - double quote
    int inSingleQuotes = 0;
    char *start = command;
    char* ptr = command;
    int equalSign = 0; // to check if the sign = exists after alias command
    int equalSignCounter = 0;

    for (int i = 0; i <= size; ++i, ptr++) {
        char* prev = ptr-1;
        char* next = ptr+1;

        if(args[0] != NULL && strcmp(args[0], "alias") == 0 && !aliasExists(alias, args[0])){ // if alias exists so its mean alias is override alias command

            if (*ptr == '\0' && *next == '\0' && *start == '\0') { // one word with spaces like alias   .
                break;
            }

            if(*ptr == '\0' && start == ptr && i == size && (*next == '&' || *next == '|')){
                break;
            }

            if (i == size && (!inSingleQuotes && !inDoubleQuotes) && *next == '\0') {
                args[0] = NULL;
            }

            if (*ptr == '=') {
                equalSign = 1;
                equalSignCounter++;
            }

            if (*ptr == '\'' && !inDoubleQuotes) {
                if (inSingleQuotes == 1) { // closing single quote
                    *ptr = '\0';
                    args[*arg_count] = start;
                    (*arg_count)++;
                    inSingleQuotes = 0;
                    start = ptr + 1;
                    if (*start == '\0') {
                        if (!equalSign) { // for case like alias g 'ls'
                            args[0] = NULL;
                            break;
                        }
                        break;
                    }
                }
                else {
                    inSingleQuotes = 1; // opening single quote
                    start = ptr + 1;
                }
            }

            else if (*ptr == '=') {
                if ((*prev == '\0' && *next == ' ') || *prev == ' ' || equalSignCounter > 1) { // if I have spaces after the = sign || case of alias g = 'echo "1+2=3"'
                    continue;
                }
                else {
                    *ptr = '\0';
                    if (start != ptr) {
                        args[*arg_count] = start;
                        (*arg_count)++;
                        start = ptr + 1;
                    }
                }
            }

            else if (*ptr == '\"' && !inSingleQuotes) { // Handle double quotes
                if (inDoubleQuotes == 2) { // if we reach here so we at the second "
                    *ptr = '\0';
                    args[*arg_count] = start;
                    (*arg_count)++;
                    inDoubleQuotes = 0;
                    start = ptr + 1;
                    if (*start == '\0') {
                        if (!equalSign) { // for case like alias g "ls"
                            args[0] = NULL;
                            break;
                        }
                        break;
                    }
                }
                else {
                    inDoubleQuotes = 2;
                    start = ptr + 1;
                }
            }

            if ((*ptr == ' ' && *prev == ' ') || (*ptr == ' ' && *prev == '\0')) { // if I have more than 1 space alias   g   =   'ls'
                start = ptr + 1;
                continue;
            }

            if ((*ptr == ' ' && *prev != '=' && !inSingleQuotes && !inDoubleQuotes) || (i == size) || *next == '\0') {
                if (*ptr != '\'' && *next == '\0') { // in case of that, the last sign of the alias command isn't quote, example : alias g = 'ls/
                    args[0] = NULL;
                    break;
                }
                *ptr = '\0';
                args[*arg_count] = start;
                (*arg_count)++;
                start = ptr + 1;
            }

            if (*start == '\0') {
                if(!inSingleQuotes && !inDoubleQuotes){
                    args[0] = NULL;
                }
                else {
                    break;
                }
            }
        }

        else if ((*ptr == ' ' && !inDoubleQuotes && !inSingleQuotes) || i == size) { // If we hit a space, and we're not in quotes, it's the end of an argument
            *ptr = '\0';
            if (start != ptr && *start != '\0') {
                args[*arg_count] = start;
                (*arg_count)++;
            }
            start = ptr + 1;
        }

        else if (*ptr == '\"' && !inSingleQuotes) {
            // Handle double quotes
            if (inDoubleQuotes == 2) { // if we reach here so we at the second "
                *ptr = '\0';
                args[*arg_count] = start;
                (*arg_count)++;
                inDoubleQuotes = 0;
                start = ptr + 1;
                if (*start == '\0'){
                    break;
                }
            }
            else {
                inDoubleQuotes = 2;
                start = ptr + 1;
            }
        }

        else if (*ptr == '\'' && !inDoubleQuotes) {
            if (inSingleQuotes == 1) { // closing single quote
                *ptr = '\0';
                args[*arg_count] = start;
                (*arg_count)++;
                inSingleQuotes = 0;
                start = ptr + 1;
                if (*start == '\0') {
                    break;
                }
            }

            else {
                inSingleQuotes = 1; // opening single quote
                start = ptr + 1;
            }
        }
        else if (*ptr == '\0' && *next == '\0' && *start != '\0' && i+1 == size){ // in case of I have command like sleep 5 &
            if (start != ptr) { // in case of I have command sleep 5&
                args[*arg_count] = start;
                (*arg_count)++;
            }
            break;
        }
        else if(*ptr == '2' && *next == '>'){
            break;
        }
    }
    args[*arg_count] = NULL;
}

void removeOuterParentheses(char *command) {
    int length = strlen(command);
    if (length == 0) return; // Empty string, nothing to do

    checkSpaces(command);

    int leftIndex = -1;
    int rightIndex = -1;
    int counter = 0;

    for (int i = 0; i < length; ++i) {
        if (command[i] == '(' && leftIndex == -1) {
            leftIndex = i;
            counter++;
        }
        if (command[i] == ')') {
            rightIndex = i;
            counter++;
        }
    }

    if (counter > 1 && leftIndex < rightIndex) {
        // Shift the string left to remove the outermost parentheses
        memmove(command + leftIndex, command + leftIndex + 1, rightIndex - leftIndex - 1);
        memmove(command + rightIndex - 1, command + rightIndex + 1, length - rightIndex);
        command[length - 2] = '\0';
    }
}

void checkIfAndOrCase (const char* command, int size){
    for (int i = 0; i < size-1; ++i) {
        if (command[i] == '&' && command[i+1] == '&'){
           andOrFlag = 1;
            return;
        }

        if (command[i] == '|' && command[i+1] == '|'){
            andOrFlag = 1;
            return;
        }
    }
    andOrFlag = 0;
}

void executeConditionalCommands(Command commands[], char* args[], int count, int* argsCount, int* flag) {
    int lastSuccess = 0; // Assume the first command always runs

    if (isAmperSand && commands[count].operator != NULL){ // if its conditional + background command, remember the last command is background
        returnAmper = 1;
        isAmperSand = 0;
    }

    if (commands[count].operator == NULL && returnAmper){ // we at the last command, return the ampersand flag
        isAmperSand = 1;
        (*argsCount)++;
    }

    if (count == 0){
        executeCommand(args, &lastSuccess, commands[count].command, argsCount);
        if (strcmp(commands[0].operator, "||") == 0 && lastCmdSuccess){
            *flag = 1;
        }
        return;
    }

    if (((strcmp(commands[count-1].operator, "&&") == 0 && lastCmdSuccess) || (strcmp(commands[count-1].operator, "||") == 0 && !lastCmdSuccess))){
        executeCommand(args, &lastSuccess, commands[count].command, argsCount);
    }

    else{
        lastCmdSuccess = 0;
    }
}

void handleConditionalCommands(char* input, Command commands[], int maxCommands, Node** alias, int* quotes) { // parse commands with && / || operators
    int cmdCount = 0;
    int argsCount = 0;
    int flag = 0;
    char copy[MAX_STR];
    int jobCmd = 0;
    int copyArgsCounter;
    char *localArgs[6] = {NULL};// Ensure you have defined MAX_ARGS appropriately

    char *token = strtok(input, "&&||");
    while (token != NULL && cmdCount < maxCommands) {
        strcpy(copy, token);
        removeEndSpaces(copy);
        checkSpaces(copy);

        commands[cmdCount].command = copy;  // Allocate new memory for the command
        commands[cmdCount].operator = NULL;

        if (input[token - input + strlen(token) + 1] == '&') {
            commands[cmdCount].operator = "&&";
        }

        else if (input[token - input + strlen(token) + 1] == '|') {
            commands[cmdCount].operator = "||";
        }

        char parseBuffer[MAX_STR];
        strcpy(parseBuffer, copy);
        parseCommand(parseBuffer, localArgs, &argsCount, strlen(parseBuffer), *alias);
        copyArgsCounter = argsCount;

        if (stdErr) {
            copyArgsCounter += fileNameExist;
            copyArgsCounter++;
        }

        argsCount = 0;
        if (!flag) {
            if (checkIfExit(localArgs, *quotes)) {
                exit(0);
            }

            if (strcmp(copy, "jobs") == 0) { // catch job command
                printJobs(backgroundList);
                cmdSuccess++;
                jobCmd = 1;
                lastCmdSuccess = 1;
                if (backgroundList == NULL) { // if did jobs command and the list is null, reset the jobs counter
                    jobCount = 0;
                }
            }

            if (!jobCmd) {
                executeConditionalCommands(commands, localArgs, cmdCount, &copyArgsCounter, &flag);
            }
            if (lastCmdSuccess) {
                countQuotes(copy, strlen(copy), quotes);
            }
        }
        token = strtok(NULL, "&&||");
        jobCmd = 0;
        cmdCount++;

    }
}

void aliasWord(Node *alias, char* command, int cmdLength, char *args[], char *copyArgs[], int *arg_count, int* success) {
    char *alias_values = search(alias, args[0]);
    if (alias_values != NULL) { // if we reach here so we in alias shortcut command
        int aliasLength = strlen(alias_values);
        int i = 0;
        for (int j = 0; j < 6; ++j) {
            copyArgs[j] = NULL;
        }
        parseCommand(command, copyArgs, &i, cmdLength, alias); // separate the values of the alias if there is more than 1 word
        args[0] = alias_values;

        i = 0;
        parseCommand(args[0], args, &i, aliasLength, NULL); // separate the values of the alias if there is more than 1 word
        int copyArgsCounter = 1;

        for (int j = i; j < 6; ++j) { // if another args array has more elements except of the key will move it to the main args array
            if (copyArgs[copyArgsCounter] == NULL) {
                break;
            }
            args[i++] = copyArgs[copyArgsCounter++];
        }
        if (i < 6){
            executeCommand(args, success, command, arg_count);
        }
        *arg_count = i; // update the argument count
    }
    free(alias_values);
}

int sterrCase(const char* command, int size){ // in case of I have the 2>
    for (int i = 0; i < size; ++i) {
        if(command[i] == '2' && command[i+1] == '>' && (i+1)<size){
            return 1;
        }
    }
    return 0;
}

void scriptCase(Node** alias, char *args[], char *copyArgs[], int *arg_count, int *activeAlias, int *scriptLines, int* success, int* quotesCounter) {
    char fileRow[MAX_STR];
    char copyFile[MAX_STR];
    int i = *arg_count;
    FILE *script = fopen(args[1], "r");

    if (script == NULL) {
        printf("ERR\n");
        return;
    }

    fgets(fileRow, sizeof(fileRow), script);
    fileRow[strcspn(fileRow, "\n")] = '\0'; // put \0 at the \n index
    checkSpaces(fileRow); // if there are spaces before the bin/bash command shift them left to the start
    if (strcmp(fileRow, "#!/bin/bash") != 0){
        printf("ERR\n");
        fclose(script);
        return;
    }
    cmdSuccess++;

    fgets(fileRow, sizeof(fileRow), script);
    while (!feof(script)) {
        if (fileRow[0] == '#' || fileRow[0] == '\n') { //
            (*scriptLines)++;
            fgets(fileRow, sizeof(fileRow), script);
            continue;
        }
        stdErr = 0;
        fileNameExist = 0;

        fileRow[strcspn(fileRow, "\n")] = '\0';
        strcpy(copyFile, fileRow);
        int fileLen = strlen(copyFile);

        findAmper(fileRow, fileLen);
        if(sterrCase(fileRow, fileLen)){ // 2> case
            extractRedirectionFilename(fileRow, filename, &fileNameExist); // extract the name of the file from the command
            stdErr = 1;
        }

        removeOuterParentheses(fileRow);
        Command commands[3]; // Supports up to 3 commands
        checkIfAndOrCase(fileRow, fileLen); // Conditional command case
        if (andOrFlag) {
            handleConditionalCommands(fileRow, commands, 3, alias, quotesCounter);
            continue;
        }

        for (int j = 0; j < i; ++j) {
            args[j] = NULL;
        }
        i = 0;

        if (strcmp(fileRow, "jobs") == 0) { // catch job command
            printJobs(backgroundList);
            cmdSuccess++;
            fgets(fileRow, sizeof(fileRow), script);
            if (backgroundList == NULL){ // if did jobs command and the list is null, reset the jobs counter
                jobCount = 0;
            }
            continue;
        }

        parseCommand(fileRow, args, &i, fileLen, *alias);
        i += fileNameExist;

        if(args[0] == NULL){
            printf("ERR\n");
            (*scriptLines)++;
            fgets(fileRow, sizeof(fileRow), script);
            continue;
        }

        aliasWord(*alias, copyFile, fileLen, args, copyArgs, &i, success); // check if args[0] is an alias word
        if (*success){
            countQuotes(copyFile, fileLen, quotesCounter);
            *success = 0;
            (*scriptLines)++;
            fgets(fileRow, sizeof(fileRow), script);
            continue;
        }

        if (i > 5){ // if we have more than 5 arguments
            printf("ERR\n");
            (*scriptLines)++;
            fgets(fileRow, sizeof(fileRow), script);
            continue;
        }

        if (strcmp(args[0], "alias") == 0) { // alias command case
            aliasCommand(alias, args, &i, activeAlias);
            (*scriptLines)++;
            countQuotes(copyFile, fileLen, quotesCounter);
            fgets(fileRow, sizeof(fileRow), script);
            continue;
        }

        if(strcmp(args[0], "unalias") == 0){ // unalias command case
            unaliasCommand(alias, args, &i, activeAlias);
            (*scriptLines)++;
            fgets(fileRow, sizeof(fileRow), script);
            continue;
        }

        if(checkIfExit(args, *quotesCounter)){ // exit_shell command case
            freeJobList(backgroundList);
            free_Alias_List(*alias);
            fclose(script);
            exit(0);
        }

        executeCommand(args, success,copyFile, arg_count);
        if (*success){ // if command succeeded: count quotes
            countQuotes(copyFile, fileLen, quotesCounter);
            (*success) = 0;
        }
        (*scriptLines)++;
        fgets(fileRow, sizeof(fileRow), script);
    }
    fclose(script);
    *arg_count = i;
}

int main(void) {
    char command[MAX_STR];
    char *copyArgs[6] = {NULL};
    int activeAlias = 0;
    int scriptLines = 0;
    int success = 0; // check if command succeeded
    int quotesCounter = 0;
    Node *alias = NULL;

    while (1) {
        printf("#cmd:%d|#alias:%d|#script lines:%d> ", cmdSuccess, activeAlias, scriptLines);
        isOpened = 0; // redirection file is not opened yet
        isAmperSand = 0;
        andOrFlag = 0;
        fgets(command, sizeof(command), stdin);
        if(command[strlen(command)-1] != '\n'){ // checking the command is less than MAX_STR length (1025)
            printf("ERR\n");
            while (fgetc(stdin) != '\n');
            continue;
        }

        command[strcspn(command, "\n")] = 0;
        int cmdLength = strlen(command);
        char copyCommand[MAX_STR];
        char copyCommand2[MAX_STR];
        strcpy(copyCommand, command);
        strcpy(copyCommand2, command);
        char *args[6] = {NULL};
        int argsCounter = 0;
        stdErr = 0;
        fileNameExist = 0;

        findAmper(command, cmdLength); // checking if its background command
        if(sterrCase(command, cmdLength)){ // 2> case
            extractRedirectionFilename(command, filename, &fileNameExist); // extract the name of the file from the command
            stdErr = 1;
        }

        removeOuterParentheses(command);
        Command commands[3]; // Supports up to 3 commands
        checkIfAndOrCase(command, cmdLength); // Conditional command case
        if (andOrFlag) {
            handleConditionalCommands(command, commands, 3, &alias, &quotesCounter);
            continue;
        }

        parseCommand(command, args, &argsCounter, cmdLength, alias);
        argsCounter += fileNameExist; // to add the file name in case of 2> redirection
        if (stdErr){ // to add the 2> as argument
            argsCounter++;
        }

        if (isAmperSand){
            argsCounter++;
        }

        if (strcmp(command, "jobs") == 0) { // catch job command
            printJobs(backgroundList);
            cmdSuccess++;
            if (backgroundList == NULL){ // if did jobs command and the list is null, reset the jobs counter
                jobCount = 0;
            }
            continue;
        }

        if (copyCommand[0] == '\0'){
            continue;
        }

        if(args[0] == NULL){
            printf("ERR\n");
            continue;
        }

        aliasWord(alias, copyCommand, cmdLength, args, copyArgs, &argsCounter, &success); // check if args[0] is an alias word
        if (success){
            countQuotes(copyCommand2, cmdLength, &quotesCounter);
            success = 0;
            continue;
        }

        if (strcmp(args[0], "alias") == 0) { // alias command case
            aliasCommand(&alias, args, &argsCounter, &activeAlias);
            countQuotes(copyCommand, cmdLength, &quotesCounter);
            continue;
        }

        else if (strcmp(args[0], "unalias") == 0){ // unalias command case
            unaliasCommand(&alias, args, &argsCounter, &activeAlias);
            continue;
        }

        if(checkIfExit(args, quotesCounter)){ // exit_shell command case
            break;
        }

        if (strcmp(args[0], "source") == 0) { // script case
            if(command[cmdLength-1] != 'h' && command[cmdLength-2] != 's' && command[cmdLength-3] != '.'){
                printf("ERR\n");
                continue;
            }
            scriptCase(&alias, args, copyArgs, &argsCounter, &activeAlias, &scriptLines, &success, &quotesCounter);
            continue;
        }

        else {
            executeCommand(args, &success, copyCommand, &argsCounter);
        }

        if (success){ // if command succeeded
            countQuotes(copyCommand, cmdLength, &quotesCounter);
        }
        success = 0;
    }
    freeJobList(backgroundList);
    free_Alias_List(alias);
    return 0;
}