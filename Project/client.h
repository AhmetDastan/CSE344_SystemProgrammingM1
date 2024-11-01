#ifndef COMMUNICATION
#define COMMUNICATION
 

#define bufferSize 1024 


// Define request struct type
struct Request{
    char requestType[bufferSize]; // request type is Connect or tryConnect
    char content[bufferSize]; // request content is pid of the client
};

struct Response{
    char content[bufferSize];
};

typedef struct Client{
    int clientPid;
    int clientFd;  
    int connectId; // 0 not connected, otherwise connected
    struct Request request;
    struct Response response;
}Client; 

#endif