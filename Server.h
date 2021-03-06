#ifndef __SERVER_H__
#define __SERVER_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include "Constant.h"
#include "sfwrite.h"
#include "Wrapper.h"
#include "Database.h"
#include "User.h"

sqlite3 *db;
UserList userList;

extern pthread_rwlock_t RW_lock;
extern pthread_mutex_t Q_lock;

extern int verboseFlag;
int runFlag;

int numThread;

fd_set communicationSet;
int maxConnfd;
int numCommunication;

char motd[MAX_LEN];

typedef struct nameSet {
	char names[MAX_DUMMY_LEN][MAX_NAME_LEN];
	int count;
} NameSet;

NameSet *nameSet;

typedef struct loginQueue {
	int *connfds;
	int numThread;
	int frontConnfd;
	int rearConnfd;
	sem_t mutex;
	sem_t slots;
	sem_t items;
} LoginQueue;

LoginQueue *loginQueue;

void initializeNameSet(NameSet *nameSet);
int pushNameSet(NameSet *nameSet, char *name);
void pullNameSet(NameSet *nameSet, char *name);

void initializeLoginQueue(LoginQueue *loginQueue, int numThread);
void freeLoginQueue(LoginQueue *loginQueue);
void loginEnqueue(LoginQueue *loginQueue, int connfd);
int loginDequeue(LoginQueue *loginQueue);

void parseOption(int argc, char **argv, char *port, char *accountsFile);
int openListenFd(char *port);
void executeCommand();

void shutdownCommand();

void printUsage();

void * loginThread(void *argv);
void * communicationThread(void *argv);

int authenticateUser(int connfd, char *userName);
int promptPassword(int connfd, char *userName);
int verifyPasswordCriteria(char *password);

void receiveTimeMessage(int connfd, time_t begin);
void receiveListuMessage(int connfd);
void receiveChatMessage(int connfd, char *line);
void receiveByeMessage(int connfd, char *userName);

void sigintHandler(int signum);

#endif