#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <process.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <thread>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

#define BUF_SIZE 1024
#define NAME_LEN 20 

void ErrorHandling(const char* message);

unsigned WINAPI SendMsg(void* arg);
unsigned WINAPI RecvMsg(void* arg);

char message[BUF_SIZE];
char name[NAME_LEN];

int main(int argc, char* argv[])
{
	WSADATA wsaData;
	SOCKET hSocket;
	SOCKADDR_IN servAdr;
	int strLen, readLen;

	if (argc != 3)
	{
		printf("argv order\n1.Port\n2.Nickname\n", argv[0]);
		exit(1);
	}

	// WSAStartup : 프로그램에서 요구하는 윈도우 소켓버전을 알리고 해당 버전을 지원하는 라이브러리 초기화 작업하는 함수
	// 1. MAKEWORD(2,2)는 소켓 버전 정보, wsaData는 초기화된 라이브러리의 정보를 받을 변수(큰 의미는 없음)
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartUp() Error");


	hSocket = socket(PF_INET, SOCK_STREAM, 0);

	if (INVALID_SOCKET == hSocket)
		ErrorHandling("socket() Error");

	memset(&servAdr, 0, sizeof(servAdr));				// 구조체 servAdr 초기화
	servAdr.sin_family = AF_INET;						// IPv4 (4바이트 주소체계) 설정
	servAdr.sin_addr.s_addr = inet_addr("127.0.0.1");   // 문자열 기반의 IP주소 초기화
	servAdr.sin_port = htons(atoi(argv[1]));			// 문자열 기반의 PORT번호 초기화
	char ip[20] = "127.0.0.1";

	if (SOCKET_ERROR == connect(hSocket, (SOCKADDR*)&servAdr, sizeof(servAdr)))	// 클라이언트 소켓을 서버와 연결 요청
		ErrorHandling("connect() Error");
	else
	{
		strcpy_s(name, argv[2]);
		printf("%s is connected\n", name);

		// 이름 전송
		send(hSocket, name, strlen(name) + 1, 0);
	}

	std::thread SendThread(SendMsg, &hSocket);		// 전송 쓰레드 실행
	std::thread ReceiveThread(RecvMsg,&hSocket);	// 수신 쓰레드 실행


	SendThread.join();								// SendThread 대기 종료
	ReceiveThread.join();							// ReceiveThread 대기 종료


	closesocket(hSocket);							// 소켓 종료
	WSACleanup();									// 윈도우 소켓 사용중지를 운영체제에 알림

	return 0;
}


unsigned WINAPI SendMsg(void* arg)
{
	SOCKET hSock = *((SOCKET*)arg);
	char ClientMessage[NAME_LEN + BUF_SIZE];

	while (true)
	{
		fgets(message, BUF_SIZE, stdin);

		if (!strcmp(message, "Quit\n"))
		{
			closesocket(hSock);
			return 0;
		}

		sprintf_s(ClientMessage, "%s : %s", name, message);
		send(hSock, ClientMessage, strlen(ClientMessage), 0);
	}

	return 0;
}

unsigned WINAPI RecvMsg(void* arg)
{
	SOCKET hSock = *((SOCKET*)arg);
	char fullMsg[NAME_LEN + BUF_SIZE];
	int len;

	while (true)
	{
		len = recv(hSock, fullMsg, NAME_LEN + BUF_SIZE - 1, 0);

		if (-1 == len)
			return -1;

		fullMsg[len] = '\0';
		fputs(fullMsg, stdout);
	}
	return 0;
}

void ErrorHandling(const char* msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}

