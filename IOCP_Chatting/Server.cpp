#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <process.h>
#include <map>
#include <list>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <vector>
#include <thread>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

constexpr short bufSize = 1024;
constexpr short nameLen = 20;
constexpr short maxClnt = 1024;


enum class RW_MODE
{
	READ,
	WRITE
};

typedef struct
{
	SOCKET hClientSocket;
	SOCKADDR_IN clntAdr;
	CHAR name[30];
	CHAR IP[30];
} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

typedef struct
{
	OVERLAPPED overlapped;
	WSABUF wsaBuf;
	char buffer[bufSize];
	RW_MODE rwMode;
	int refCount;
} PER_IO_DATA, *LPPER_IO_DATA;

unsigned int __stdcall IOFunction(void* CompletionIO);
void ErrorHandling(const char* message);

std::map<SOCKET, const char*> mClient;
std::vector<std::thread> Threads;
std::mutex mtx;
std::mutex AccessMtx;

std::vector<LPPER_HANDLE_DATA> UserList;

int main(int argc, char* argv[])
{
	WSADATA wsaData;
	HANDLE hComPort;
	
	LPPER_IO_DATA ioInfo;
	LPPER_HANDLE_DATA handleInfo;

	SOCKET hServSock;
	SOCKADDR_IN servAdr;
	DWORD recvBytes, flags = 0;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartUp() Error");

	// Completion Port ������Ʈ ����
	hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	
	
	// �ִ� ������ŭ ������ ����
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	for (int i = 0; i < sysInfo.dwNumberOfProcessors; ++i)
		Threads.emplace_back(std::thread(IOFunction,hComPort));

	// ���� ���� ����
	// AF_INET�� IPv4, SOCK_STREAM�� TCP�� �ǹ�
	hServSock = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;				
	servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAdr.sin_port = htons(atoi(argv[1]));	

	bind(hServSock, (SOCKADDR*)&servAdr, sizeof(servAdr));	// �ּ� ����(IP�ּ�,PORT��ȣ) �Ҵ�
	listen(hServSock, 5);									// ���� ������ Ŭ���̾�Ʈ ���� ��û�� �޾Ƶ��� �� �ִ� ����


	printf("Chatting Start\n");
	while (true)
	{
		SOCKET hClientSocket;
		SOCKADDR_IN ClientAddr;
		int addrLen = sizeof(ClientAddr);

		hClientSocket = accept(hServSock, (SOCKADDR*)&ClientAddr, &addrLen);	// Ŭ���̾�Ʈ������ ���� ��û�� ����

		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClientSocket = hClientSocket;								// Ŭ���̾�Ʈ�� ������ ����ü�� ��� ����
		memcpy(&(handleInfo->clntAdr), &ClientAddr, addrLen);


		// Completion Port�� ���ϰ� ����, �ڵ� ������ ����, handleInfo�� �Ϸ�� IO���� ������ ������ ���� �Ű�����
		CreateIoCompletionPort((HANDLE)hClientSocket, hComPort, (DWORD)handleInfo, 0);

		// io ���� �����ϰ� Receive
		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = bufSize;
		ioInfo->wsaBuf.buf = ioInfo->buffer;
		ioInfo->rwMode = RW_MODE::READ;
		ioInfo->refCount = 0;
		

		mtx.lock();
		// name �ޱ�
		recv(handleInfo->hClientSocket, handleInfo->name, 30, 0);
		mClient.insert(std::make_pair(hClientSocket, handleInfo->name));
		printf("If you chat Quit, Program is end\n");
		mtx.unlock();

		

		// �񵿱� ����� ����
		WSARecv(handleInfo->hClientSocket, &(ioInfo->wsaBuf), 1, &recvBytes, 
			&flags, &(ioInfo->overlapped), NULL);
	}


	for (auto& t : Threads)
		t.join();

	return 0;
}

unsigned int __stdcall IOFunction(void* CompletionIO)
{
	HANDLE hComPort = (HANDLE)CompletionIO;
	SOCKET sock;
	DWORD bytesTrans;
	LPPER_HANDLE_DATA handleInfo;
	LPPER_IO_DATA ioInfo;
	DWORD flags = 0;

	char message[bufSize];
	char clientname[bufSize];

	while (true)
	{
		// Completion Queue���� Overlapped����ü ������ handle Info�� �޾ƿ´�.
		GetQueuedCompletionStatus(hComPort, &bytesTrans, (LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE);
		sock = handleInfo->hClientSocket;

		if (ioInfo->rwMode == RW_MODE::READ)
		{
			if (bytesTrans == 0)
			{
				// ������ ���� ���, Client�� �����ϴ� map���� ������ ����� �ȵǹǷ� ���ؽ�
				AccessMtx.lock();
				mClient.erase(sock);
				printf("Exit Client. Remain Client : %d\n", mClient.size());
				free(handleInfo);
				free(ioInfo);
				AccessMtx.unlock();
				continue;
			}

			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = bytesTrans;

			// Client Map�� ���� ����, �ٸ� Ŭ���̾�Ʈ�� ����Ǹ� �ȵǹǷ� ���ؽ�
			AccessMtx.lock();
			ioInfo->rwMode = RW_MODE::WRITE;
			for (auto& e : mClient)
			{
				// ���� ���� Send��û�� Overlapped IO�� ��û�ϱ� ������, ������ �ּ��� ioInfo�� ���� �����忡�� ������ �� �ִ�.
				// ���� ���۷��� ī��Ʈ�� ����
				++ioInfo->refCount;
				WSASend(e.first, (&ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
			}
			AccessMtx.unlock();


			memcpy(message, ioInfo->wsaBuf.buf, bufSize);
			message[bytesTrans] = '\0';
			strcpy_s(clientname, handleInfo->name);
			int nameLen = strlen(clientname);
			char printMessage[30];
			
			// nameLen�� �̸�, 2�� -> " :" ���ֱ� ����
			strncpy_s(printMessage, message + nameLen + 2, strlen(message) - nameLen);

			printf("%s : %s", handleInfo->name, printMessage);

			// ���� �����ϰ� ���
			ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = bufSize;
			ioInfo->wsaBuf.buf = ioInfo->buffer;
			ioInfo->rwMode = RW_MODE::READ;
			ioInfo->refCount = 0;
			WSARecv(sock, &(ioInfo->wsaBuf), 1, NULL, &flags, &(ioInfo->overlapped), NULL);
		}
		else
		{
			AccessMtx.lock();
			--ioInfo->refCount;
			AccessMtx.unlock();

			// ������ ������
			if (ioInfo->refCount <= 0)
			{
				free(ioInfo);
			}
		}
	}
}

void ErrorHandling(const char* msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}
