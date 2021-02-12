#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#define MAX_BUFFER 1024

int main(int argc, char** argv)
{
    char buffer[MAX_BUFFER];
    uint32_t inputSize;
    int socketFD; // Socket file descriptor
    int port;
    int ip;

    // get port number and IP from command line
    if (argc < 2)
    {
        printf(
            "ERROR: No port specified!\n"
            "Usage: ./server portNumber\n"
        );
        return -1;
    }
    ip = atoi(argv[1]);
    port = atoi(argv[2]);

    // Create socket
    socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0) // socket returns -1 on error
    {
        perror("Client unable to create socket");
        return -1;
    }
    else
    {
        printf("Client created socket.\n");
    }

    // connect to a remote server on a certain IP and port

    // reading user input till ctrl+d
    while(scanf("%s", buffer) != EOF){
        inputSize = strlen(buffer);
        // send the data over to the server TODO

    }

    // user terminated the program so send 0 to the server and self close

    // Close socket
    if (close(socketFD)) // close returns -1 on error
    {
        perror("Client unable to close socket");
    }
    else
    {
        printf("Client closed socket.\n");
    }


    return 0;
}