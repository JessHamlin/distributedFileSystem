/* 
 * dfc.c - A simple client used with distributed file servers
 * usage: ./dfc <configuration file>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <openssl/md5.h>
#include <errno.h>
#include <math.h>

#define BUFSIZE 1024
#define PKTSIZE 1024
#define STRSIZE 250
extern int errno;

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

// Send a part of a file, returns -1 on error
int sendFile(FILE* fp, int sockfd, int start, int stop, char* un, char* pw, int chunk, char* fname, char* d) {
    char buf[BUFSIZE];
    int n;
    int toSend = stop - start;

    bzero(buf, BUFSIZE);
    // Send a header type thing
    strcpy(buf, "put ");
    sprintf(buf + 4, "%d", toSend); //size
    strcat(buf, " ");
    strcat(buf, un);
    strcat(buf, " ");
    strcat(buf, pw);
    strcat(buf, " ");
    sprintf(buf + strlen(buf), "%d", chunk); // chunk 1, 2, 3, or 4
    strcat(buf, " ");
    strcat(buf, fname);
    strcat(buf, " ");
    strcat(buf, d);
    n = send(sockfd, buf, strlen(buf), MSG_NOSIGNAL);
    if (n <= 0) {
        printf("Error: Could not send\n");
        return -1;
    }
    // See if username and password was right
    bzero(buf, BUFSIZE);
    n = read(sockfd, buf, BUFSIZE);
    if (n <= 0) {
        printf("Error: Could not read\n");
        return -1;
    }
    if (strcmp(buf, "OK") != 0) {
        printf("Error: Incorrect username or password for a server, or dir does not exist\n");
        return -2;
    }

    // Send file
    fseek(fp, start, SEEK_SET);
    int xorVal = 0; // Do super simple encryption
    for (int i = 0; i < strlen(pw); i++) {
        xorVal = (xorVal + pw[i]) % 256;
    }
    while(toSend > BUFSIZE) {
        bzero(buf, BUFSIZE);

        fread(buf, sizeof(char), BUFSIZE, fp);
        for (int i = 0; i < BUFSIZE; i++) {
            buf[i] = buf[i] ^ xorVal;
        }

        n = send(sockfd, buf, BUFSIZE, 0);
        if (n <= 0) {
            printf("Error: Could not send\n");
            return -1;
        }

        toSend -= BUFSIZE;
    }
    if (toSend > 0) {
        bzero(buf, BUFSIZE);

        fread(buf, sizeof(char), toSend, fp);
        for (int i = 0; i < toSend; i++) {
            buf[i] = buf[i] ^ xorVal;
        }

        n = send(sockfd, buf, toSend, 0);
        if (n <= 0) {
            printf("Error: Could not send\n");
            return -1;
        }
    }

    return 0;
}

// Get the hash mod 4 of a file, returns -1 on error
// Source: https://stackoverflow.com/questions/10324611/how-to-calculate-the-md5-hash-of-a-large-file-in-c
int getHash(char* fname) {
    unsigned char c[MD5_DIGEST_LENGTH];
    FILE *fp = fopen(fname, "rb");
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[BUFSIZE];
    int ret = 0;

    if (fp == NULL) {
        printf ("Error: %s can't be opened.\n", fname);
        return -1;
    }

    MD5_Init(&mdContext);
    while ((bytes = fread(data, 1, BUFSIZE, fp)) != 0) {
        MD5_Update(&mdContext, data, bytes);
    }
    MD5_Final(c, &mdContext);
    fclose (fp);

    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ret = ret + c[i];
    }

    return ret % 4;
}

int main(int argc, char **argv) {
    FILE *fp;
    char buf[BUFSIZE];
    char rep[BUFSIZE];
    char extra[BUFSIZE];
    char input[BUFSIZE];
    char fname[STRSIZE];
    char* tmpStr = NULL;
    size_t tmpStrLen = BUFSIZE;
    int firstFlag = 1;
    int n;
    int hash;
    int fsize;
    int fileParts[4];
    int sockets[4];

    // For getting from servers
    int fnaIndex = 0;
    int fnaNums[100][4];
    char fnaNames[100][STRSIZE];

    // Info for servers
    char username[STRSIZE];
    char password[STRSIZE];
    char hostname1[STRSIZE];
    int portno1 = -1;
    char hostname2[STRSIZE];
    int portno2 = -1;
    char hostname3[STRSIZE];
    int portno3 = -1;
    char hostname4[STRSIZE];
    int portno4 = -1;
    int status[] = {1, 1, 1, 1};

    // For connection stuff
    int sockfd1, sockfd2, sockfd3, sockfd4;
    int serverlen1;
    struct sockaddr_in serveraddr1;
    struct hostent *server1;
    int serverlen2;
    struct sockaddr_in serveraddr2;
    struct hostent *server2;
    int serverlen3;
    struct sockaddr_in serveraddr3;
    struct hostent *server3;
    int serverlen4;
    struct sockaddr_in serveraddr4;
    struct hostent *server4;
    struct timeval timeout;      
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    // Zero buffers
    bzero(hostname1, STRSIZE);
    bzero(hostname2, STRSIZE);
    bzero(hostname3, STRSIZE);
    bzero(hostname4, STRSIZE);
    bzero(username, STRSIZE);
    bzero(password, STRSIZE);
    bzero(buf, BUFSIZE);
    bzero(rep, BUFSIZE);
    bzero(input, BUFSIZE);
    bzero(extra, BUFSIZE);

    /* check command line arguments */
    if (argc != 2) {
       fprintf(stderr,"usage: %s <Configuration File>\n", argv[0]);
       return 1;
    }

    // Read configuration file
    // TODO: Due to time constraints this is not robust, should use regex in future
    fp = fopen(argv[1], "r");
    if (fp == NULL) {
        printf("Error: Cannot find configuration file\n");
        return 1;
    }
    while(getline(&tmpStr, &tmpStrLen, fp) != -1) {
        //printf("%s\n", tmpStr); // DEBUG
        if (tmpStr[0] == 'S') {
            if (portno1 == -1) {
                strncpy(hostname1, tmpStr + 12, 9);
                portno1 = atoi(tmpStr + 22);
            }
            else if (portno2 == -1) {
                strncpy(hostname2, tmpStr + 12, 9);
                portno2 = atoi(tmpStr + 22);
            }
            else if (portno3 == -1) {
                strncpy(hostname3, tmpStr + 12, 9);
                portno3 = atoi(tmpStr + 22);
            }
            else {
                strncpy(hostname4, tmpStr + 12, 9);
                portno4 = atoi(tmpStr + 22);
            }
        }
        else if (tmpStr[0] == 'U') {
            strncpy(username, tmpStr + 10, STRSIZE);
            username[strlen(username)-1] = 0x00;
        }
        else if (tmpStr[0] == 'P') {
            strncpy(password, tmpStr + 10, STRSIZE);
            password[strlen(password)-1] = 0x00;
        }
    }
    fclose(fp);
    if (tmpStr) {
        free(tmpStr);
    }

    // Make sure file was read correctly
    // printf("%s, %d\n", hostname1, portno1); // DEBUG
    // printf("%s, %d\n", hostname2, portno2); // DEBUG
    // printf("%s, %d\n", hostname3, portno3); // DEBUG
    // printf("%s, %d\n", hostname4, portno4); // DEBUG
    // printf("%s, %s\n", username, password); // DEBUG

    // Connect to hosts
    /* socket: create the socket */
    sockfd1 = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd1 < 0) 
        error("ERROR opening socket1");
    sockfd2 = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd2 < 0) 
        error("ERROR opening socket2");
    sockfd3 = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd3 < 0) 
        error("ERROR opening socket3");
    sockfd4 = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd4 < 0) 
        error("ERROR opening socket4");
    // Set timeout values (try to send or receive for 1 second)
    setsockopt(sockfd1, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(sockfd1, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    setsockopt(sockfd2, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(sockfd2, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    setsockopt(sockfd3, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(sockfd3, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    setsockopt(sockfd4, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(sockfd4, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

    /* gethostbyname: get the server's DNS entry */
    server1 = gethostbyname(hostname1);
    if (server1 == NULL) {
        printf("ERROR: No such host as %s\n", hostname1);
        return 1;
    }
    server2 = gethostbyname(hostname2);
    if (server2 == NULL) {
        printf("ERROR: No such host as %s\n", hostname2);
        return 1;
    }
    server3 = gethostbyname(hostname3);
    if (server3 == NULL) {
        printf("ERROR: No such host as %s\n", hostname3);
        return 1;
    }
    server4 = gethostbyname(hostname4);
    if (server4 == NULL) {
        printf("ERROR: No such host as %s\n", hostname4);
        return 1;
    }

    /* build the servers' Internet address */
    bzero((char *) &serveraddr1, sizeof(serveraddr1));
    serveraddr1.sin_family = AF_INET;
    bcopy((char *)server1->h_addr, (char *)&serveraddr1.sin_addr.s_addr, server1->h_length);
    serveraddr1.sin_port = htons(portno1);
    serverlen1 = sizeof(serveraddr1);

    bzero((char *) &serveraddr2, sizeof(serveraddr2));
    serveraddr2.sin_family = AF_INET;
    bcopy((char *)server2->h_addr, (char *)&serveraddr2.sin_addr.s_addr, server2->h_length);
    serveraddr2.sin_port = htons(portno2);
    serverlen2 = sizeof(serveraddr2);

    bzero((char *) &serveraddr3, sizeof(serveraddr3));
    serveraddr3.sin_family = AF_INET;
    bcopy((char *)server3->h_addr, (char *)&serveraddr3.sin_addr.s_addr, server3->h_length);
    serveraddr3.sin_port = htons(portno3);
    serverlen3 = sizeof(serveraddr3);

    bzero((char *) &serveraddr4, sizeof(serveraddr4));
    serveraddr4.sin_family = AF_INET;
    bcopy((char *)server4->h_addr, (char *)&serveraddr4.sin_addr.s_addr, server4->h_length);
    serveraddr4.sin_port = htons(portno4);
    serverlen4 = sizeof(serveraddr4);

    // Connect to servers
    if (connect(sockfd1, (struct sockaddr *)&serveraddr1, serverlen1) < 0) {
        // Try again in 1 second
        sleep(1);
        if (connect(sockfd1, (struct sockaddr *)&serveraddr1, serverlen1) < 0) {
            printf("Error: Could not connect to server 1\n");
            status[0] = 0;
        }
    }
    if (connect(sockfd2, (struct sockaddr *)&serveraddr2, serverlen2) < 0) {
        // Try again in 1 second
        sleep(1);
        if (connect(sockfd2, (struct sockaddr *)&serveraddr2, serverlen2) < 0) {
            printf("Error: Could not connect to server 2\n");
            status[1] = 0;
        }
    }
    if (connect(sockfd3, (struct sockaddr *)&serveraddr3, serverlen3) < 0) {
        // Try again in 1 second
        sleep(1);
        if (connect(sockfd3, (struct sockaddr *)&serveraddr3, serverlen3) < 0) {
            printf("Error: Could not connect to server 3\n");
            status[2] = 0;
        }
    }
    if (connect(sockfd4, (struct sockaddr *)&serveraddr4, serverlen4) < 0) {
        // Try again in 1 second
        sleep(1);
        if (connect(sockfd4, (struct sockaddr *)&serveraddr4, serverlen4) < 0) {
            printf("Error: Could not connect to server 4\n");
            status[3] = 0;
        }
    }

    // Fill socket array
    sockets[0] = sockfd1;
    sockets[1] = sockfd2;
    sockets[2] = sockfd3;
    sockets[3] = sockfd4;

    // Start main loop
    while (1) {
        // Reset variables
        bzero(buf, BUFSIZE);
        bzero(rep, BUFSIZE);
        bzero(input, BUFSIZE);
        bzero(fname, STRSIZE);
        bzero(extra, BUFSIZE);
        for (int i = 0; i < 100; i++) {
            bzero(fnaNames[i], STRSIZE);
            fnaNums[i][0] = 0;
            fnaNums[i][1] = 0;
            fnaNums[i][2] = 0;
            fnaNums[i][3] = 0;
        }
        fnaIndex = 0;

        // Get user input
        if (firstFlag == 0) {
            printf("\n");
        }
        firstFlag = 0;
        char* statStr[] = {"Off", "On"};
        printf("Server Status: Server1:%s Server2:%s Server3:%s Server4:%s\n",
            statStr[status[0]], statStr[status[1]], statStr[status[2]],statStr[status[3]]);
        printf("Please enter command or help for a list of commands:\n");
        fgets(input, BUFSIZE, stdin);

        // Case 1: help
        if (strcmp(input, "help\n") == 0) {
            printf( 
                "Commands:\n"
                "   get [file_name]     -Get a file from the servers (replaces existing files)\n"
                "   put [file_name]     -Store a local file on the servers (replaces existing files)\n"
                "   list                -List all files on the servers\n"
                "   mkdir [dir]/        -Make a directory on the servers\n"
                "   exit                -Close this client\n"
            );
        }
        // Case 2: exit
        else if (strcmp(input, "exit\n") == 0) {
            if (status[0] == 1) {
                send(sockfd1, "exit", 5, 0);
            }
            if (status[1] == 1) {
                send(sockfd2, "exit", 5, 0);
            }
            if (status[2] == 1) {
                send(sockfd3, "exit", 5, 0);
            }
            if (status[3] == 1) {
                send(sockfd4, "exit", 5, 0);
            }
            printf("Goodbye\n");
            break;
        }
        // Case 3: list
        else if (strcmp(input, "list\n") == 0) {
            strcpy(buf, "list ");
            strcat(buf, username);
            strcat(buf, " ");
            strcat(buf, password);

            for (int i = 0; i < 4; i++) {
                if (status[i] == 0) {
                    continue;
                }

                // Send list request
                n = send(sockets[i], buf, BUFSIZE, 0);
                if (n <= 0) {
                    printf("Error: Could not send to server%d\n", i + 1);
                    continue;
                }

                // See if password worked
                bzero(rep, BUFSIZE);
                n = read(sockets[i], rep, BUFSIZE);
                if (n <= 0) {
                    printf("Error: Could not read from server%d\n", i + 1);
                    status[i] = 0;
                    continue;
                }
                if (strcmp(rep, "OK") != 0) {
                    printf("Error: Incorrect username or password for server%d\n", i + 1);
                    continue; 
                }
                // Read file names
                bzero(rep, BUFSIZE);
                n = read(sockets[i], rep, BUFSIZE);
                if (n <= 0) {
                    printf("Error: Could not read from server%d\n", i + 1);
                    status[i] = 0;
                    continue;
                }
                if (strcmp(rep, "nothing") == 0) {
                    continue;
                }
                //printf("rep: %s\n", rep); // DEBUG

                // Init file name list
                strcpy(extra, "Folders available on at least one server:\n");

                // Get list of file names
                tmpStr = strtok(rep, " ");
                int found;
                int num;
                while(tmpStr != NULL) {
                    if (tmpStr[0] != '.') {
                        strcat(extra, tmpStr);
                        strcat(extra, "\n");
                        tmpStr = strtok(NULL, " ");
                        continue;
                    }
                    found = 0;
                    // Clean tmpStr
                    tmpStr = tmpStr + 1;
                    num = atoi(tmpStr + strlen(tmpStr) - 1);
                    tmpStr[strlen(tmpStr) - 2] = 0x00;
                    // Check if in the file name array
                    for (int j = 0; j < fnaIndex; j++) {
                        if (strcmp(fnaNames[j], tmpStr) == 0) {
                            fnaNums[j][num - 1] = 1;
                            found = 1;
                            break;
                        }
                    }
                    // Add to the array
                    if (found == 0) {
                        strcpy(fnaNames[fnaIndex], tmpStr);
                        fnaNums[fnaIndex][num - 1] = 1;
                        if (fnaIndex < 99) {
                            fnaIndex += 1;
                        }
                    }
                    tmpStr = strtok(NULL, " ");
                }
            }
            // Print names
            printf("Available files:\n");
            for (int i = 0; i < fnaIndex; i++) {
                if (fnaNums[i][0] == 1 && fnaNums[i][1] == 1 && 
                    fnaNums[i][2] == 1 && fnaNums[i][3] == 1) {

                    printf("%s\n", fnaNames[i]);
                }
                else {
                    printf("%s [incomplete]\n", fnaNames[i]);
                }
            }
            // Print directories
            //extra[strlen(extra) - 1] = 0x00;
            printf("%s", extra);
        }
        // Case 4: put
        else if (strncmp(input, "put ", 4) == 0) {
            // Dont allow if servers are down
            n = 0;
            for (int i = 0; i < 4; i++) {
                if (status[i] == 0) {
                    n += 1;
                }
            }
            if (n == 2 || n == 3) {
                printf("Error: Too many servers are down store full file\n");
            }
            else if (n == 1) {
                printf("Warning: One server is down, but file can still be stored\n");
            }
            else if (n == 4) {
                printf("Error: All servers are down, no part of file stored\n");
                continue;
            }

            // Get MD5 hash of the file
            input[strlen(input)-1] = 0x00;
            tmpStr = strtok(input, " ");
            tmpStr = strtok(NULL, " ");
            strcpy(fname, tmpStr);
            tmpStr = strtok(NULL, " ");
            char d[STRSIZE]; bzero(d, STRSIZE);
            if (tmpStr != NULL) {
                strcpy(d, "/");
                strcat(d, tmpStr);
            }
            else {
                strcpy(d, "/");
            }
            //printf("%s\n", fname); // DEBUG
            if(access(fname, F_OK) != 0) {
                printf("Error: File does not exist\n");
                continue;
            }
            hash = getHash(fname);
            printf("Hash: %d\n", hash); // DEBUG

            // Get length of file
            fp = fopen(fname, "rb");
            fseek(fp, 0L, SEEK_END);
            fsize = ftell(fp);
            fseek(fp, 0L, SEEK_SET);
            if (fsize < 4) {
                printf("Error: File too small\n");
                continue;
            }

            // Divide file into pieces
            fileParts[3] = fsize;
            fileParts[2] = (fsize / 4) * 3;
            fileParts[1] = (fsize / 4) * 2;
            fileParts[0] = (fsize / 4) * 1;
            //printf("%d, %d, %d, %d\n", 
            //    fileParts[0], fileParts[1], fileParts[2], fileParts[3]); // DEBUG

            // Send file
            if (hash == 0) {
                if (sendFile(fp, sockfd1, 0, fileParts[0], 
                    username, password, 1, fname, d) == -1) {status[0] = 0; continue;}
                if (sendFile(fp, sockfd1, fileParts[0], fileParts[1], 
                    username, password, 2, fname, d) == -1) {status[0] = 0; continue;}
                if (sendFile(fp, sockfd2, fileParts[0], fileParts[1], 
                    username, password, 2, fname, d) == -1) {status[1] = 0; continue;}
                if (sendFile(fp, sockfd2, fileParts[1], fileParts[2], 
                    username, password, 3, fname, d) == -1) {status[1] = 0; continue;}
                if (sendFile(fp, sockfd3, fileParts[1], fileParts[2], 
                    username, password, 3, fname, d) == -1) {status[2] = 0; continue;}
                if (sendFile(fp, sockfd3, fileParts[2], fileParts[3], 
                    username, password, 4, fname, d) == -1) {status[2] = 0; continue;}
                if (sendFile(fp, sockfd4, fileParts[2], fileParts[3], 
                    username, password, 4, fname, d) == -1) {status[3] = 0; continue;}
                if (sendFile(fp, sockfd4, 0, fileParts[0], 
                    username, password, 1, fname, d) == -1) {status[3] = 0; continue;}
            }
            else if (hash == 1) {
                if (sendFile(fp, sockfd2, 0, fileParts[0], 
                    username, password, 1, fname, d) == -1) {status[1] = 0; continue;}
                if (sendFile(fp, sockfd2, fileParts[0], fileParts[1], 
                    username, password, 2, fname, d) == -1) {status[1] = 0; continue;}
                if (sendFile(fp, sockfd3, fileParts[0], fileParts[1], 
                    username, password, 2, fname, d) == -1) {status[2] = 0; continue;}
                if (sendFile(fp, sockfd3, fileParts[1], fileParts[2], 
                    username, password, 3, fname, d) == -1) {status[2] = 0; continue;}
                if (sendFile(fp, sockfd4, fileParts[1], fileParts[2], 
                    username, password, 3, fname, d) == -1) {status[3] = 0; continue;}
                if (sendFile(fp, sockfd4, fileParts[2], fileParts[3], 
                    username, password, 4, fname, d) == -1) {status[3] = 0; continue;}
                if (sendFile(fp, sockfd1, fileParts[2], fileParts[3], 
                    username, password, 4, fname, d) == -1) {status[0] = 0; continue;}
                if (sendFile(fp, sockfd1, 0, fileParts[0], 
                    username, password, 1, fname, d) == -1) {status[0] = 0; continue;}
            }
            else if (hash == 2) {
                if (sendFile(fp, sockfd3, 0, fileParts[0], 
                    username, password, 1, fname, d) == -1) {status[2] = 0; continue;}
                if (sendFile(fp, sockfd3, fileParts[0], fileParts[1], 
                    username, password, 2, fname, d) == -1) {status[2] = 0; continue;}
                if (sendFile(fp, sockfd4, fileParts[0], fileParts[1], 
                    username, password, 2, fname, d) == -1) {status[3] = 0; continue;}
                if (sendFile(fp, sockfd4, fileParts[1], fileParts[2], 
                    username, password, 3, fname, d) == -1) {status[3] = 0; continue;}
                if (sendFile(fp, sockfd1, fileParts[1], fileParts[2], 
                    username, password, 3, fname, d) == -1) {status[0] = 0; continue;}
                if (sendFile(fp, sockfd1, fileParts[2], fileParts[3], 
                    username, password, 4, fname, d) == -1) {status[0] = 0; continue;}
                if (sendFile(fp, sockfd2, fileParts[2], fileParts[3], 
                    username, password, 4, fname, d) == -1) {status[1] = 0; continue;}
                if (sendFile(fp, sockfd2, 0, fileParts[0], 
                    username, password, 1, fname, d) == -1) {status[1] = 0; continue;}
            }
            else {
                if (sendFile(fp, sockfd4, 0, fileParts[0], 
                    username, password, 1, fname, d) == -1) {status[3] = 0;}
                if (sendFile(fp, sockfd4, fileParts[0], fileParts[1], 
                    username, password, 2, fname, d) == -1) {status[3] = 0;}
                if (sendFile(fp, sockfd1, fileParts[0], fileParts[1], 
                    username, password, 2, fname, d) == -1) {status[0] = 0; continue;}
                if (sendFile(fp, sockfd1, fileParts[1], fileParts[2], 
                    username, password, 3, fname, d) == -1) {status[0] = 0; continue;}
                if (sendFile(fp, sockfd2, fileParts[1], fileParts[2], 
                    username, password, 3, fname, d) == -1) {status[1] = 0; continue;}
                if (sendFile(fp, sockfd2, fileParts[2], fileParts[3], 
                    username, password, 4, fname, d) == -1) {status[1] = 0; continue;}
                if (sendFile(fp, sockfd3, fileParts[2], fileParts[3], 
                    username, password, 4, fname, d) == -1) {status[2] = 0; continue;}
                if (sendFile(fp, sockfd3, 0, fileParts[0], 
                    username, password, 1, fname, d) == -1) {status[2] = 0; continue;}
            }

            fclose(fp);
            printf ("File sent\n");
        }
        // Case 5: get
        else if (strncmp(input, "get ", 4) == 0) {
            // Get file name
            input[strlen(input)-1] = 0x00;
            tmpStr = strtok(input, " ");
            tmpStr = strtok(NULL, " ");
            strcpy(fname, tmpStr);
            tmpStr = strtok(NULL, " ");
            char d[STRSIZE]; bzero(d, STRSIZE);
            if (tmpStr != NULL) {
                strcpy(d, "/");
                strcat(d, tmpStr);
            }
            else {
                strcpy(d, "/");
            }
            int numDown = 0;
            
            // Loop through servers
            for (int i = 0; i < 4; i++) {
                if (status[i] == 0) {
                    numDown += 1;
                    continue;
                }
                else if (numDown > 1) {
                    break;
                }
                else if (fnaNums[0][0] == 1 && fnaNums[0][1] == 1 &&
                    fnaNums[0][2] == 1 && fnaNums[0][3] == 1) {

                    break;
                }

                // Make request including files already gotten
                bzero(buf, BUFSIZE);
                strcpy(buf, "get ");
                strcat(buf, username);
                strcat(buf, " ");
                strcat(buf, password);
                strcat(buf, " ");
                strcat(buf, fname);
                strcat(buf, " ");
                strcat(buf, d);
                if (fnaNums[0][0] == 0) {
                    strcat(buf, " 1");
                }
                if (fnaNums[0][1] == 0) {
                    strcat(buf, " 2");
                }
                if (fnaNums[0][2] == 0) {
                    strcat(buf, " 3");
                }
                if (fnaNums[0][3] == 0) {
                    strcat(buf, " 4");
                }

                // Send request for file
                n = send(sockets[i], buf, strlen(buf), 0);
                if (n <= 0) {
                    printf("Error: Could not send to server%d\n", i + 1);
                    continue;
                }

                // See if password worked
                bzero(buf, BUFSIZE);
                n = read(sockets[i], buf, STRSIZE);
                if (n <= 0) {
                    printf("Error: Could not read from server%d\n", i + 1);
                    status[i] = 0;
                    continue;
                }
                if (strncmp(buf, "OK", 2) != 0) {
                    printf("Error: Incorrect username or password for server or dir does not exist%d\n", i + 1);
                    continue; 
                }

                // Log who has what
                strtok(buf, " ");
                tmpStr = strtok(NULL, " ");
                int num;
                while (tmpStr != NULL) {
                    num = atoi(tmpStr) - 1;
                    fnaNums[0][num] = 1;
                    fnaNums[1][num] = i;
                    //memcpy(&fnaNums[1][atoi(tmpStr) - 1], &i, sizeof(int));
                    tmpStr = strtok(NULL, " ");
                }
                //printf("%d, %d, %d, %d\n", fnaNums[0][0], fnaNums[0][1], fnaNums[0][2], fnaNums[0][3]);
                //printf("%d, %d, %d, %d\n", fnaNums[1][0], fnaNums[1][1], fnaNums[1][2], fnaNums[1][3]);
            }

            //printf("%d, %d, %d, %d\n", fnaNums[0][0], fnaNums[0][1], fnaNums[0][2], fnaNums[0][3]);
            //printf("%d, %d, %d, %d\n", fnaNums[1][0], fnaNums[1][1], fnaNums[1][2], fnaNums[1][3]);

            // Handle case where no files can be read
            if (numDown > 1 || 
                (fnaNums[0][0] == 0 || fnaNums[0][1] == 0 || 
                fnaNums[0][2] == 0 || fnaNums[0][3] == 0)) {

                printf("Error: File is incomplete or does not exist\n");
                continue;
            }

            // Get the file
            fp = fopen(fname, "wb");
            int err = 0;
            int xorVal = 0; // Do super simple encryption
            for (int i = 0; i < strlen(password); i++) {
                xorVal = (xorVal + password[i]) % 256;
            }
            for (int i = 0; i < 4; i++) {
                // Send request
                bzero(buf, BUFSIZE);
                strcpy(buf, "get2 ");
                sprintf(buf + strlen(buf), "%d ", i + 1);
                strcat(buf, fname);
                strcat(buf, " ");
                strcat(buf, username);
                strcat(buf, " ");
                strcat(buf, d);

                n = send(sockets[fnaNums[1][i]], buf, strlen(buf), 0);
                if (n <= 0) {
                    printf("Error: Could not send to server%d\n", fnaNums[1][i] + 1);
                    err = 1;
                    break;
                }
                // Get size
                bzero(buf, BUFSIZE);
                n = read(sockets[fnaNums[1][i]], buf, STRSIZE);
                if (n <= 0) {
                    printf("Error: Could not read from server%d\n", fnaNums[1][i] + 1);
                    status[fnaNums[1][i]] = 0;
                    err = 1;
                    break;
                }
                fsize = atoi(buf);
                //printf("size: %d, from %d\n", fsize, fnaNums[1][i]);

                // Get file
                while (fsize > BUFSIZE) {
                    bzero(buf, BUFSIZE);

                    n = read(sockets[fnaNums[1][i]], buf, BUFSIZE);
                    if (n <= 0) {
                        printf("Error: Read failed\n");
                        status[fnaNums[1][i]] = 0;
                        err = 1;
                        break;
                    }

                    for (int i = 0; i < BUFSIZE; i++) {
                        buf[i] = buf[i] ^ xorVal;
                    }

                    fwrite(buf, sizeof(char), BUFSIZE, fp);

                    fsize -= BUFSIZE;
                }
                if (err == 1) {
                    break;
                }
                if (fsize > 0) {
                    //printf("size2: %d\n", fsize);
                    bzero(buf, BUFSIZE);

                    n = read(sockets[fnaNums[1][i]], buf, fsize);
                    if (n <= 0) {
                        printf("Error: Read failed\n");
                        status[fnaNums[1][i]] = 0;
                        err = 1;
                        break;
                    }

                    for (int i = 0; i < fsize; i++) {
                        buf[i] = buf[i] ^ xorVal;
                    }

                    fwrite(buf, sizeof(char), fsize, fp);
                }
            }
            fclose(fp);

            if (err == 1) {
                printf("A server failed while downloading your file, please retry\n");
            }
            else {
                printf("File downloaded\n");
            }
        }
        // Case 6: mkdir
        else if (strncmp(input, "mkdir ", 6) == 0) {
            strcpy(buf, "mkdir ");
            strcat(buf, username);
            strcat(buf, " ");
            strcat(buf, password);
            strcat(buf, " ");
            strcat(buf, input + 6);
            buf[strlen(buf) - 1] = 0x00;

            // Send buf to each server
            for (int i = 0; i < 4; i++) {
                if (status[i] == 0) {
                    continue;
                }

                // Send list request
                n = send(sockets[i], buf, strlen(buf), 0);
                if (n <= 0) {
                    printf("Error: Could not send to server%d\n", i + 1);
                    continue;
                }

                // See if password worked
                bzero(rep, BUFSIZE);
                n = read(sockets[i], rep, STRSIZE);
                if (n <= 0) {
                    printf("Error: Could not read from server%d\n", i + 1);
                    status[i] = 0;
                    continue;
                }
                if (strcmp(rep, "OK") != 0) {
                    printf("Error: Incorrect username or password for server%d\n", i + 1);
                    continue; 
                }
                else {
                    printf("Directory made successfully on server%d\n", i + 1);
                }
            }
        }
        // Case 7: Unknown
        else {
            printf("Unknown command\n");
        }
    }

    return 0;
}
