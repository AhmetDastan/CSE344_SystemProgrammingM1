#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include "../client.h" 

char clientFifoName[256];

void handleInterruptSignal(int sig) {
    printf("SIGTSTP received, unlinking FIFO...\n");

    // send quit signal to server for killing the client serverside
    int requestFd = open(clientFifoName, O_WRONLY); // open client fifo for request from server
    if(requestFd < 0){
        perror("In signal client open fifo failed");
        return;
    }
    // Send request to server
    if (write(requestFd, "quit", sizeof("quit")) == -1){
        perror("Server requesting error.");
        return;
    }
    close(requestFd);  
    
    // Unlink the FIFO
    if (unlink(clientFifoName) == -1) { // if already unlinked, it will return -1
        perror("Failed to unlink FIFO");
    }
    printf("killed cleint");
    kill(getpid(), SIGKILL); // kill the client process
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]){

    if(argc != 3){
        perror("client wrong number of arguments");
        return -1;
    }

    char connectReq[11];
    strcpy(connectReq, argv[1]);

    if((strcmp("Connect", connectReq) != 0 && strcmp("tryConnect", connectReq) != 0)){
        perror("Client Argument 2 is wrong \n");
        return -1;
    }

    // Setup the signal handler
    struct sigaction sa;
    sa.sa_handler = handleInterruptSignal;
    sa.sa_flags = 0; // No special flags
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTSTP, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    struct Client client;
    client.clientPid = getpid();
    strcpy(client.request.requestType, connectReq);

    sprintf(clientFifoName, "%d", client.clientPid); 
    // Create named pipe
    if (mkfifo(clientFifoName, 0777) < 0 && errno != EEXIST) {
        perror("server mkfifo failed");
        exit(EXIT_FAILURE);
    } 

    char serverPidReq[10]; 
    strcpy(serverPidReq, argv[2]); 
    
    printf(">> Waiting for Que..");
    while(1){
        //if request is connect or reconnect, connect to server and send pid data of the client  
        if ((strcmp(connectReq, "tryConnect") == 0) || (strcmp(connectReq, "Connect") == 0) ){
            // connect to server
            int serverFd = open(serverPidReq, O_WRONLY);  // try to write server with servers fifo (Main Fifo)
            if(serverFd < 0){
                perror("client open fifo failed");
                return -1;
            }
            if (write(serverFd, &client.clientPid, sizeof(int)) == -1){
                perror("Server requesting error.");
                return -1;
            }else{
                // connection is good
                printf("Connection established:\n");
                close(serverFd);
                client.connectId = 1; 
                break;
            }
            close(serverFd); // client speak with server and close the connection (Connection or reconnection maybe is okay maybe not)
        }
    }

    // wait response from server with unique fifo. Server know the client pid and client fifo name 
    int responseFd;
    int requestFd;
    if(client.connectId == 1){  /// wait for connet response
        responseFd = open(clientFifoName, O_RDONLY); // open client fifo for reading response from server
        if(responseFd < 0){
            perror("client open fifo failed");
            return -1;
        }
        // Read response from server for ensure connection is okay
        if (read(responseFd, &client.response.content, sizeof(client.response.content)) == -1){
            perror("Server requesting error.");
            return -1;
        } 
        client.connectId = 0;
        close(responseFd);
    } 
    
    char request[1024], response[1024];
    while(1){
        // Read request from user
        memset(request, 0, sizeof(request));  // clear the tempMssg
        printf(">> Enter comment: "); 
        fgets(request, sizeof(request), stdin); 
        
        requestFd = open(clientFifoName, O_WRONLY); // open client fifo for request from server
        if(requestFd < 0){
            perror("client open fifo failed");
            return -1;
        } 
        // Send request to server
        if (write(requestFd, &request, sizeof(request)) == -1){
            perror("Server requesting error.");
            return -1;
        }
        close(requestFd); 
        
        // Read response from server
        responseFd = open(clientFifoName, O_RDONLY); // open client fifo for reading response from server
        if(responseFd < 0){
            perror("client open fifo failed");
            return -1;
        } 
        // Read response from server for ensure connection is okay
        while (read(responseFd, &response, sizeof(response)) > 0){
            printf("%s\n", client.response.content);
            memset(client.response.content, 0, sizeof(client.response.content));  // clear the tempMssg
        }
        close(responseFd);
    }
    unlink(clientFifoName);
}