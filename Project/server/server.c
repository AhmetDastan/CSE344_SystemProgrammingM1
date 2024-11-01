#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>   
#include <signal.h>
#include <dirent.h> // for directory operations

#include <semaphore.h> // for semaphore
#include <sys/mman.h>  // for mmap - shared memory
 
#include "../client.h"   // client struct and request response struct

void initClientLists();    // Initialize the active clients and waiting queue
void addClientToList(Client **list, int *count, int *capacity, Client newClient);  // Add a client to the list
void addClientToActive(Client newClient);   // Add a client to the active clients list
void addClientToWaiting(Client newClient);  // Add a client to the waiting queue
int removeClientFromList(Client *list, int *count, int pid); // Remove a client from the list
void activateClientFromQueue(); // Activate a client from the waiting queue
Client getLastElementofQUE(Client *list); // Get the last element of the queue
int isEmptyQueue(Client *list); // Check if a specific queue is empty


void manageClient(char* clientReturnFifo); // Manage the client
char* getLine(FILE *file, int lineNumber); // Get a specific line from a file

char mainFifo[10];


Client *activeClients;
int activeCount = 0;
int activeCapacity;

Client *waitingQueue;
int waitingCount = 0;
int waitingCapacity = 100;


///////// shared memory Design with semaphoers Constants//////////
int *sharedSequentialClientCounter; // shared memory for sequentialClientCounter

const char *semaphoreName = "mysemaphore";  // semaphore name
sem_t *sem; // define semaphore

const char *counterSharedMemory = "/clientCounter";  // shared memory name
///////// shared memory Design with semaphoers Constants//////////

void handleInterruptSignal(int sig) {
    printf("SIGTSTP received, unlinking FIFO...\n");

    // Unlink the FIFO
    if (unlink(mainFifo) == -1) { // if already unlinked, it will return -1
        perror("Failed to unlink FIFO");
    }
    // kill clients and child process
    for (int i = 0; i < activeCount; i++) {
        kill(activeClients[i].clientPid, SIGKILL);
    }
    // release shared memory
    munmap(sharedSequentialClientCounter, sizeof(int));
    shm_unlink(counterSharedMemory);

    // release semaphores
    sem_close(sem);
    sem_unlink(semaphoreName);

    kill(getpid(), SIGKILL); // kill the server process
}

int main(int argc, char *argv[]){

    if(argc != 3){
        perror("server wrong number of arguments");
        return -1;
    }

    activeCapacity = atoi(argv[2]);
    if(activeCapacity < 1){
        perror("server wrong number of arguments");
        return -1;
    } 


///// shared memory Design with semaphoers//////////   
    int shm_fd = shm_open(counterSharedMemory, O_CREAT | O_RDWR, 0666);  // define shared memory file descriptor
    if (shm_fd == -1) {
        perror("shm_open");
        return EXIT_FAILURE;
    }
    if (ftruncate(shm_fd, sizeof(int)) == -1) {  // set size of shared memory
        perror("ftruncate");
        return EXIT_FAILURE;
    }
    sharedSequentialClientCounter = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);  // map shared memory
    if (sharedSequentialClientCounter == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }
    *sharedSequentialClientCounter = 0;  // initialize shared memory
    // create semaphore
    sem = sem_open(semaphoreName, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        return EXIT_FAILURE;
    } 
///// shared memory Design with semaphoers//////////

///// signal handler //////
    struct sigaction sa;
    sa.sa_handler = handleInterruptSignal; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // No special flags
    
    if (sigaction(SIGTSTP, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
///// signal handler //////
 
    char dirName[256]; 
    strcat(dirName, argv[1]); 

    initClientLists(); // with this function i create 2 list for active clients and waiting clients
    sprintf(mainFifo, "%d", getpid()); // Convert server PID to string 
    // Create named pipe
    if (mkfifo(mainFifo, 0777) < 0 && errno != EEXIST) {
        perror("server mkfifo failed");
        exit(EXIT_FAILURE);
    }

    
    // server get request from client
    int serverParentPid = getpid();
    printf(">> Server Started PID %d\n", serverParentPid);
    printf(">> Waiting for clients...\n");
    int serverResponseFd, foundClient = 0;
    while(1){
        // open fifo for reading connection
        int serverResponseFd = open(mainFifo, O_RDONLY); 
        // Read client PID
        int clientPid;
        if (read(serverResponseFd, &clientPid, sizeof(int)) < 0) {
            perror("read");
            close(serverResponseFd);
            exit(EXIT_FAILURE);
        }else foundClient = 1; 
        if(foundClient == 1 && !(activeCount >= activeCapacity)){        
            foundClient = 0;
            // Fork a new process to handle client
            int pid = fork();
            if (pid < 0) {
                perror("fork error \n");
                exit(EXIT_FAILURE);
            } else if (pid == 0) { // Child process
                ++activeCount; // when created a new fork for a new client increase the client counter
                char newClientPid[256]; // string of client pid
                sprintf(newClientPid, "%d", clientPid); // Convert client PID to string 
                manageClient(newClientPid); // Open FIFO for writing with the spesific client
                exit(EXIT_SUCCESS);
            }
        }else if(foundClient == 1 && activeCount >= activeCapacity){
            // printf serverside to full clients
            printf("Connection request PID %d... Que FULL\n", clientPid);
            // add wait queue the client
            Client newClient;
            newClient.clientPid = clientPid; 
            // add wait queue the client
            addClientToWaiting(newClient);
        }else if(foundClient == 0 && (activeCount < activeCapacity) && waitingCount > 0){
            // printf serverside to full clients
            Client newClient = getLastElementofQUE(waitingQueue);
            int pid = fork();
            if (pid < 0) {
                perror("fork error \n");
                exit(EXIT_FAILURE);
            } else if (pid == 0) { // Child process
                ++activeCount; // when created a new fork for a new client increase the client counter
                char newClientPid[256]; // string of client pid
                sprintf(newClientPid, "%d", newClient.clientPid); // Convert client PID to string 
                manageClient(newClientPid); // Open FIFO for writing with the spesific client
                exit(EXIT_SUCCESS);
            } 
        }
    }


    // release shared memory
    munmap(sharedSequentialClientCounter, sizeof(int));
    shm_unlink(counterSharedMemory);
    
    // release semaphores
    sem_close(sem);
    sem_unlink(semaphoreName);

    // Close the FIFO
    close(serverResponseFd);
    unlink(mainFifo);
    // Free the allocated memory
    free(activeClients);
    free(waitingQueue);

    kill(getpid(), SIGKILL); // kill the server process
    return 0;
}

void manageClient(char* clientReturnFifo){
    Client currentClient;
    currentClient.clientPid = atoi(clientReturnFifo);
    
    sem_wait(sem);  // Wait (decrement semaphore)
    (*sharedSequentialClientCounter)++;
    currentClient.connectId = *sharedSequentialClientCounter;
    sem_post(sem);  // Signal (increment semaphore) 
    
    addClientToActive(currentClient); 
    // Open FIFO for writing with the spesific client
    int clientFd = open(clientReturnFifo, O_WRONLY); 
    int writebytes, readBytes;  
    if(writebytes = write(clientFd, "success", sizeof("success")) < 0){ // connection is succesfull
        printf("Client connection error\n");
        close(clientFd);
        exit(EXIT_FAILURE);
    }else printf(">> Client PID %d connected as \"Client %d \" \n", currentClient.clientPid, currentClient.connectId);
    close(clientFd);

    char *newReq[50];
    char tempMssg[1024];
    while(1){ // get request from client and then responce to client
        memset(tempMssg, 0, sizeof(tempMssg));  // clear the tempMssg 
        clientFd = open(clientReturnFifo, O_RDONLY);
        // Connection is succesfull, hadnle the client
        // get request from client 
        if(readBytes = read(clientFd, &tempMssg, sizeof(tempMssg)) < 0){
            perror("read");
            close(clientFd);
            exit(EXIT_FAILURE);
        }
        
        // Parse the request
        char *token = strtok(tempMssg, " ");
        int tokenCounter = 0;
        while (token != NULL) {
            newReq[tokenCounter++] = token;  // Save the token in the array
            if (tokenCounter >= 50) {  // Check for array overflow
                break;
            }
            token = strtok(NULL, " ");  // Get the next token
        } 
        
        // - help; display the list of possible client request
        if (strcmp(newReq[0], "help") == 0) { 
            // Help request   
            if(newReq[1] == NULL){
                snprintf(tempMssg, sizeof(tempMssg),"Available comments are :\nhelp, list, readF, writeT, upload, download, archServer, quit, killServer"); 
                printf("burasi temp mesaj %s\n", tempMssg);
            }else if(strcmp(newReq[1], "readF") == 0){
                printf("anans");
                snprintf(tempMssg, sizeof(tempMssg), "readF <file> <line # \n> requests to display the # line of the <file>, if no line number is given \n the whole contents of the file is requested (and displayed on the client side)");
            }else if(strcmp(newReq[1], "writeT") == 0){
                strcpy(tempMssg,"writeT <file> <line #> <string> \n : request to write the content of “string” to the #th line the <file>, if the line # is not given \n writes to the end of file. If the file does not exists in Servers directory creates and edits the\n file at the same time");
            }else if(strcmp(newReq[1], "upload") == 0){
                strcpy(tempMssg,"upload <file> \nuploads the file from the current working directory of client to the Servers directory \n(beware of the cases no file in clients current working directory and file with the same\n name on Servers side)");
            }else if(strcmp(newReq[1], "download") == 0){
                strcpy(tempMssg,"download <file> \nrequest to receive <file> from Servers directory to client side");
            }else if(strcmp(newReq[1], "archServer") == 0){
                strcpy(tempMssg,"archServer <fileName>.tar \n Using fork, exec and tar utilities create a child process that will collect all the files currently \navailable on the the Server side and store them in the <filename>.tar archive");
            }else if(newReq[1] == "killServer"){
                strcpy(tempMssg,"killServer \nSends a kill request to the Server");
            }else if(newReq[1] == "quit"){
                strcpy(tempMssg,"quit \nSend write request to Server side log file and quits");
            }
            
            int clientFd = open(clientReturnFifo, O_WRONLY);
            if (clientFd < 0) {
                perror("server open fifo failed");
                exit(EXIT_FAILURE);
            } 
            if (write(clientFd, &tempMssg, sizeof(tempMssg)) < 0) {
                perror("server write failed");
                exit(EXIT_FAILURE);
            } 
            close(clientFd);
        }else if(strcmp(newReq[0], "list") == 0){
            // list all server directory elemetns
            DIR *dir;
            struct dirent *ent;
            int clientFd = open(clientReturnFifo, O_WRONLY, 0666);
            if (clientFd < 0) {
                perror("server open fifo failed");
                exit(EXIT_FAILURE);
            }
            // Open the current directory
            if ((dir = opendir("./server")) != NULL) {
                
                // Iterate over entries in the directory
                while ((ent = readdir(dir)) != NULL) {
                    
                    //snprintf(tempMssg, sizeof(tempMssg), "%s\n", ent->d_name);   // Send serponse to client
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                        continue;
                    }
                    snprintf(tempMssg, sizeof(tempMssg), "%s", ent->d_name);
                    if (write(clientFd, tempMssg, sizeof(tempMssg)) < 0) {
                        perror("server write failed");
                        close(clientFd);
                        closedir(dir);
                        exit(EXIT_FAILURE);
                    }
                    memset(tempMssg, 0, sizeof(tempMssg));  // clear the tempMssg
                }  
            } else { 
                perror("");
                return;
            }
            close(clientFd);
            closedir(dir);
        }else if(strcmp(newReq[0], "feadF") == 0){
            
        }else if(strcmp(newReq[0], "quit") == 0){ 
            // Quit request
            // Send request to server
            int clientFd = open(clientReturnFifo, O_WRONLY, 0666);
            if (clientFd < 0) {
                perror("server open fifo failed");
                exit(EXIT_FAILURE);
            }
            strcpy(tempMssg,"Server is closing");
            if (write(clientFd, &tempMssg, sizeof(tempMssg)) < 0) {
                perror("server write failed");
                exit(EXIT_FAILURE);
            }
            strcpy(tempMssg, "");
            close(clientFd);
            // kill current client
            printf(">> \"Client %d\" is disconnected\n", currentClient.connectId);
            --activeCount;
            kill(getpid(), SIGKILL);
            break;
        }else if(strcmp(newReq[0], "killServer") == 0){
            // Kill all clients and server
            // kill clients and child process
            for (int i = 0; i < activeCount; i++) {
                kill(activeClients[i].clientPid, SIGKILL);
            } 
        } 
        // - list; sends a request to display the list of files in Servers directory (also displays the list received from the Server)
        
        memset(tempMssg, 0, sizeof(tempMssg));  // clear the tempMssg
        //Clear request
        for (int j = 0; j < tokenCounter; ++j) {
            //free(newReq[j]);  // Free each token 
            newReq[j] = NULL;
        }
        close(clientFd);
        
    }
    exit(EXIT_SUCCESS);
} 


void initClientLists() {
    activeClients = (Client *)malloc(sizeof(Client) * activeCapacity);
    if (activeClients == NULL) {
        fprintf(stderr, "Failed to allocate memory for active clients\n");
        exit(EXIT_FAILURE);
    }

    waitingQueue = (Client *)malloc(sizeof(Client) * waitingCapacity);
    if (waitingQueue == NULL) {
        fprintf(stderr, "Failed to allocate memory for waiting queue\n");
        exit(EXIT_FAILURE);
    }
}

void addClientToList(Client **list, int *count, int *capacity, Client newClient) {
    if (*count >= *capacity) {
        addClientToWaiting(newClient);
        return;
    }
    (*list)[*count] = newClient;
    (*count)++;
}

void addClientToActive(Client newClient) {
    addClientToList(&activeClients, &activeCount, &activeCapacity, newClient);
}

void addClientToWaiting(Client newClient) {
    addClientToList(&waitingQueue, &waitingCount, &waitingCapacity, newClient);
}

int removeClientFromList(Client *list, int *count, int pid) {
    int found = 0;
    for (int i = 0; i < *count; i++) {
        if (list[i].clientPid == pid) {
            found = 1;
            // Shift elements to fill the gap
            for (int j = i; j < *count - 1; j++) {
                list[j] = list[j + 1];
            }
            (*count)--;
            break;
        }
    }
    return found;
}
Client getLastElementofQUE(Client *list) {
    if (isEmptyQueue(list)) {
        // Handle empty queue case (e.g., print error message, return default value) 
        Client emptyClient; // Initialize with appropriate values if needed
        return emptyClient;
    }
    return list[waitingCount - 1]; // Adjust index based on the queue implementation
}
// Check if a specific queue (activeClients or waitingQueue) is empty
int isEmptyQueue(Client *list) {
    return list == NULL || waitingCount == 0; // Adjust based on the queue you're checking
}

void activateClientFromQueue() {
    if (waitingCount > 0) {
        Client client = waitingQueue[0];
        removeClientFromList(waitingQueue, &waitingCount, client.clientPid);
        addClientToActive(client);
    }
}