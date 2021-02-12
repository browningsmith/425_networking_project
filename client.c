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

    // setting in inputing state
    while(1){
        // clearing the buffer each pass
        bzero(buffer, sizeof(buffer)); 
        printf("enter input: ");
        // taking a line of user input
        scanf("%s", buffer);
        inputSize = strlen(buffer);
        // send the data over to the server TODO


        // if the input is zero close local program after sending
        if(buffer[0] == '0') break;

    }

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