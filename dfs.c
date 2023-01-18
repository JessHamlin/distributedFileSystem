/* 
 * dfs.c - Distributed file server system
 * usage: ./dfs <Folder To Use> <Port To Use>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* for fgets */
#include <strings.h>     /* for bzero, bcopy */
#include <unistd.h>      /* for read, write */
#include <sys/socket.h>  /* for socket use */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>        /* for poll */
#include <time.h>        /* for time */
#include <openssl/md5.h> /* for md5 */
#include <sys/stat.h>
#include <dirent.h>

#define BUFSIZE  1024  /* max I/O buffer size */
#define LISTENQ  1024  /* second argument to listen() */
#define STRSIZE  250   /* max size for filenames and miscellaneous */

int open_listenfd(int port);
void respond(int connfd);
void *thread(void *vargp);

char directory[STRSIZE];

int main(int argc, char **argv)
{
    int listenfd, *connfdp, port, clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid; 
    struct stat stats;

    // Make sure arguments are correct
    if (argc != 3) {
        fprintf(stderr, "usage: %s <Directory> <Port>\n", argv[0]);
        exit(0);
    }
    // Get port number
    port = atoi(argv[2]);
    // Get directory
    bzero(directory, STRSIZE);
    strcpy(directory, ".");
    strcat(directory, argv[1]);

    // Check if directory exists
    stat(directory, &stats);
    if (S_ISDIR(stats.st_mode) == 0) {
        printf("Error: Directory %s does not exist\n", directory);
        return 1;
    } 

    // Listen on the port and make a new thread if anything is heard
    listenfd = open_listenfd(port);
    while (1) {
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, (unsigned int*)&clientlen);
        pthread_create(&tid, NULL, thread, connfdp);
    }
}

/* thread routine */
void * thread(void * vargp) 
{  
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self()); 
    free(vargp);
    respond(connfd);
    close(connfd);
    return NULL;
}

// Check if username and password exist, returns 0 if it does
int checkAuth(char* username, char* password) {
    FILE *fp;
    char fileName[STRSIZE];
    char* line = NULL;
    char* tmpStr = NULL;
    size_t lineLen = BUFSIZE;
    char tmp[BUFSIZE];
    int ret = 1;

    // Zero buffers
    bzero(tmp, BUFSIZE);
    bzero(fileName, STRSIZE);

    strcpy(fileName, directory);
    strcat(fileName, "/dfs.conf");
    //printf("File Name: %s\n", fileName); // DEBUG
    fp = fopen(fileName, "r");
    if (fp == NULL) {
        printf("Error: Cannot find configuration file\n");
        return 1;
    }
    // Loop through usernames and passwords
    while(getline(&line, &lineLen, fp) != -1) {
        if (line[0] != '/') {
            bzero(tmp, BUFSIZE);
            strcpy(tmp, line);
            tmpStr = strtok(tmp, " ");
            //printf("Given UN: %s, Testing: %s\n", username, tmpStr); // DEBUG
            if (strcmp(username, tmpStr) == 0) {
                tmpStr = strtok(NULL, " ");
                tmpStr[strlen(tmpStr)-1] = 0x00;
                //printf("Given PW: %s, Testing: %s\n", password, tmpStr); // DEBUG
                if (strcmp(password, tmpStr) == 0) {
                    ret = 0;
                    break;
                }
            }
        }
    }
    fclose(fp);
    if (line) {
        free(line);
    }
    return ret;
}

/*
 * respond
 */
void respond(int connfd) 
{
    FILE *fp;
    int n;
    char buf[BUFSIZE];
    char username[STRSIZE];
    char password[STRSIZE];
    char tmp[BUFSIZE];
    char* tmpStr;
    int fsize;
    int endFlag = 0;
    struct stat stats;
    char userDir[STRSIZE];
    char chunk[STRSIZE];
    char fname[STRSIZE];
    struct dirent *de;
    int needed[4];

    while(endFlag == 0) {
        // Zero buffers
        bzero(buf, BUFSIZE);
        bzero(tmp, BUFSIZE);
        bzero(username, STRSIZE);
        bzero(password, STRSIZE);
        bzero(userDir, STRSIZE);
        bzero(chunk, STRSIZE);
        bzero(fname, STRSIZE);
        needed[0] = 0;
        needed[1] = 0;
        needed[2] = 0;
        needed[3] = 0;

        // Get message
        n = read(connfd, buf, BUFSIZE);
        if (n <= 0) {
            printf("Error: Read failed\n");
            return;
        }
        printf("Received: %s\n", buf); // DEBUG

        // Case 1: list
        if (strncmp(buf, "list", 4) == 0) {
            // Get username and password
            strncpy(tmp, buf, BUFSIZE);
            strtok(tmp, " ");
            tmpStr = strtok(NULL, " ");
            strcpy(username, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(password, tmpStr);
            //printf("UN, PW: %s, %s\n", username, password); // DEBUG
            // Verify Password
            if (checkAuth(username, password) != 0) {
                send(connfd, "Bad Authentication", 19, 0);
                continue;
            }
            send(connfd, "OK", 3, 0);

            // Get files
            bzero(buf, BUFSIZE);
            strcpy(userDir, directory);
            strcat(userDir, "/");
            strcat(userDir, username);
            DIR *dr = opendir(userDir);
            if (dr == NULL) { 
                printf("Error: Could not open a directory\n");
                continue;
            }
            while ((de = readdir(dr)) != NULL) {
                //printf("%s, %d\n", de->d_name, de->d_type); // DEBUG
                if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
                    strcat(buf, de->d_name);
                    strcat(buf, " ");
                }
            }
            closedir(dr);
            if (strlen(buf) == 0) {
                strcpy(buf, "nothing");
            }
            printf("Sending: %s, Size: %ld\n", buf, strlen(buf)); // DEBUG
            send(connfd, buf, strlen(buf), 0);
        }
        // Case 2: put
        else if (strncmp(buf, "put ", 4) == 0) {
            // Get username and password
            strncpy(tmp, buf, BUFSIZE);
            strtok(tmp, " ");
            tmpStr = strtok(NULL, " ");
            fsize = atoi(tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(username, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(password, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(chunk, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(fname, tmpStr);
            tmpStr = strtok(NULL, " ");
            char d[STRSIZE]; bzero(d, STRSIZE);
            strcpy(d, tmpStr);
            if (checkAuth(username, password) != 0) {
                send(connfd, "Bad Authentication", 19, 0);
                continue;
            }

            // Make directory
            strcpy(userDir, directory);
            strcat(userDir, "/");
            strcat(userDir, username);
            stat(userDir, &stats);
            if (S_ISDIR(stats.st_mode) == 0) {
                //Dir does not exist
                mkdir(userDir, 0777);
            }

            // Check directory
            strcat(userDir, d);
            stat(userDir, &stats);
            if (S_ISDIR(stats.st_mode) == 0) {
                send(connfd, "Bad Authentication", 19, 0);
                continue;
            }
            send(connfd, "OK", 3, 0);

            // Put file
            strcat(userDir, ".");
            strcat(userDir, fname);
            strcat(userDir, ".");
            strcat(userDir, chunk);
            printf("%s\n", userDir);
            fp = fopen(userDir, "wb");
            while (fsize > BUFSIZE) {
                bzero(buf, BUFSIZE);

                n = read(connfd, buf, BUFSIZE);
                if (n <= 0) {
                    printf("Error: Read failed\n");
                    continue;
                }

                fwrite(buf, sizeof(char), BUFSIZE, fp);

                fsize -= BUFSIZE;
            }
            if (fsize > 0) {
                bzero(buf, BUFSIZE);

                n = read(connfd, buf, fsize);
                if (n <= 0) {
                    printf("Error: Read failed\n");
                    continue;
                }

                fwrite(buf, sizeof(char), fsize, fp);
            }
            fclose(fp);
        }
        // Case 3: get
        else if (strncmp(buf, "get ", 4) == 0) {
            // Get username and password
            strncpy(tmp, buf, BUFSIZE);
            strtok(tmp, " ");
            tmpStr = strtok(NULL, " ");
            strcpy(username, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(password, tmpStr);

            // Get other values
            tmpStr = strtok(NULL, " ");
            strcpy(fname, tmpStr);

            tmpStr = strtok(NULL, " ");
            char d[STRSIZE]; bzero(d, STRSIZE);
            strcpy(d, tmpStr);

            tmpStr = strtok(NULL, " ");
            while (tmpStr != NULL) {
                n = atoi(tmpStr);
                needed[n - 1] = 1;
                tmpStr = strtok(NULL, " ");
            }

            // Verify Password
            if (checkAuth(username, password) != 0) {
                send(connfd, "Bad Authentication", 19, 0);
                continue;
            }

            // See if we have a needed
            strcpy(userDir, directory);
            strcat(userDir, "/");
            strcat(userDir, username);
            strcat(userDir, d);
            printf("%s\n", userDir);
            DIR *dr = opendir(userDir);
            if (dr == NULL) { 
                printf("Error: Could not open a directory\n");
                continue;
            }
            while ((de = readdir(dr)) != NULL) {
                if (de->d_type == 8) {
                    strcpy(chunk, de->d_name);
                    //printf("c: %s, %d\n", chunk, chunk[strlen(chunk) - 1] - '0'); // DEBUG
                    if (strncmp(chunk + 1, fname, strlen(chunk) - 3) == 0) {
                        if (needed[chunk[strlen(chunk) - 1] - '0' -1] == 1) {
                            // We have a needed
                            needed[chunk[strlen(chunk) - 1] - '0' -1] = 2;
                        }
                    }
                }
            }
            closedir(dr);
            // Send header
            bzero(buf, BUFSIZE);
            strcpy(buf, "OK");
            for (int i = 0; i < 4; i++) {
                if (needed[i] == 2) {
                    strcat(buf, " ");
                    sprintf(buf + strlen(buf), "%d", i + 1);
                }
            }
            printf("Sending: %s, Size: %ld\n", buf, strlen(buf)); // DEBUG
            send(connfd, buf, strlen(buf), 0); 
        }
        // Case 4: get2
        else if (strncmp(buf, "get2 ", 5) == 0) {
            // Read request
            strtok(buf, " ");
            tmpStr = strtok(NULL, " ");
            strcpy(chunk, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(fname, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(username, tmpStr);

            tmpStr = strtok(NULL, " ");
            char d[STRSIZE]; bzero(d, STRSIZE);
            strcpy(d, tmpStr);

            // Get directory and filename
            strcpy(userDir, directory);
            strcat(userDir, "/");
            strcat(userDir, username);
            strcat(userDir, d);
            strcat(userDir, ".");
            strcat(userDir, fname);
            strcat(userDir, ".");
            strcat(userDir, chunk);

            // Open file
            fp = fopen(userDir, "rb");
            // Get size
            fseek(fp, 0L, SEEK_END);
            fsize = ftell(fp);
            fseek(fp, 0L, SEEK_SET);
            // Send size
            bzero(buf, BUFSIZE);
            sprintf(buf, "%d", fsize);
            n = send(connfd, buf, strlen(buf), 0);
            if (n <= 0) {
                printf("Error: Could not send\n");
            }
            // Send file
            //printf("size: %d\n", fsize);
            while (fsize > BUFSIZE) {
                bzero(buf, BUFSIZE);

                fread(buf, sizeof(char), BUFSIZE, fp);
                n = send(connfd, buf, BUFSIZE, 0);
                if (n <= 0) {
                    printf("Error: Could not send\n");
                }

                fsize -= BUFSIZE;
            }
            if (fsize > 0) {
                //printf("size2: %d\n", fsize);
                bzero(buf, BUFSIZE);

                fread(buf, sizeof(char), fsize, fp);

                n = send(connfd, buf, fsize, 0);
                if (n <= 0) {
                    printf("Error: Could not send\n");
                }
            }

            fclose(fp);

        }
        // Case 5: mkdir
        else if (strncmp(buf, "mkdir ", 6) == 0) {
            // Get username and password
            strncpy(tmp, buf, BUFSIZE);
            strtok(tmp, " ");
            tmpStr = strtok(NULL, " ");
            strcpy(username, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(password, tmpStr);
            tmpStr = strtok(NULL, " ");
            strcpy(chunk, tmpStr);
            // Verify Password
            if (checkAuth(username, password) != 0) {
                send(connfd, "Bad Authentication", 19, 0);
                continue;
            }
            send(connfd, "OK", 3, 0);

            // Make username directory if needed
            strcpy(userDir, directory);
            strcat(userDir, "/");
            strcat(userDir, username);
            stat(userDir, &stats);
            if (S_ISDIR(stats.st_mode) == 0) {
                //Dir does not exist
                mkdir(userDir, 0777);
            }

            // Make the dir
            strcat(userDir, "/");
            strcat(userDir, chunk);
            printf("Making dir: %s", userDir);
            mkdir(userDir, 0777);
        }
        // Case 6: exit
        else if (strncmp(buf, "exit", 4) == 0) {
            endFlag = 1;
        }
    }
}

/* 
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure 
 */
int open_listenfd(int port) 
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval, sizeof(int)) < 0)
        return -1;

    /* listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
} /* end open_listenfd */

