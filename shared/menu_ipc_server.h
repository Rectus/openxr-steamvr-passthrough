
#pragma once

#include "menu_ipc.h"



struct ClientConnection
{
	HANDLE Pipe = NULL;
	OVERLAPPED ReadOverlap = {};
	OVERLAPPED WriteOverlap = {};
	uint8_t ReadBuffer[IPC_BUFFER_SIZE] = {};
	uint8_t WriteBuffer[IPC_BUFFER_SIZE] = {};
	int ReadSize = 0;
	int WriteSize = 0;
	std::atomic<bool> bConnected = false;
	std::atomic<bool> bReadPending = false;
	std::atomic<bool> bWritePending = false;
	std::atomic<bool> bShuttingDown = false;
	std::mutex Mutex;

	ClientConnection() {}
};


class MenuIPCServer
{
public:
	MenuIPCServer();
	~MenuIPCServer();

	bool RegisterReader(std::weak_ptr<IMenuIPCReader> callback);
	bool WriteMessage(MenuIPCMessage& message, int clientIndex);
	bool BroadcastMessage(MenuIPCMessage& message);

protected:
	void Listen();
	bool CueRead(int index);
	bool AddPipe();
	void RemovePipe(int index);

	std::thread m_listenThread;
	bool m_bRunThread = false;

	std::weak_ptr<IMenuIPCReader> m_callback;

	std::vector<std::unique_ptr<ClientConnection>> m_clientConnections;
	std::vector<HANDLE> m_events;
	std::shared_mutex m_connectionStateMutex;
};