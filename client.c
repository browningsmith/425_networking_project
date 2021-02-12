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