

#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BFSIZE 1024

void error(char *msg) {
    perror(msg);
    exit(1);
}
/*
파일 사이즈를 알려주는 함수입니다.
파일 포인터를 맨 끝까지 이동시킨 후 파일 포인터 위치를 저장하고 파일 포인터를 처음으로 위치시킵니다.
*/
int file_size(FILE *file) {
    int size;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    return size;
}

/*
HTTP Request Protocol Message를 분석한 후 요청 파일명 부분을 짤라냅니다.ß
strncpy함수를 이용해 Source파일이 존재하는 폴더뿐만아니라 하위폴더에 존재하는 파일도 접근 가능합니다.
*/
char *get_file_name(char *request) {
    char getHeader[BFSIZE] = "\0";
    char *_tok;
    strncpy(getHeader, request + 5, strstr(request, "HTTP/1.1") - request - 1 - 5);
    _tok = (char *)malloc(strlen(getHeader));
    strcpy(_tok, getHeader);
    return _tok;
}

/*
해당파일의 파일타입을 반환합니다.
*/
char *get_file_mime(char *file) {
    char fileType[10];
    strcpy(fileType, file + (strstr(file, ".") - file));
    if (strcmp(fileType, ".gif") == 0)
        return "image/gif";
    if (strcmp(fileType, ".png") == 0)
        return "image/png";
    if (strcmp(fileType, ".jpeg") == 0)
        return "image/jpg";
    if (strcmp(fileType, ".pdf") == 0)
        return "application/pdf";
    if (strcmp(fileType, ".mp3") == 0)
        return "audio/mpeg3";
    if (strcmp(fileType, ".html") == 0)
        return "text/html";
    else
        return "text/plain";
    return NULL;
}

int main(int argc, char *argv[]) {
    int sockfd, newsockfd, portno;
    int n, filesize, responseSize;
    char buffer[BFSIZE] = "\0";
    char response[BFSIZE] = "\0";
    char *fbuff, *tok;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    FILE *file;

    bzero((char *)&serv_addr, sizeof(serv_addr));
    bzero(buffer, BFSIZE);
    bzero(response, BFSIZE);

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }

    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) error("ERROR opening socker");
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    listen(sockfd, 20);

	int newPid = -1;

    while (1) {
        if ((newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) < 0)
            error("ERROR on accpet");

		newPid = fork();

		if(newPid == 0) {
			break;
		}
	}

        if (read(newsockfd, buffer, BFSIZE - 1) < 0) error("ERROR reading fro socket");

        printf("%s \n", buffer);

        /*
    파일명을 알아내고 해당 파일을 오픈합니다.
    파일이 없을경우에 404메세지를 날려줍니다.
    */
        tok = get_file_name(buffer);
        if (!(file = fopen(tok, "rb"))) {
            responseSize = sprintf(response, "HTTP/1.1 404 Not Found\nServer: Apache\n");
            responseSize += sprintf(response + responseSize, "Content-Type: text/html\n");
            responseSize += sprintf(response + responseSize, "Content-Length: 7\n\n");
            responseSize += sprintf(response + responseSize, "NO FILE");
            write(newsockfd, response, strlen(response));
            error("file not found\n");
        }
        /*
	오픈한 파일을 읽어온다음에 malloc함수를 이용해 파일 사이즈만큼 버퍼크기를 할당 후 버퍼에 파일을 저장합니다.
    */
        filesize = file_size(file);
        fbuff = malloc(filesize);
        fread(fbuff, 1, filesize, file);
        /*
    HTTP Response Protocol Message를 전송해줍니다.
    */
        responseSize = sprintf(response, "HTTP/1.1 200 OK\nServer: Apache\n");
        responseSize += sprintf(response + responseSize, "Content-Type: %s\n", get_file_mime(tok));
        responseSize += sprintf(response + responseSize, "Content-Length: %d\n\n", filesize);

        if (write(newsockfd, response, strlen(response)) < 0) error("ERROR writing to socket");
        if (write(newsockfd, fbuff, filesize) < 0) error("ERROR writing to socket");

        free(fbuff);
        free(tok);

        fclose(file);
        close(newsockfd);
	
		//if(newPid != 0) {
    		close(sockfd);
		//}

    return 0;
}
