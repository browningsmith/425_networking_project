/*
Authors:    Keith Smith, Sean Callahan
Assignment: Mobile TCP Proxy, Milestone 3
File:       sproxy.c
Class:      425
Due Date:   04/16/2021

Note:       This is the server part of the program. The program takes 1
            command line argument: the port number to listen for
            incoming client connections.

            When sproxy receives a tcp connection on its client socket,
            it it establishes a tcp connection to IP 127.0.0.1 port 23

            The program uses select() to wait for data on either the
            client side or the server side, or both, to be ready to
            send. If data is available from the server side, it
            packages it into an application level packet that is
            then forwarded on to the client socket, that cproxy will
            be able to understand.

            If data is available from the client side, it treats this
            data as a similarly formatted packet, and unpacks the
            payload data and, if necessary, sends it back to the server.

            Every second, the program sends a "heartbeat" packet to the
            client socket: a packet whose only data is the current session
            ID.

            If the data packet received by sproxy is a "heartbeat" packet
            from cproxy, it examines the session ID contained in the packet.
            If the session ID is the same as the previously received sessionID
            sproxy maintains the current connection to the server socket.
            But if the sessionID is new, it closes the server socket and
            establishes a brand new session with the telnet daemon

            If sproxy does not receive any data from the client for over
            3 seconds, it will automatically disconnect the client socket
            and begin accepting new connections, which will either recover
            the original session or start a new session based on the
            incoming session ID from the new client.
*/
#define _DEFAULT_SOURCE // Needed to use timersub on Windows Subsystem for Linux

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LEN 1024
#define LOCALHOST "127.0.0.1"
#define TELNET_PORT 23

typedef enum {
    PACKET_TYPE,
    LENGTH,
    PAYLOAD,
} segmentType;

struct packet {
    // header
    uint32_t type;      // 0 = heartbeat, !0 = data
    uint32_t length;    // length of payload
    // payload
    void* payload;      // either int sessionID or buffer
};

typedef struct LLNode_struct {

    struct LLNode_struct* next;
    struct packet* pck;

} LLNode;

typedef struct {

    LLNode* head;

} LinkedList;

/**************************************************
 * pushTail
 * 
 * Arguments: LinkedList* list, struct packet* pck
 * Returns: void
 * 
 * Creates a new LLNode that points to the
 * given pck, and pushes it on to the end of the
 * given list
 *************************************************/
void pushTail(LinkedList* list, struct packet* pck);

/**************************************************
 * pop
 * 
 * Arguments: LinkedList* list
 * Returns: packet*
 * 
 * Pop's off the head of the linked list and
 * returns the packet contained in it
 * 
 * Returns NULL if the list was empty
 *************************************************/
struct packet* pop(LinkedList* list);

/*************************************
 * max
 * 
 * Arguments: int a,b
 * Returns: int
 * 
 * Returns the maximum of a and b
 *************************************/
int max(int a, int b);

/******************************************
 * compressPacket
 * 
 * Arguments: char* buffer, struct packet
 * Returns: int
 * 
 * Takes the given packet and compresses all
 * the data into a single buffer, which can
 * then be sent later using send()
 * 
 * Returns the number of bytes now stored
 * in buffer
 *****************************************/
int compressPacket(void* buffer, struct packet);

/**********************************************************
 * addToPacket
 * 
 * Arguments: void* buffer, struct packet* pck, int n,
 *            segmentType* currentSegment, int remaining
 * 
 * buffer: Buffer containing data to be added to the
 * packet
 * 
 * This function places data from buffer into the pck packet
 * in the following way: It first reads n bytes from buffer,
 * and copies them in to the memory pointed to by pck->payload
 * + index
 * 
 * index is calulated as the length of the
 * currentSegment - remaining
 * 
 * If the end of a segment is reached during the runtime
 * of this function, it copies the data from pck->payload into
 * the proper segment, and updates currentSegment to be
 * the next expected segment, unless the last segment read
 * was actually the packet payload. The function returns the
 * number of bytes remaining to be read for any segment it
 * does not finish.
 * 
 * If the function returns 0, with currentSegment being set
 * to PACKET_TYPE, this indicates the end of an entire
 * packet was read.
 * 
 * Otherwise, the data added in pck is unreliable, and should
 * be passed to subsequent calls to addToPacket to continue
 * building it out
 *********************************************************/
int addToPacket(void* buffer, struct packet* pck, int n, segmentType* currentSegment, int remaining);

int main(int argc, char** argv)
{
    int sessionID = 0;
    int isNewTelnetSession = 0;
    int pauseDaemonData = 0;
    segmentType segmentExpected = PACKET_TYPE;
    int bytesExpected = sizeof(uint32_t); // Size of packet.type
    int bytesRead = 0;
    
    // Booleans that keep track of which sockets are connected
    int clientConnected = 0; // 0 false, !0 true
    int serverConnected = 0; // 0 false, !0 true

    // Timevals that keep track of next select() timeout, and last message received
    struct timeval timeLastMessageReceived;
    struct timeval nextTimeout;
    
    int listenSocketFD, clientSocketFD, serverSocketFD; // Socket file descriptor
    fd_set socketSet;
    in_port_t listenPort;
    struct sockaddr_in listenAddress, serverAddress;
    struct sockaddr clientAddress;
    socklen_t clientAddressLength;
    void* toClientBuffer = NULL;
    void* fromClientBuffer = NULL;

    // Get port number to listen on from command line
    if (argc < 2)
    {
        printf(
            "ERROR: No port specified!\nUsage: ./sproxy portNumber\n"
        );
        return -1;
    }
    listenPort = atoi(argv[1]);

    // defining the heartbeat packet with session ID
    struct packet heartbeatPacket;
    heartbeatPacket.type = (uint32_t) 0;
    heartbeatPacket.length = (uint32_t) sizeof(int);
    heartbeatPacket.payload = (void*) &sessionID;

    // defining the data packet
    struct packet dataPacket;
    dataPacket.type = (uint32_t) 1;
    dataPacket.length = (uint32_t) 0;
    
    // Attempt to allocate space for dataPacket payload
    dataPacket.payload = malloc(BUFFER_LEN);
    if (dataPacket.payload == NULL)
    {
        perror("Unable to allocate space to store payload of dataPacket");
        return -1;
    }

    // defining the receivedPacket
    struct packet receivedPacket;
    receivedPacket.length = (uint32_t) 0;

    // Attempt to allocate space for receivedPacket payload
    receivedPacket.payload = malloc(BUFFER_LEN);
    if (receivedPacket.payload == NULL)
    {
        perror("Unable to allocate space to store payload of receivedPacket");
        return -1;
    }

    // Attempt to allocate space for toClientBuffer
    toClientBuffer = malloc(2*sizeof(uint32_t) + BUFFER_LEN);
    if (toClientBuffer == NULL)
    {
        perror("Unable to allocate space for the toClientBuffer");
        return -1;
    }

    // Attempt to allocate space for fromClientBuffer
    fromClientBuffer = malloc(BUFFER_LEN);
    if (fromClientBuffer == NULL)
    {
        perror("Unable to allocate space for the toClientBuffer");
        return -1;
    }

    // Create listen socket
    listenSocketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocketFD < 0) // socket returns -1 on error
    {
        perror("sproxy unable to create listen socket");
        return -1;
    }

    // Bind listen socket to port
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = INADDR_ANY;
    listenAddress.sin_port = htons(listenPort);

    // bind returns -1 on error
    if (bind(listenSocketFD, (struct sockaddr*) &listenAddress, sizeof(listenAddress)) < 0)
    {
        perror("sproxy unable to bind listen socket to port");
        return -1;
    }

    // set to listen to incoming connections
    if (listen(listenSocketFD, 5) < 0) // listen returns -1 on error
    {
        perror("sproxy unable to listen to port");
        return -1;
    }

    // populate info for telnet daemon into serverAddress
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(LOCALHOST);
    serverAddress.sin_port = htons(TELNET_PORT);

    // Infinite loop, continue to listen for new connections
    while (1)
    {
        if (clientConnected == 0)
        {
            printf("client is not connected. Connecting...\n");
            
            // accept a new client
            printf("sproxy waiting for new connection...\n");
            clientAddressLength = sizeof(struct sockaddr); // Should solve INVALID ARGUMENT error
            clientSocketFD = accept(listenSocketFD, &clientAddress, &clientAddressLength);
            if (clientSocketFD < 0) // accept returns -1 on error
            {
                perror("sproxy unable to receive connection from client.");
                continue; // Repeat loop to receive another client connection
            }
            else
            {
                clientConnected = 1;
                pauseDaemonData = 1;
                gettimeofday(&timeLastMessageReceived, NULL);
                printf("sproxy accepted new connection from client!\n");
            }
        }

        if (serverConnected == 0)
        {
            printf("server is not connected. Connecting...\n");
            
            // Create server socket
            serverSocketFD = socket(AF_INET, SOCK_STREAM, 0);
            if (serverSocketFD < 0) // socket returns -1 on error
            {
                perror("sproxy unable to create server socket. Trying again in one second");

                struct timeval oneSec = {
                    .tv_sec = 1,
                    .tv_usec = 0
                };
                select(0, NULL, NULL, NULL, &oneSec);

                continue; // move to attempt to reconnect to telnet daemon
            }

            // Connect to server
            printf("sproxy attempting to connect to %s %i...\n", LOCALHOST, htons(serverAddress.sin_port));
            if (connect(serverSocketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0)
            {
                perror("sproxy unable to connect to telnet daemon. Trying again in one second");

                // Close server socket so we don't have a TOO MANY OPEN FILES error
                if (close(serverSocketFD) < 0)
                {
                    perror("sproxy unable to properly close server socket");
                }

                struct timeval oneSec = {
                    .tv_sec = 1,
                    .tv_usec = 0
                };

                select(0, NULL, NULL, NULL, &oneSec);

                continue; // move to attempt to reconnect to telnet daemon
            }

            serverConnected = 1;
            isNewTelnetSession = 1;
            pauseDaemonData = 0;
            printf("sproxy successfully connected to telnet daemon!\n");
        }

        if ((serverConnected != 0) && (clientConnected != 0))
        {
            printf("Client and Server are both connected\n");
            
            // Reset segmentExpected to PACKET_TYPE and bytesExpected to sizeof(uint32_t)
            segmentExpected = PACKET_TYPE;
            bytesExpected = sizeof(uint32_t);
            
            // Set nextTimeout to current time to ensure the first message sent is a heartbeat
            gettimeofday(&nextTimeout, NULL);

            // Use select for data to be ready on both serverSocket and clientSocket
            while (1)
            {   
                // Reset socketSet
                FD_ZERO(&socketSet); // zero out socketSet
                FD_SET(serverSocketFD, &socketSet); // add server socket
                FD_SET(clientSocketFD, &socketSet); // add client socket

                // Calculate new timeout value (passing a returned timeout value from a previous select call does not work on all OSs)
                struct timeval currentTime;
                gettimeofday(&currentTime, NULL);
                struct timeval timeout;
                timersub(&nextTimeout, &currentTime, &timeout);
                if (timeout.tv_sec < 0) // If it came back negative, set to zero
                {
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 0;
                }

                // Wait up to one second for input to be available using select
                int resultOfSelect = select(
                        max(serverSocketFD, clientSocketFD) + 1,
                        &socketSet,
                        NULL,
                        NULL,
                        &timeout
                    );
                // If select timed out, check for a heartbeat from cproxy, and also send one
                if(resultOfSelect == 0 )
                {
                    struct timeval newTime;
                    gettimeofday(&newTime, NULL);
                    struct timeval timeDif;
                    timersub(&newTime, &timeLastMessageReceived, &timeDif); // getting the time difference
                    if(timeDif.tv_sec >= 3) // if the time difference is 3 or greater
                    {
                        //TODO: close the sockets between cproxy and sproxy
                        if (close(clientSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close client socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to client\n");
                        }
                        clientConnected = 0;

                        break;
                    }

                    // Add a second to nextTimeout
                    nextTimeout.tv_sec += 1;

                    // Compress and send heartbeat packet
                    int bytesToSend = compressPacket(toClientBuffer, heartbeatPacket);
                    int bytesSent = send(clientSocketFD, toClientBuffer, bytesToSend, 0);

                    // Report if there was an error (just for debugging, no need to exit)
                    if (bytesSent < 0)
                    {
                        perror("Unable to send heartbeat message to cproxy");
                    }
                    else
                    {
                        printf("Sent heartbeat to cproxy\n");
                    }
                }
                // If there was an error with select, this is non recoverable
                else if (resultOfSelect < 0)
                {
                    perror("FATAL: sproxy unable to use select to wait for input");

                    // Close server socket, so it doesn't stay open
                    if (close(serverSocketFD)) // close returns -1 on error
                    {
                        perror("sproxy unable to properly close server socket");
                    }
                    else
                    {
                        printf("sproxy closed connection to server\n");
                    }

                    // Close client socket, so it doesn't stay open
                    if (close(clientSocketFD)) // close returns -1 on error
                    {
                        perror("sproxy unable to properly close client socket");
                    }
                    else
                    {
                        printf("sproxy closed connection to client\n");
                    }

                    return -1;
                }

                // If input is ready on clientSocket, place data into receivedPacket
                if (FD_ISSET(clientSocketFD, &socketSet))
                {   
                    // Update timeLastMessageReceived
                    gettimeofday(&timeLastMessageReceived, NULL);
                    
                    // Read bytesExpected into fromClientBuffer
                    bytesRead = recv(clientSocketFD, fromClientBuffer, bytesExpected, 0);

                    // If bytesRead is 0 or -1, controlled disconnect, disconnect both sockets and
                    // break into outer while loop
                    if (bytesRead <= 0)
                    {
                        printf("recv() returned with %i on clientSocketFD\n", bytesRead);

                        // Close server socket
                        if (close(serverSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close server socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to server\n");
                        }
                        serverConnected = 0;

                        // Close client socket
                        if (close(clientSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close client socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to client\n");
                        }
                        clientConnected = 0;
                        
                        break;
                    }

                    // add data to packet
                    bytesExpected = addToPacket(fromClientBuffer, &receivedPacket, bytesRead, &segmentExpected, bytesExpected);

                    // If bytesExpected is 0, we just finished reading a whole packet
                    if (bytesExpected == 0)
                    {           
                        // Update bytesExpected to sizeof(uint32_t)
                        bytesExpected = sizeof(uint32_t);

                        // If the packet is a data packet, send the payload to server
                        if (receivedPacket.type != 0)
                        {
                            int bytesSent = send(serverSocketFD, receivedPacket.payload, receivedPacket.length, 0);

                            // Report if there was an error (just for debugging, no need to exit)
                            if (bytesSent < 0)
                            {
                                perror("Unable to send data to cproxy");
                            }
                        }
                        // If the packet is a heartbeat packet, check if new session ID matches the current session ID
                        else
                        {
                            printf("Heartbeat received from cproxy\n");

                            int newID = *(int*) receivedPacket.payload;

                            if (newID != sessionID)
                            {
                                printf("Client has new sessionID\n");

                                sessionID = newID;

                                if (isNewTelnetSession != 0)
                                {
                                    printf("This is already a brand new telnet daemon session, no need to start a new one\n");

                                    isNewTelnetSession = 0;
                                    pauseDaemonData = 0;
                                }
                                else
                                {
                                    printf("Closing current connection to telnet daemon\n");
                                    
                                    if (close(serverSocketFD) < 0)
                                    {
                                        perror("sproxy unable to properly close server socket");
                                    }
                                    serverConnected = 0;

                                    printf("Closed connection to telnet daemon\n");
                                }
                            }
                            else
                            {
                                printf("Client has old sessionID, maintaining current telnet session\n");
                                pauseDaemonData = 0;
                            }
                        }
                    }
                }

                // Break in to outer while loop if server was disconnected
                if (serverConnected == 0)
                {
                    break;
                }

                // If input is ready on serverSocket, construct a packet and send to client socket
                if (FD_ISSET(serverSocketFD, &socketSet))
                {   
                    // If it is indicated that daemon data should be paused, don't do anything
                    if (pauseDaemonData != 0)
                    {
                        continue;
                    }
                    
                    int serverBytesRead = recv(serverSocketFD, dataPacket.payload, BUFFER_LEN, 0);
                    // If bytesRead is 0 or -1, controlled disconnect, disconnect both sockets and
                    // break into outer while loop
                    if (serverBytesRead <= 0)
                    {
                        printf("recv() returned with %i on serverSocketFD\n", serverBytesRead);
                        
                        // Close server socket
                        if (close(serverSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close server socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to server\n");
                        }
                        serverConnected = 0;

                        // Close client socket
                        if (close(clientSocketFD)) // close returns -1 on error
                        {
                            perror("sproxy unable to properly close client socket");
                        }
                        else
                        {
                            printf("sproxy closed connection to client\n");
                        }
                        clientConnected = 0;

                        break;
                    }

                    // Create packet and send to clientSocketFD
                    dataPacket.length = serverBytesRead;
                    int bytesToSend = compressPacket(toClientBuffer, dataPacket);
                    int bytesSent = send(clientSocketFD, toClientBuffer, bytesToSend, 0);
                    // Report if there was an error (just for debugging, no need to exit)
                    if (bytesSent < 0)
                    {
                        perror("Unable to send data to cproxy");
                    }
                }
            }
        }
    }

    printf("Error! Outer while loop was broken!!!!\n");

    // Close listen socket
    if (close(listenSocketFD)) // close returns -1 on error
    {
        perror("sproxy unable to close listen socket");
    }

    // free buffers
    free(dataPacket.payload);
    free(receivedPacket.payload);
    free(toClientBuffer);
    free(fromClientBuffer);

    return 0;
}

void pushTail(LinkedList* list, struct packet* pck)
{
    LLNode* newNode = malloc(sizeof(LLNode));
    if (newNode == NULL)
    {
        perror("Unable to allocate space for new linked list node");
        exit(-1);
    }

    // Populate node
    newNode->pck = pck;
    newNode->next = NULL;

    // If the list is empty, insert at the head
    if (list->head == NULL)
    {
        list->head = newNode;
        return;
    }

    // Insert at the end of the list
    LLNode* lastNode = list->head;
    while (lastNode->next != NULL)
    {
        lastNode = lastNode->next;
    }

    lastNode->next = newNode;
}

struct packet* pop(LinkedList* list)
{
    if (list->head == NULL)
    {
        return NULL;
    }

    LLNode* poppedNode = list->head;
    list->head = poppedNode->next;

    struct packet* pck = poppedNode->pck;
    free(poppedNode);

    return pck;
}

int max(int a, int b)
{
    if (a > b)
    {
        return a;
    }

    return b;
}

int compressPacket(void* buffer, struct packet pck)
{
    int index = 0;

    // Write in packet type
    *(uint32_t*) (buffer+index) = pck.type;
    index += sizeof(uint32_t);

    // Write in payload length
    *(uint32_t*) (buffer+index) = pck.length;
    index += sizeof(uint32_t);

    // Write in the payload
    memcpy(buffer+index, pck.payload, pck.length);
    index += pck.length;

    return index; // This should now equal the size of the data in buffer
}

int addToPacket(void* buffer, struct packet* pck, int n, segmentType* currentSegment, int remaining)
{
    int bufferIndex = 0;
    
    // As long as there are still bytes to read
    while (n > 0)
    {
        // Calculate payloadIndex
        int payloadIndex;
        if ((*currentSegment == PACKET_TYPE) || (*currentSegment == LENGTH))
        {
            payloadIndex = sizeof(uint32_t) - remaining;
        }
        else
        {
            payloadIndex = pck->length - remaining;
        }

        // Check that payloadIndex is not negative
        if (payloadIndex < 0)
        {
            printf("Error in addToPacket: the given \"remaining\" argument is too high: %i\n", remaining);
            return remaining;
        }
        
        // If there are more bytes remaining than n, copy n bytes and return
        if (remaining > n)
        {
            memcpy(pck->payload + payloadIndex, buffer + bufferIndex, n);
            return remaining - n;
        }
        else
        {
            // Copy remaining bytes
            memcpy(pck->payload + payloadIndex, buffer + bufferIndex, remaining);
            bufferIndex += remaining;
            n -= remaining;

            // Update segment type
            switch (*currentSegment)
            {
                case PACKET_TYPE:

                    // Copy packet type data into pck->type
                    pck->type = *(uint32_t*) pck->payload;

                    // Change currentSegment to LENGTH
                    *currentSegment = LENGTH;

                    // Update remaining to sizeof(uint32_t)
                    remaining = sizeof(uint32_t);

                    break;
                case LENGTH:

                    // Copy packet length data into pck->length
                    pck->length = *(uint32_t*) pck->payload;

                    // Change currentSegment to PAYLOAD
                    *currentSegment = PAYLOAD;

                    // Update remaining to pck->length
                    remaining = pck->length;

                    break;
                case PAYLOAD:
                    
                    // pck->payload should contain the correct payload now

                    // Change currentSegment to PACKET_TYPE
                    *currentSegment = PACKET_TYPE;

                    return 0;
            }
        }
    }

    return remaining;
}