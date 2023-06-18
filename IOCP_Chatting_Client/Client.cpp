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

	// WSAStartup : ���α׷����� �䱸�ϴ� ������ ���Ϲ����� �˸��� �ش� ������ �����ϴ� ���̺귯�� �ʱ�ȭ �۾��ϴ� �Լ�
	// 1. MAKEWORD(2,2)�� ���� ���� ����, wsaData�� �ʱ�ȭ�� ���̺귯���� ������ ���� ����(ū �ǹ̴� ����)
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartUp() Error");


	hSocket = socket(PF_INET, SOCK_STREAM, 0);

	if (INVALID_SOCKET == hSocket)
		ErrorHandling("socket() Error");

	memset(&servAdr, 0, sizeof(servAdr));				// ����ü servAdr �ʱ�ȭ
	servAdr.sin_family = AF_INET;						// IPv4 (4����Ʈ �ּ�ü��) ����
	servAdr.sin_addr.s_addr = inet_addr("127.0.0.1");   // ���ڿ� ����� IP�ּ� �ʱ�ȭ
	servAdr.sin_port = htons(atoi(argv[1]));			// ���ڿ� ����� PORT��ȣ �ʱ�ȭ
	char ip[20] = "127.0.0.1";

	if (SOCKET_ERROR == connect(hSocket, (SOCKADDR*)&servAdr, sizeof(servAdr)))	// Ŭ���̾�Ʈ ������ ������ ���� ��û
		ErrorHandling("connect() Error");
	else
	{
		strcpy_s(name, argv[2]);
		printf("%s is connected\n", name);

		// �̸� ����
		send(hSocket, name, strlen(name) + 1, 0);
	}

	std::thread SendThread(SendMsg, &hSocket);		// ���� ������ ����
	std::thread ReceiveThread(RecvMsg,&hSocket);	// ���� ������ ����


	SendThread.join();								// SendThread ��� ����
	ReceiveThread.join();							// ReceiveThread ��� ����


	closesocket(hSocket);							// ���� ����
	WSACleanup();									// ������ ���� ��������� �ü���� �˸�

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

