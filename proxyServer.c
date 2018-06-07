#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

typedef int bool;
#define true 1
#define false 0
#define null NULL
#define BFSIZE 2048
#define CACHEMSIZE 5 * 1024 * 1024 //캐시 최대 사이즈
#define OBJECTMSIZE 512 * 1024 //캐시 오브젝트 최대 사이즈

//  Project Test address
//  http://infosec.hanyang.ac.kr/infosec/
//  http://commlab.hanyang.ac.kr/
//  http://cnlab.hanyang.ac.kr/
//  http://database.hanyang.ac.kr/
//  http://www.all-con.co.kr/
//  http://ajax.googleapis.com/ajax/libs/jquery/1.3.2/jquery.min.js 

typedef struct INFOCLI {
    struct sockaddr_in * cli_addr;
    int sockfd;
}infocli;

typedef union defSema { 
 int val; 
 struct semid_ds *buf; 
 unsigned short int *array; 
 struct seminfo *__buf; 
} defsema;
int acceptKey, fileKey, cacheKey, tempCacheKey, hostKey;

typedef struct CACHE {
    int size;
    time_t time;
    char * hostName, *fileName;
    void * buffer;
    struct CACHE * next;
}cache;
int cacheSize;
cache * cacheHead;

void CallExit(); //프로그램 종료될 때 모든 세마포어 해제 (정상종료 / 비정상종료)
void CallSignalExit(int sig); //프로그램 종료될 때 캐시 해제(출력), CallExit함수 호출 (정상종료)
void error(char * msg); //비정상종료시 오류메세지 출력
void errorS(char * msg, int count, ...); //비정상종료시 오류메세지 출력 및 열려있는 소켓 종료(가변인자 사용)

int initsem(key_t semkey); //세마포어 초기화
void freesem(int semid); //세마포어 해제
int p(int semid); //세마포어 컨트롤
int v(int semid); //세마포어 컨트롤

void ReallocMemory(void ** origin, int size); //Realloc함수 컨트롤
bool CmpCache(char * hostName, char * fileName, cache * cmpCache); //기존에 있는 캐시와 비교
cache * CreateCache(char * hostName, char * fileName, int size, void * buffer); //캐시 생성
bool GetCacheAndSend(char * hostName, char * fileName, int clientSocket); //기존 캐시와 비교 후 있으면 캐시 저달
void ReCreateCache(cache ** now, int size, void * buffer); //기존에 있는 캐시 업데이트
void AddCache(char * hostName, char * fileName, int size, void * buffer); //캐시 추가(링크드리스트)
void AllFreeCache(); //모든 캐시 해제
void DelCache(); //가장 오래된 캐시 해제
void PrintCacheTest(); //모든 캐시 출력

void InitSocket(struct sockaddr_in * socket, int portNo); //소켓 초기화
void WriteFile(struct hostent * cntSocket, char * fileName, int size); //로그 작성
void MakeRequest(char * httpRequest, char * host, char * fileName); //Proxy -> Server Request 메세지 작성
char * GetHostName(char * request, char * hostName); //Client -> Proxy Request 호스트이름 파싱
char * GetFileName(char * request, char * hostName, char * fileName); //Client -> Proxy Request 파일이름 파싱

void SendBuffer(char * request, char * hostName, char * fileName, struct hostent * cntSocket, int clientSocket); //Client <-> Proxy <-> Server 통신
void * ProcRequest(void * info); //Server 연결 전 동작

int main(int argc, char * argv[]) {
    int sockfd, newsockfd, i;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen;

    if(argc < 2) error("ERROR, no port provided");

    //프로그램 종료시 호출 함수 등록
    atexit(CallExit);

    memset(&cli_addr, 0, sizeof(cli_addr));
    memset(&serv_addr, 0, sizeof(serv_addr));
    clilen = sizeof(cli_addr);

    InitSocket(&serv_addr, atoi(argv[1]));

    //SIGINT(ctrl + c), SIGTSTP(ctrl + z) 제외한 모든 시그널 Ignore
    for (i = 1; i < NSIG; i++)
        signal(i, SIG_IGN);
    signal(SIGINT, CallSignalExit);
    signal(SIGTSTP, CallSignalExit);

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) error("ERROR opening socker");
    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        errorS("ERROR on binding", 1, sockfd);
    listen(sockfd, 1000);

    acceptKey = initsem(11111);
    fileKey = initsem(11112);
    tempCacheKey = initsem(11113);
    cacheKey = initsem(11114);
    hostKey = initsem(11115);

    cacheSize = 0;
    cacheHead = (cache *)malloc(sizeof(cache));
    cacheHead->next = null;

    printf("\n-------------------------------------------------\n");
    printf("|\tStart Proxy Server!!\t\t\t|\n");
    printf("|\tBinding Port Numb : %d\t\t|\n", atoi(argv[1]));
    printf("|\tMade by Kangdonghyuk\t\t\t|\n");
    printf("-------------------------------------------------\n");

    while(1) {
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if(newsockfd > 0) {
            infocli * infoClient = (infocli*)malloc(sizeof(infocli));
            infoClient->sockfd = newsockfd;
            infoClient->cli_addr = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
            memcpy(infoClient->cli_addr, &cli_addr, sizeof(cli_addr));

            pthread_t * thread = (pthread_t*)malloc(sizeof(pthread_t));
            pthread_create(thread, NULL, ProcRequest, infoClient);
            //ProcRequest(infoClient);
        }
    }
    printf("WHAT WHILE BREAK\n");

    close(newsockfd);
    close(sockfd);

    exit(0);

    return 0;
}


void CallExit() {
    printf("exit program\n");
    freesem(acceptKey);
    freesem(fileKey);
    freesem(tempCacheKey);
    freesem(cacheKey);
    freesem(hostKey);
    exit(0);
}
void CallSignalExit(int sig) {
    PrintCacheTest();
    AllFreeCache();
    free(cacheHead);
    printf("kill program\n");
    CallExit();
}
void error(char * msg) {
    perror(msg);
    exit(1);
}
void errorS(char * msg, int count, ...) {
    perror(msg);
    va_list ap;
    int i;
    va_start(ap, count);
    for(i=0; i<count; i++)
        close(va_arg(ap, int));
    va_end(ap);
    exit(1);
}

int initsem(key_t semkey)
{
    int status = 0, semid;
    if ((semid = semget(semkey, 1, 0777 | IPC_CREAT)) < 0){
        semid = semget(semkey, 1, 0);
    } else {
        defsema arg;
        arg.val = 1;
        status = semctl(semid, 0, SETVAL, arg);
    }
    if (semid == -1 || status == -1) {
        perror("sem init error");
        return -1;
    } else {
        return semid;
    }
}
void freesem(int semid) {
    semctl(semid, 0, IPC_RMID, 0);
}
int p(int semid) {
    struct sembuf buf;
    buf.sem_num = 0;
    buf.sem_op = -1;
    buf.sem_flg = SEM_UNDO;
    
    if(semop(semid, &buf, 1) == -1) {
        perror("p error");
        return -1;
    } else  return 0;
}
int v(int semid) {
    struct sembuf buf;
    buf.sem_num = 0;
    buf.sem_op = 1;
    buf.sem_flg = SEM_UNDO;
    
    if(semop(semid,&buf, 1) == -1) {
        perror("v error");
        return -1;
    } else  return 0;
}

void ReallocMemory(void ** origin, int size) {
    void * new;
    new = realloc(*origin, size);
    if(new == null) perror("Realloc Fail");
    else *origin = new;
}
bool CmpCache(char * hostName, char * fileName, cache * cmpCache) {
    if(strcmp(hostName, cmpCache->hostName) == 0) {
        if(strcmp(fileName, cmpCache->fileName) == 0) {
            return true;
        }
    }
    return false;
}
bool GetCacheAndSend(char * hostName, char * fileName, int clientSocket) {
    bool returnState = false;
    cache * now = cacheHead->next;
    while(now != null) {
        if(CmpCache(hostName, fileName, now) == true) {
            send(clientSocket, now->buffer, now->size, 0);
            returnState = true;
        }
        now = now->next;
    }
    return returnState;
}
cache * CreateCache(char * hostName, char * fileName, int size, void * buffer) {
    cache * newCache = (cache*)malloc(sizeof(cache));
    newCache->next = null;
    newCache->size = size;
    time(&newCache->time);
    newCache->hostName = (char*)malloc(strlen(hostName));
    newCache->fileName = (char*)malloc(strlen(fileName));
    newCache->buffer = malloc(size);
    strcpy(newCache->hostName, hostName);
    strcpy(newCache->fileName, fileName);
    memcpy(newCache->buffer, buffer, size);
    return newCache;
}
void ReCreateCache(cache ** now, int size, void * buffer) {
    ReallocMemory(&((*now)->buffer), size + (*now)->size);
    if((*now)->buffer != null)
        memcpy((*now)->buffer+(*now)->size, buffer, size);
    (*now)->size += size;
}
void AllFreeCache() {
    cache * nowCache = cacheHead->next;
    cache * nextCache;
    while(nowCache != null) {
        nextCache = nowCache->next;
        free(nowCache);
        nowCache = nextCache;
    }
}
void DelCache() {
    cache * delCache = cacheHead->next;
    printf("%s(%d)>", delCache->fileName, delCache->size);
    cacheSize -= delCache->size;
    cacheHead->next = delCache->next; 
    free(delCache);
}
void AddCache(char * hostName, char * fileName, int size, void * buffer) {
    bool cmpCache = false;
    cache * nowCache = cacheHead;
    if(cacheHead->next == null)
        cacheHead->next = CreateCache(hostName, fileName, size, buffer);
    else{
        while(nowCache->next != null) {
            if((cmpCache = CmpCache(hostName, fileName, nowCache)) == true)
                break;
            nowCache = nowCache->next;
        }
        if(cmpCache == false)
            nowCache->next = CreateCache(hostName, fileName, size, buffer);
        else if(cmpCache == true) 
            ReCreateCache(&nowCache, size, buffer);

    }
    cacheSize+=size;

    while(cacheSize > CACHEMSIZE) {
        printf("-$Over Tot Size Del(LRU) <");
        DelCache();
        printf(" %d / %d\n", cacheSize, CACHEMSIZE);
    }
}
void PrintCacheTest() {
    printf("\n\n-------------------CACHE LIST---------------------\n");
    cache * nowCache = cacheHead->next;
    while(nowCache != null) {
        printf("%s : %d \n", nowCache->fileName, nowCache->size);
        nowCache = nowCache->next;
    }
    printf("-----------------------------------------------------\n");
}

void InitSocket(struct sockaddr_in * socket, int portNo) {
    (*socket).sin_family = AF_INET;
    (*socket).sin_addr.s_addr = INADDR_ANY;
    (*socket).sin_port = htons(portNo);
}

void WriteFile(struct hostent * cntSocket, char * fileName, int size) {
    char writeForm[BFSIZE];
    char * timeForm;
    time_t nowTime;
    struct in_addr  myen;
    long int *add;
    int fd = open("proxy.log", O_CREAT | O_APPEND | O_RDWR, 0777);

    time(&nowTime); 
    timeForm = ctime(&nowTime);
    timeForm[strlen(timeForm)-1] = '\0';

    add = (long int *)*cntSocket->h_addr_list;
    myen.s_addr = *add;

    memset(writeForm, '\0', BFSIZE);
    sprintf(writeForm, "%s EST: %s %s/%s %d\n", 
        timeForm, inet_ntoa(myen), cntSocket->h_name, fileName, size);

    write(fd, writeForm, strlen(writeForm));

    close(fd);
}

void MakeRequest(char * httpRequest, char * host, char * fileName) {
    int httpRequestSize = 0;
    httpRequestSize += sprintf(httpRequest, "GET /%s HTTP/1.1\r\n", fileName);
    httpRequestSize += sprintf(httpRequest+httpRequestSize, "Host: %s\r\n", host);
    httpRequestSize += sprintf(httpRequest+httpRequestSize, "Connection: close\r\n\r\n");
}

char * GetHostName(char * request, char * hostName) {
    char * str = strstr(request, "CONNECT");
    if(str != 0) {
        if(*str == *request)
            return null;
    }
    strncpy(hostName, strstr(request, "Host:") + 6,
        (strstr(request, "User-Agent") - strstr(request, "Host:")) - 8);
    return hostName;
}

char * GetFileName(char * request, char * hostName, char * fileName) {
    strncpy(fileName, strstr(request, hostName)+strlen(hostName)+1 ,
        strstr(request, "HTTP/1.1") - strstr(request, hostName) - strlen(hostName) - 2 );
    return fileName;
}

void SendBuffer(char * request, char * hostName, char * fileName, struct hostent * cntSocket, int clientSocket) {
    int sockfd, port = 80;
    int httpRequestSize = 0, recvSize, fileSize = 0;
    char response[BFSIZE];
    char httpRequest[BFSIZE];
    void * data = null;
    int dataSize = 0;
    struct sockaddr_in web_addr;

    memset(httpRequest, '\0', BFSIZE);
    memset(&web_addr, 0, sizeof(web_addr));

    memcpy(&web_addr.sin_addr.s_addr, cntSocket->h_addr, cntSocket->h_length);
    web_addr.sin_family = AF_INET;
    web_addr.sin_port = htons(port);

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        error("Error opening socket");

    if(connect(sockfd, (struct sockaddr*)&web_addr, sizeof(web_addr)) < 0)
        errorS("Error connecting Try", 2, sockfd, clientSocket);

    MakeRequest(httpRequest, hostName, fileName);

    send(sockfd, httpRequest, strlen(httpRequest), 0);

    do {
        memset(response, '\0', BFSIZE);
        recvSize = recv(sockfd, response, BFSIZE-1, 0);
        if(!(recvSize <= 0)) {
            fileSize += recvSize;

            //임시 캐시파일 생성
            p(tempCacheKey);
            if(data == null) {
                dataSize = recvSize;
                data = malloc(dataSize);
                memcpy(data, (void*)response, dataSize);
            }
            else if(data != null) {
                ReallocMemory(&data, dataSize + recvSize);
                if(data != null) {
                    memcpy(data + dataSize, (void*)response, recvSize);
                    dataSize += recvSize;
                }
            }
            v(tempCacheKey);

            send(clientSocket, response, recvSize, 0);
        }
    }while(recvSize > 0);

    if(data != null) {
        if(dataSize > OBJECTMSIZE)
            printf("-$Over Object Size <%s-(%d / %d)>\n", fileName, dataSize, OBJECTMSIZE);
        else {
            p(cacheKey);
            AddCache(hostName, fileName, dataSize, data);
            v(cacheKey);
        }
        free(data);
    }

    p(fileKey);
    WriteFile(cntSocket, fileName, fileSize);
    v(fileKey);
    
    close(sockfd);
}

void * ProcRequest(void * info) {
    int recvSize, sockfd;
    char request[BFSIZE];
    char hostName[BFSIZE];
    char fileName[BFSIZE];
    struct hostent * cntSocket;
    struct sockaddr_in * cli_addr;
    
    sockfd = ((infocli*)info)->sockfd;
    cli_addr = ((infocli*)info)->cli_addr;
    if(info != null)
        free(info);

    pthread_detach(pthread_self());

    memset(fileName, '\0', BFSIZE);
    memset(request, '\0', BFSIZE);
    memset(hostName, '\0', BFSIZE);

    recvSize = recv(sockfd, request, BFSIZE-1, 0);
    if(recvSize < 0) {
        if(cli_addr != null)
            free(cli_addr);
        errorS("Can't read socket request Thread ::", 1, sockfd);
        pthread_exit(0);
    }
    else if(recvSize > 0) {
        if(GetHostName(request, hostName) != null) {
            //요청한 파일이 캐시에 있는지 확인 후 있으면 바로 전달
            if(GetCacheAndSend(hostName, GetFileName(request, hostName, fileName), sockfd) == true)
                printf("-$Send from Cache <%s>\n", fileName);
            else {
                //캐시에 없을경우 서버랑 연결
                p(hostKey);
                if((cntSocket = gethostbyname(hostName)) == NULL) {
                    errorS("Can't find Hostname", 1, sockfd);
                    pthread_exit(0);
                }
                SendBuffer(request, hostName, fileName, cntSocket, sockfd);
                v(hostKey);
            }
        }
    }
    else    printf("Recv 0 Returned\n");
        
    if(cli_addr != null)
        free(cli_addr);

    close(sockfd);
    pthread_exit(0);
}