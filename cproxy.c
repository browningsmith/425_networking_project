/*
Authors:    Keith Smith, Sean Callahan
Assignment: Mobile TCP Proxy, Milestone 3
file:       cproxy.c
Class:      425
Due Date:   04/16/2021

Note:       This is the client part of the program. The program takes 3
            command line arguments: lport (the port to listen for an
            incoming connection), and sip and sport (the ip and port of
            the sproxy program).

            When cproxy receives a tcp connection on its client socket,
            it generates a unique session ID, and then establishes
            a tcp connection with the device on the provided sport
            and sip.

            The program uses select() to wait for data on either the
            client side or the server side, or both, to be ready to
            send. If data is available from the client side, it
            packages it into an application level packet that is
            then forwarded on to the server socket, that sproxy will
            be able to understand.

            If data is available from the server side, it treats this
            data as a similarly formatted packet, and unpacks the
            payload data and sends it back to the client.

            Every second, the program sends a "heartbeat" packet to the
            server socket: a packet whose only data is the current session
            ID. If it is a new session ID that sproxy did not have before,
            a new telnet daemon session will be established by sproxy,
            otherwise the current session is maintained.

            If cproxy does not receive any data from the server for over
            3 seconds, it will automatically disconnect the server socket
            and attempt to reconnect once every second to try to recover 
            the session.
            
*/
#define _DEFAULT_SOURCE // Needed to use timersub on Windows Subsystem for Linux

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LEN 1024

typedef enum {

    PACKET_TYPE,
    SEQ,
    ACK,
    LENGTH,
    PAYLOAD,

} segmentType;

struct packet {
    // header
    uint32_t type;      // 0 = heartbeat, !0 = data
    uint32_t seqN;      // Sequence number
    uint32_t ackN;      // Ack number (the seqN of the next expected packet)
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
 * generateID
 * 
 * Arguments: int oldID
 * Returns: int
 * 
 * Generates a new session ID using rand()
 * but ensures that it is not the same as
 * the old session ID
 *****************************************/
int generateID(int oldID);

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
    in_port_t listenPort, serverPort;
    struct sockaddr_in listenAddress, serverAddress;
    struct sockaddr clientAddress;
    socklen_t clientAddressLength;
    void* toServerBuffer = NULL;
    void* fromServerBuffer = NULL;

    // Get listenPort and serverPort from command line
    if (argc < 4)
    {
        printf(
            "ERROR: You must enter the port to listen for new connections on,\n"
            "       as well as the address and port number to forward data to\n"
            "Usage: ./cproxy lport sip sport\n"
        );
        return -1;
    }
    listenPort = atoi(argv[1]);
    serverPort = atoi(argv[3]);
    
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

    // Attempt to allocate space for toServerBuffer
    toServerBuffer = malloc(4*sizeof(uint32_t) + BUFFER_LEN);
    if (toServerBuffer == NULL)
    {
        perror("Unable to allocate space for the toServerBuffer");
        return -1;
    }

    // Attempt to allocate space for fromServerBuffer
    fromServerBuffer = malloc(BUFFER_LEN);
    if (fromServerBuffer == NULL)
    {
        perror("Unable to allocate space for the fromServerBuffer");
        return -1;
    }

    // Seed RNG to help ensure that two different cproxy sessions don't start with the same sessionID
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    srand(currentTime.tv_usec);

    // Create listen socket
    listenSocketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocketFD < 0) // socket returns -1 on error
    {
        perror("cproxy unable to create listen socket");
        return -1;
    }

    // Bind listen socket to port
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = INADDR_ANY;
    listenAddress.sin_port = htons(listenPort);

    // bind returns -1 on error
    if (bind(listenSocketFD, (struct sockaddr*) &listenAddress, sizeof(listenAddress)) < 0)
    {
        perror("cproxy unable to bind listen socket to port");
        return -1;
    }

    // set to listen to incoming connections
    if (listen(listenSocketFD, 5) < 0) // listen returns -1 on error
    {
        perror("cproxy unable to listen to port");
        return -1;
    }

    // populate address info for connection to server
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(argv[2]);
    serverAddress.sin_port = htons(serverPort);

    // Infinite loop, continue to listen for new connections
    while (1)
    {
        if (clientConnected == 0)
        {
            printf("client is not connected. Connecting...\n");
            
            // accept a new client
            printf("cproxy waiting for new connection...\n");
            clientAddressLength = sizeof(struct sockaddr); // Should solve INVALID ARGUMENT error
            clientSocketFD = accept(listenSocketFD, &clientAddress, &clientAddressLength);
            if (clientSocketFD < 0) // accept returns -1 on error
            {
                perror("cproxy unable to receive connection from client");
                continue; // Repeat loop to receive another client connection
            }
            else
            {
                clientConnected = 1;
                sessionID = generateID(sessionID);
                printf("cproxy accepted new connection from client!\n");
            }
        }

        if (serverConnected == 0)
        {
            // Attempt to re-establish connection
            printf("server is not connected. Connecting...\n");
            
            // Create new server socket
            serverSocketFD = socket(AF_INET, SOCK_STREAM, 0);
            if (serverSocketFD < 0) // socket returns -1 on error
            {
                perror("cproxy unable to create server socket. Trying again in one second");

                struct timeval oneSec = {
                    .tv_sec = 1,
                    .tv_usec = 0
                };
                select(0, NULL, NULL, NULL, &oneSec);

                continue; // Repeat loop to attempt a new connection
            }

            // Attempt to connect to server
            printf("cproxy attempting to connect to %s %i\n", argv[2], htons(serverAddress.sin_port));
            if (connect(serverSocketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0)
            {
                perror("cproxy unable to connect to server. Trying again in one second");

            // close server socket to avoid TOO MANY OPEN FILES error
            if (close(serverSocketFD) < 0)
            {
                perror("cproxy unable to properly close server socket");
            }

            struct timeval oneSec = {
                .tv_sec = 1,
                .tv_usec = 0
            };
		    select(0, NULL, NULL, NULL, &oneSec);

                continue; // Repeat loop to attempt a new connection
            }

            serverConnected = 1;
            gettimeofday(&timeLastMessageReceived, NULL);
            printf("cproxy successfully connected to server!\n");
        }

        if ((serverConnected != 0) && (clientConnected != 0))
        {
            printf("Client and Server are both connected\n");
            
            // Reset segmentExpected to PACKET_TYPE and bytesExpected to sizeof(uint32_t)
            segmentExpected = PACKET_TYPE;
            bytesExpected = sizeof(uint32_t);

            // Set nextTimeout to currentTime to ensure the first message sent is a heartbeat
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

                // Wait at most one second for input to be available using select
                int resultOfSelect = select(
                    max(serverSocketFD, clientSocketFD) + 1,
                    &socketSet,
                    NULL,
                    NULL,
                    &timeout
                );
                // If select timed out, check for a heartbeat from sproxy, and also send one
                if (resultOfSelect == 0)
                {
                    struct timeval newTime;
                    gettimeofday(&newTime, NULL);
                    struct timeval timeDif;
                    timersub(&newTime, &timeLastMessageReceived, &timeDif); // getting the time difference
                    if(timeDif.tv_sec >= 3) // if the time difference is 3 or greater
                    {
                        //TODO: close the sockets between cproxy and sproxy
                        if (close(serverSocketFD)) // close returns -1 on error
                        {
                            perror("cproxy unable to properly close server socket");
                        }
                        else
                        {
                            printf("cproxy closed connection to server\n");
                        }
                        serverConnected = 0;

                        break;
                    }
                    
                    // Add a second to nextTimeout
                    nextTimeout.tv_sec += 1;

                    // Compress and send heartbeat packet
                    int bytesToSend = compressPacket(toServerBuffer, heartbeatPacket);
                    int bytesSent = send(serverSocketFD, toServerBuffer, bytesToSend, 0);

                    // Report if there was an error (just for debugging, no need to exit)
                    if (bytesSent < 0)
                    {
                        perror("Unable to send heartbeat message to sproxy");
                    }
                    else
                    {
                        printf("Sent heartbeat to sproxy\n");
                    }
                }
                // If there was an error with select, this is non recoverable
                else if (resultOfSelect < 0)
                {
                    perror("FATAL: cproxy unable to use select to wait for input");

                    // Close server socket, so it doesn't stay open
                    if (close(serverSocketFD)) // close returns -1 on error
                    {
                        perror("cproxy unable to properly close server socket");
                    }
                    else
                    {
                        printf("cproxy closed connection to server\n");
                    }

                    // Close client socket, so it doesn't stay open
                    if (close(clientSocketFD)) // close returns -1 on error
                    {
                        perror("cproxy unable to properly close client socket");
                    }
                    else
                    {
                        printf("cproxy closed connection to client\n");
                    }

                    return -1;
                }

                // If input is ready on serverSocket, place data into receivedPacket
                if (FD_ISSET(serverSocketFD, &socketSet))
                {   
                    // Update timeLastMessageReceived
                    gettimeofday(&timeLastMessageReceived, NULL);
                    
                    // Read bytesExpected into fromServerBuffer
                    bytesRead = recv(serverSocketFD, fromServerBuffer, bytesExpected, 0);

                    // If bytesRead is 0 or -1, controlled disconnect, disconnect both sockets and
                    // break into outer while loop
                    if (bytesRead <= 0)
                    {
                        printf("recv() returned with %i on serverSocketFD\n", bytesRead);

                        // Close server socket
                        if (close(serverSocketFD)) // close returns -1 on error
                        {
                            perror("cproxy unable to properly close server socket");
                        }
                        else
                        {
                            printf("cproxy closed connection to server\n");
                        }
                        serverConnected = 0;

                        // Close client socket
                        if (close(clientSocketFD)) // close returns -1 on error
                        {
                            perror("cproxy unable to properly close client socket");
                        }
                        else
                        {
                            printf("cproxy closed connection to client\n");
                        }
                        clientConnected = 0;

                        break;
                    }

                    // Add data to packet
                    bytesExpected = addToPacket(fromServerBuffer, &receivedPacket, bytesRead, &segmentExpected, bytesExpected);

                    // If bytesExpected is 0, we just finished reading a whole packet
                    if (bytesExpected == 0)
                    {
                        // Update bytesExpected to sizeof(uint32_t)
                        bytesExpected = sizeof(uint32_t);

                        // If the packet is a data packet, send the payload to client
                        if (receivedPacket.type != 0)
                        {
                            int bytesSent = send(clientSocketFD, receivedPacket.payload, receivedPacket.length, 0);

                            // Report if there was an error (just for debugging, no need to exit)
                            if (bytesSent < 0)
                            {
                                perror("Unable to send data to telnet");
                            }
                        }
                        else
                        {
                            printf("Heartbeat received from sproxy\n");
                        }
                        
                    }
                }

                // If input is ready on clientSocket, construct packet and send to serverSocket
                if (FD_ISSET(clientSocketFD, &socketSet))
                {   
                    int clientBytesRead = recv(clientSocketFD, dataPacket.payload, BUFFER_LEN, 0);
                    // If bytesRead is 0 or -1, controlled disconnect, disconnect both sockets and
                    // break into outer while loop
                    if (clientBytesRead <= 0)
                    {
                        printf("recv() returned with %i on clientSocketFD\n", clientBytesRead);
                        
                        // Close server socket
                        if (close(serverSocketFD)) // close returns -1 on error
                        {
                            perror("cproxy unable to properly close server socket");
                        }
                        else
                        {
                            printf("cproxy closed connection to server\n");
                        }
                        serverConnected = 0;

                        // Close client socket
                        if (close(clientSocketFD)) // close returns -1 on error
                        {
                            perror("cproxy unable to properly close client socket");
                        }
                        else
                        {
                            printf("cproxy closed connection to client\n");
                        }
                        clientConnected = 0;

                        break;
                    }

                    // Create packet and send to serverSocketFD
                    dataPacket.length = clientBytesRead;
                    int bytesToSend = compressPacket(toServerBuffer, dataPacket);
                    int bytesSent = send(serverSocketFD, toServerBuffer, bytesToSend, 0);
                    // Report if there was an error (just for debugging, no need to exit)
                    if (bytesSent < 0)
                    {
                        perror("Unable to send data to sproxy");
                    }
                }
            }
        }
    }

    printf("Error! Outer while loop was broken!!!!\n");

    // Close listen socket
    if (close(listenSocketFD)) // close returns -1 on error
    {
        perror("cproxy unable to close listen socket");
    }

    // free buffers
    free(dataPacket.payload);
    free(receivedPacket.payload);
    free(toServerBuffer);
    free(fromServerBuffer);

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

int generateID(int oldID)
{
    int newID;
    
    do
    {
        newID = rand();

    } while (newID == oldID);

    return newID;
}

int compressPacket(void* buffer, struct packet pck)
{
    int index = 0;

    // Write in packet type
    *(uint32_t*) (buffer+index) = pck.type;
    index += sizeof(uint32_t);

    // Write in seqN
    *(uint32_t*) (buffer+index) = pck.seqN;
    index += sizeof(uint32_t);

    // Write in ackN
    *(uint32_t*) (buffer+index) = pck.ackN;
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

                    // Change currentSegment to SEQ
                    *currentSegment = SEQ;

                    // Update remaining to sizeof(uint32_t)
                    remaining = sizeof(uint32_t);

                    break;
                case SEQ:

                    // Copy packet seqN data into pck->seqN
                    pck->seqN = *(uint32_t*) pck->payload;

                    // Change currentSegment to ACK
                    *currentSegment = ACK;

                    // Update remaining to sizeof(uint32_t)
                    remaining = sizeof(uint32_t);

                    break;
                case ACK:

                    // Copy packet ackN data into pck->ackN
                    pck->ackN = *(uint32_t*) pck->payload;

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


