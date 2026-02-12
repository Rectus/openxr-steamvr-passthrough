
#pragma once

#include "menu_ipc.h"

class MenuIPCClient
{
public:

	MenuIPCClient();
	~MenuIPCClient();

	bool RegisterReader(std::weak_ptr<IMenuIPCReader> callback);
	bool WriteMessage(MenuIPCMessage& message, bool bAllowBlocking);

protected:
	void Listen();
	bool CueRead();
	bool OpenPipe();
	bool ClosePipe();

	std::thread m_listenThread;
	bool m_bRunThread = false;

	HANDLE m_pipe = NULL;
	OVERLAPPED m_readOverlap = {};
	OVERLAPPED m_writeOverlap = {};
	uint8_t m_readBuffer[IPC_BUFFER_SIZE] = {};
	uint8_t m_writeBuffer[IPC_BUFFER_SIZE] = {};
	int m_readSize = 0;
	int m_writeSize = 0;
	std::atomic<bool> m_bReadPending = false;
	std::atomic<bool> m_bWritePending = false;
	std::weak_ptr<IMenuIPCReader> m_callback;
	HANDLE m_event = NULL;
	std::mutex m_mutex;
	bool m_bNoServerLogged = false;
};