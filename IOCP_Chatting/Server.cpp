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

	// Completion Port 오브젝트 생성
	hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	
	
	// 최대 개수만큼 스레드 생성
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	for (int i = 0; i < sysInfo.dwNumberOfProcessors; ++i)
		Threads.emplace_back(std::thread(IOFunction,hComPort));

	// 서버 소켓 생성
	// AF_INET은 IPv4, SOCK_STREAM은 TCP를 의미
	hServSock = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;				
	servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAdr.sin_port = htons(atoi(argv[1]));	

	bind(hServSock, (SOCKADDR*)&servAdr, sizeof(servAdr));	// 주소 정보(IP주소,PORT번호) 할당
	listen(hServSock, 5);									// 서버 소켓이 클라이언트 연결 요청을 받아들일 수 있는 상태


	printf("Chatting Start\n");
	while (true)
	{
		SOCKET hClientSocket;
		SOCKADDR_IN ClientAddr;
		int addrLen = sizeof(ClientAddr);

		hClientSocket = accept(hServSock, (SOCKADDR*)&ClientAddr, &addrLen);	// 클라이언트에서의 연결 요청을 수락

		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClientSocket = hClientSocket;								// 클라이언트의 정보를 구조체에 담아 놓기
		memcpy(&(handleInfo->clntAdr), &ClientAddr, addrLen);


		// Completion Port를 소켓과 연결, 핸들 정보가 전달, handleInfo는 완료된 IO관련 정보의 전달을 위한 매개변수
		CreateIoCompletionPort((HANDLE)hClientSocket, hComPort, (DWORD)handleInfo, 0);

		// io 정보 생성하고 Receive
		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = bufSize;
		ioInfo->wsaBuf.buf = ioInfo->buffer;
		ioInfo->rwMode = RW_MODE::READ;
		ioInfo->refCount = 0;
		

		mtx.lock();
		// name 받기
		recv(handleInfo->hClientSocket, handleInfo->name, 30, 0);
		mClient.insert(std::make_pair(hClientSocket, handleInfo->name));
		printf("If you chat Quit, Program is end\n");
		mtx.unlock();

		

		// 비동기 입출력 시작
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
		// Completion Queue에서 Overlapped구조체 정보와 handle Info를 받아온다.
		GetQueuedCompletionStatus(hComPort, &bytesTrans, (LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE);
		sock = handleInfo->hClientSocket;

		if (ioInfo->rwMode == RW_MODE::READ)
		{
			if (bytesTrans == 0)
			{
				// 연결이 끊긴 경우, Client를 관리하는 map에사 문제가 생기면 안되므로 뮤텍스
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

			// Client Map을 도는 도중, 다른 클라이언트가 종료되면 안되므로 뮤텍스
			AccessMtx.lock();
			ioInfo->rwMode = RW_MODE::WRITE;
			for (auto& e : mClient)
			{
				// 여러 번의 Send요청을 Overlapped IO로 요청하기 때문에, 동일한 주소의 ioInfo를 여러 스레드에서 참조할 수 있다.
				// 따라서 레퍼런스 카운트를 관리
				++ioInfo->refCount;
				WSASend(e.first, (&ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
			}
			AccessMtx.unlock();


			memcpy(message, ioInfo->wsaBuf.buf, bufSize);
			message[bytesTrans] = '\0';
			strcpy_s(clientname, handleInfo->name);
			int nameLen = strlen(clientname);
			char printMessage[30];
			
			// nameLen은 이름, 2는 -> " :" 없애기 위해
			strncpy_s(printMessage, message + nameLen + 2, strlen(message) - nameLen);

			printf("%s : %s", handleInfo->name, printMessage);

			// 새로 생성하고 대기
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

			// 마지막 스레드
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
