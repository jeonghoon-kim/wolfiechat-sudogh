#include "Server.h"

int main(int argc, char **argv) {
  
  int listenfd;
  int connfd;

  fd_set readSet;
  fd_set readySet;

  char port[MAX_PORT_LEN];
  char accountsFile[MAX_FILE_LEN];

  struct sockaddr_in *connAddr = malloc(sizeof(struct sockaddr_in));
  socklen_t connLen;

  pthread_t tid;

  int i;

  signal(SIGINT, sigintHandler);

  db = NULL;

  verboseFlag = FALSE;
  runFlag = TRUE;

  maxConnfd = -1;
  numThread = 2;
  numCommunication = 0;

  pthread_rwlock_init(&RW_lock, NULL);
  pthread_mutex_init(&Q_lock, NULL);

  parseOption(argc, argv, port, accountsFile);

  loginQueue = malloc(sizeof(LoginQueue));
  initializeLoginQueue(loginQueue, numThread);

  nameSet = malloc(sizeof(NameSet));
  initializeNameSet(nameSet);

  openDatabase(&db, accountsFile);
  initializeUserList(&userList);

  listenfd = openListenFd(port);

  FD_ZERO(&readSet);
  FD_SET(STDIN, &readSet);
  FD_SET(listenfd, &readSet);

  for(i=0; i<numThread; i++) {
    pthread_create(&tid, NULL, loginThread, NULL);
  }

  sfwrite(&Q_lock, stdout, "Currently listening on port %s\n", port);

  while(runFlag) {
    readySet  = readSet;
    select(listenfd+1, &readySet, NULL, NULL, NULL);

    if(FD_ISSET(STDIN, &readySet)) {
      executeCommand();

      if(runFlag == FALSE) {
        break;
      }
    }

    if(FD_ISSET(listenfd, &readySet)) {
      connLen = sizeof(struct sockaddr_in);
      connfd = accept(listenfd, (struct sockaddr *)connAddr, &connLen);
      loginEnqueue(loginQueue, connfd);
    }
  }

  sqlite3_close(db);
  close(listenfd);
  free(connAddr);
  freeUserList(&userList);
  free(nameSet);
  freeLoginQueue(loginQueue);
  free(loginQueue);
  return EXIT_SUCCESS;
}

void initializeNameSet(NameSet *nameSet) {
  nameSet->count = 0;
}

int pushNameSet(NameSet *nameSet, char *name) {
  int i;
  int succeed = TRUE;

  pthread_rwlock_wrlock(&RW_lock);
  for(i=0; i<nameSet->count; i++) {
    if(strcmp(nameSet->names[i], name) == 0) {
      succeed = FALSE;
    }
  }

  if(succeed == TRUE) {
    strcpy(nameSet->names[nameSet->count], name);
    (nameSet->count)++;
  }
  pthread_rwlock_unlock(&RW_lock);

  return succeed;
}

void pullNameSet(NameSet *nameSet, char *name) {
  int i;
  int j;
  int nameFound = FALSE;

  pthread_rwlock_wrlock(&RW_lock);
  for(i=0; i<nameSet->count; i++) {
    nameFound = TRUE;
    if(strcmp(nameSet->names[i], name) == 0) {
      for(j=i; j<nameSet->count-1; j++) {
        strcpy(nameSet->names[j], nameSet->names[j+1]);
      }
    }
  }

  if(nameFound == TRUE) {
    (nameSet->count)--;
  }

  pthread_rwlock_unlock(&RW_lock);
}

void initializeLoginQueue(LoginQueue *loginQueue, int numThread) {
  loginQueue->connfds = malloc(sizeof(int) * numThread);
  loginQueue->numThread = numThread;
  loginQueue->frontConnfd = 0;
  loginQueue->rearConnfd = 0;

  sem_init(&(loginQueue->mutex), 0, 1);
  sem_init(&(loginQueue->slots), 0, numThread);
  sem_init(&(loginQueue->items), 0, 0);
}

void freeLoginQueue(LoginQueue *loginQueue) {
  free(loginQueue->connfds);
}

void loginEnqueue(LoginQueue *loginQueue, int connfd) {
  sem_wait(&(loginQueue->slots));
  sem_wait(&(loginQueue->mutex));
  loginQueue->connfds[(++(loginQueue->rearConnfd)%(loginQueue->numThread))] = connfd;
  sem_post(&(loginQueue->mutex));
  sem_post(&(loginQueue->items));
}

int loginDequeue(LoginQueue *loginQueue) {
  int connfd;
  sem_wait(&(loginQueue->items));
  sem_wait(&(loginQueue->mutex));
  connfd = loginQueue->connfds[(++(loginQueue->frontConnfd)%(loginQueue->numThread))];
  sem_post(&(loginQueue->mutex));
  sem_post(&(loginQueue->slots));
  return connfd;
}

void parseOption(int argc, char **argv, char *port, char *accountsFile) {

  int opt;

  memset(port, 0, MAX_PORT_LEN);
  memset(motd, 0, MAX_LEN);
  memset(accountsFile, 0, MAX_FILE_LEN);

  while((opt = getopt(argc, argv, "ht:v")) != -1) {
    switch(opt) {
    case 'h':
      printUsage();
      exit(EXIT_SUCCESS);
      break;
    case 't':
    	numThread = atoi(optarg);
    	break;
    case 'v':
      verboseFlag = TRUE;
      break;
    case '?':
    default:
      printUsage();
      exit(EXIT_FAILURE);
      break;
    }
  }

  if(optind < argc && (argc - optind) == 2) {
    strcpy(port, argv[optind++]);
    strcpy(motd, argv[optind++]);
  } else if(optind < argc && (argc - optind) == 3) {
    strcpy(port, argv[optind++]);
    strcpy(motd, argv[optind++]);
    strcpy(accountsFile, argv[optind++]);
  } else {
    if(argc - optind < 2) {
      printError("Missing arguments\n");
    } else if(argc - optind > 3) {
      printError("Too many arguments\n");
    }

    printUsage();
    exit(EXIT_FAILURE);
  }
}

int openListenFd(char *port) {

  struct addrinfo hints;
  struct addrinfo *list;
  struct addrinfo *cur;

  int listenfd;
  int opt = 1;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG | AI_NUMERICSERV;
  getaddrinfo(NULL, port, &hints, &list);

  for(cur=list; cur!=NULL; cur=cur->ai_next) {
    if((listenfd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol)) < 0) {
      continue;
    }

    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(int));

    if(bind(listenfd, cur->ai_addr, cur->ai_addrlen) == 0) {
      break;
    }

    close(listenfd);
  }

  freeaddrinfo(list);

  if(cur == NULL) {
    return -1;
  }

  if(listen(listenfd, BACKLOG) < 0) {
    close(listenfd);
    return -1;
  }

  return listenfd;
}

void executeCommand() {
  
  char buf[MAX_LEN];
  fgets(buf, MAX_LEN, stdin);

  if(strcmp(buf, "/users\n") == 0) {
    printAllUserInfo(userList);
  } else if(strcmp(buf, "/help\n") == 0) {
    printUsage();
  } else if(strcmp(buf, "/accts\n") == 0) {
    printAllAccountsInfo(&db);
  } else if(strcmp(buf, "/shutdown\n") == 0) {
    shutdownCommand();
  } else {
    printError("Command does not exist\n");
  }
}

void shutdownCommand() {
  User *cur = userList.head;
  int connfd;

  while(cur != NULL) {
    connfd = cur->connfd;
    Send(connfd, "BYE \r\n\r\n", strlen("BYE \r\n\r\n"), 0);
    cur = cur->next;
  }

  runFlag = FALSE;
}

void printUsage() {
  sfwrite(&Q_lock, stderr, "USAGE: ./server [-h|-v] [-t THREAD_COUNT] PORT_NUMBER MOTD ACCOUNTS_FILE\n");
  sfwrite(&Q_lock, stderr, "-h               Displays help menu & returns EXIT_SUCCESS.\n");
  sfwrite(&Q_lock, stderr, "-t THREAD_COUNT  The number of threads used for the login queue.\n");
  sfwrite(&Q_lock, stderr, "-v               Verbose print all incoming and outgoing protocol verbs & content.\n");
  sfwrite(&Q_lock, stderr, "PORT_NUMBER      Port number to listen on.\n");
  sfwrite(&Q_lock, stderr, "MOTD             Message to display to the client when they connect.\n");
  sfwrite(&Q_lock, stderr, "ACCOUNTS_FILE    File containing username and password data.\n");
  sfwrite(&Q_lock, stderr, "\nServer Commands\n");
  sfwrite(&Q_lock, stderr, "/users           Display a list of currently logged in users\n");
  sfwrite(&Q_lock, stderr, "/help            Display usage statement\n");
  sfwrite(&Q_lock, stderr, "/shutdown        Terminate server\n");
  sfwrite(&Q_lock, stderr, "/accts           Display a list of all user accounts and information\n");
}

void * loginThread(void *argv) {

  pthread_t tid;

  char buf[MAX_LEN];
  char userName[MAX_NAME_LEN];

  int connfd;

  pthread_detach(pthread_self());

  while(TRUE) {

    connfd = loginDequeue(loginQueue);

    Recv(connfd, buf, MAX_LEN, 0);

    if(strcmp(buf, "WOLFIE \r\n\r\n") == 0) {
    
      Send(connfd, "EIFLOW \r\n\r\n", strlen("EIFLOW \r\n\r\n"), 0);

      if(authenticateUser(connfd, userName) == TRUE) {
        if(promptPassword(connfd, userName) == TRUE) {
          sprintf(buf, "HI %s \r\n\r\n", userName);
          Send(connfd, buf, strlen(buf), 0);
          insertUser(&userList, userName, connfd, time(NULL));
          pullNameSet(nameSet, userName);

          sprintf(buf, "MOTD %s \r\n\r\n", motd);
          Send(connfd, buf, strlen(buf), 0);

          if(numCommunication == 0) {
            numCommunication++;
            maxConnfd = connfd;
            FD_ZERO(&communicationSet);
            FD_SET(connfd, &communicationSet);
            pthread_create(&tid, NULL, communicationThread, NULL);
          } else {
            numCommunication++;
            maxConnfd = (connfd > maxConnfd) ? connfd : maxConnfd;
            FD_SET(connfd, &communicationSet);
          }      
        } else {
          pullNameSet(nameSet, userName);
        }
      } else {
        if(isUserExist(userList, userName) == TRUE) {
          pullNameSet(nameSet, userName);
        }
      }
    }
  }

  return NULL;
}

int authenticateUser(int connfd, char *userName) {

  char buf[MAX_LEN];
  
  Recv(connfd, buf, MAX_LEN, 0);

  if(strncmp(buf, "IAMNEW ", 7) == 0 && strcmp(&buf[strlen(buf)-5], " \r\n\r\n") == 0) {
    
    sscanf(buf, "IAMNEW %s \r\n\r\n", userName);

    if(isAccountExist(&db, userName) == FALSE && pushNameSet(nameSet, userName) == TRUE) {
      sprintf(buf, "HINEW %s \r\n\r\n", userName);
      Send(connfd, buf, strlen(buf), 0);

      return TRUE;
    } else {
      sprintf(buf, "ERR 00 USER NAME TAKEN %s \r\n\r\n", userName);
      Send(connfd, buf, strlen(buf), 0);
      Send(connfd, "BYE \r\n\r\n", strlen("BYE \r\n\r\n"), 0);

      return FALSE;
    }
  } else if (strncmp(buf, "IAM ", 4) == 0 && strcmp(&buf[strlen(buf)-5], " \r\n\r\n") == 0) {
    
    sscanf(buf, "IAM %s \r\n\r\n", userName);
    
    if(isAccountExist(&db, userName) == FALSE) {
      Send(connfd, "ERR 01 USER NOT AVAILABLE \r\n\r\n", strlen("ERR 01 USER NOT AVAILABLE \r\n\r\n"), 0);
      Send(connfd, "BYE \r\n\r\n", strlen("BYE \r\n\r\n"), 0);

      return FALSE;
    } else if(isUserExist(userList, userName) == FALSE && pushNameSet(nameSet, userName) == TRUE) {
      sprintf(buf, "AUTH %s \r\n\r\n", userName);
      Send(connfd, buf, strlen(buf), 0);

      return TRUE;
    } else {
      Send(connfd, "ERR 00 USER NAME TAKEN \r\n\r\n", strlen("ERR 00 USER NAME TAKEN \r\n\r\n"), 0);
      Send(connfd, "BYE \r\n\r\n", strlen("BYE \r\n\r\n"), 0);

      return FALSE;
    }
  }

  return FALSE;
}

int promptPassword(int connfd, char *userName) {

  char buf[MAX_LEN];
  char password[MAX_PASSWORD_LEN];
  
  Recv(connfd, buf, MAX_LEN, 0);

  if(strncmp(buf, "NEWPASS ", 8) == 0 && strcmp(&buf[strlen(buf)-5], " \r\n\r\n") == 0) {
    sscanf(buf, "NEWPASS %s \r\n\r\n", password);

    if(verifyPasswordCriteria(password) == TRUE) {
      char salt[MAX_SALT_LEN*2+1];
      char hash[SHA256_DIGEST_LENGTH*2+1];
      
	    Send(connfd, "SSAPWEN \r\n\r\n", strlen("SSAPWEN \r\n\r\n"), 0);
      getSalt(salt);
      getHash(hash, password, salt);
	    insertAccount(&db, userName, hash, salt);
	    return TRUE;
    } else {
    	Send(connfd, "ERR 02 BAD PASSWORD \r\n\r\n", strlen("ERR 00 BAD PASSWORD \r\n\r\n"), 0);
    	Send(connfd, "BYE \r\n\r\n", strlen("BYE \r\n\r\n"), 0);
      return FALSE;
    }
  } else if (strncmp(buf, "PASS ", 5) == 0 && strcmp(&buf[strlen(buf)-5], " \r\n\r\n") == 0) {
    sscanf(buf, "PASS %s \r\n\r\n", password);
    
    if(verifyPassword(&db, userName, password) == TRUE) {
      Send(connfd, "SSAP \r\n\r\n", strlen("SSAP \r\n\r\n"), 0);
      return TRUE;
    } else {
      Send(connfd, "ERR 02 BAD PASSWORD \r\n\r\n", strlen("ERR 02 BAD PASSWORD \r\n\r\n"), 0);
      Send(connfd, "BYE \r\n\r\n", strlen("BYE \r\n\r\n"), 0);
      return FALSE;
    }
  }

  return FALSE;
}

int verifyPasswordCriteria(char *password) {
	
	int i;
	int upper = 0;
	int lower = 0;
	int symbol = 0;
	int number = 0;
	
	for(i = 0; i < strlen(password); i++) {
		if(password[i] > 47 && password[i] < 58) {
			number++;
		} else if(password[i] > 64 && password[i] < 91) {
			upper++;
		} else if(password[i] > 96 && password[i] < 123) {
			lower++;
		} else {
			symbol++;
		}
	}

	if(strlen(password) >= 5 && upper && symbol && number) {
		return TRUE;
	}

	return FALSE;
}

void * communicationThread(void *argv) {

  char buf[MAX_LEN];
  char userName[MAX_NAME_LEN];

  fd_set communicationReadySet;

  struct timeval tv;

  int i;

  pthread_detach(pthread_self());

  tv.tv_sec = 1;
  tv.tv_usec = 0;

  while(TRUE) {

    communicationReadySet = communicationSet;
    select(maxConnfd+1, &communicationReadySet, NULL, NULL, &tv);

    for(i=3; i<maxConnfd+1; i++) {
      if(FD_ISSET(i, &communicationReadySet)) {
        Recv(i, buf, MAX_LEN, 0);

        if(strcmp(buf, "TIME \r\n\r\n") == 0) {
          receiveTimeMessage(i, matchBegin(userList, i));
        } else if(strcmp(buf, "LISTU \r\n\r\n") == 0) {
          receiveListuMessage(i);
        } else if(strncmp(buf, "MSG", 3) == 0) {
          receiveChatMessage(i, buf);
        } else if(strcmp(buf, "BYE \r\n\r\n") == 0) {
          matchUser(userList, userName, i);
          receiveByeMessage(i, userName);

          close(i);
          FD_CLR(i, &communicationSet);
        }
      }
    }
  }

  return NULL;
}

void receiveTimeMessage(int connfd, time_t begin) {
  char buf[MAX_LEN];
  time_t current = time(NULL);

  sprintf(buf, "EMIT %ld \r\n\r\n", current-begin);
  Send(connfd, buf, strlen(buf), 0);
}

void receiveListuMessage(int connfd) {
  char buf[MAX_LISTU_LEN];

  User *cur = userList.head;

  sprintf(buf, "UTSIL");

  while(cur != NULL) {
    sprintf(buf+strlen(buf), " %s \r\n", cur->userName);
    cur = cur->next;
  }

  sprintf(buf+strlen(buf), "\r\n");
  Send(connfd, buf, strlen(buf), 0);
}

void receiveChatMessage(int connfd, char *line) {
  char to[MAX_NAME_LEN];
  char from[MAX_NAME_LEN];

  User *user;
  int userConnfd;

  sscanf(line, "MSG %s %s", to, from);

  if(isUserExist(userList, to) == FALSE || isUserExist(userList, from) == FALSE) {
    if((user = findUser(userList, to)) != NULL) {
      userConnfd = user->connfd;
      Send(userConnfd, "ERR 01 USER NOT AVAILABLE \r\n\r\n", strlen("ERR 01 USER NOT AVAILABLE \r\n\r\n"), 0);
    }

    if((user = findUser(userList, from)) != NULL) {
      userConnfd = user->connfd;
      Send(userConnfd, "ERR 01 USER NOT AVAILABLE \r\n\r\n", strlen("ERR 01 USER NOT AVAILABLE \r\n\r\nd"), 0);
    }
  } else if(strcmp(to, from) == 0) {
      Send(connfd, "ERR 01 USER NOT AVAILABLE \r\n\r\n", strlen("ERR 01 USER NOT AVAILABLE \r\n\r\nd"), 0);
  } else {
    if((user = findUser(userList, to)) != NULL) {
      userConnfd = user->connfd;
      Send(userConnfd, line, strlen(line), 0);
    }

    if((user = findUser(userList, from)) != NULL) {
      userConnfd = user->connfd;
      Send(userConnfd, line, strlen(line), 0);
    }
  }
}

void receiveByeMessage(int connfd, char *userName) {
  char buf[MAX_LEN];

  User *cur;

  sprintf(buf, "UOFF %s \r\n\r\n", userName);

  Send(connfd, "BYE \r\n\r\n", strlen("BYE \r\n\r\n"), 0);
  deleteUser(&userList, userName);

  cur = userList.head;

  while(cur != NULL) {
    Send(cur->connfd, buf, strlen(buf), 0);
    cur = cur->next;
  }
}

void sigintHandler(int signum) {
  shutdownCommand();
  exit(signum);
}