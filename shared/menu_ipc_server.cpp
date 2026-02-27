
#include "pch.h"
#include "menu_ipc_server.h"

#define CLIENT_TIMEOUT_MS 5000

MenuIPCServer::MenuIPCServer()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	m_counterFreq = freq.QuadPart;

	HANDLE shutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (shutdownEvent == INVALID_HANDLE_VALUE || shutdownEvent == NULL) 
	{ 
		g_logger->error("Critical: failed to create IPC event!");
		abort(); 
	}

	m_events.push_back(shutdownEvent);

	m_bRunThread = true;
	m_listenThread = std::thread(&MenuIPCServer::Listen, this);
	g_logger->info("Menu IPC Server: Started");
}

MenuIPCServer::~MenuIPCServer()
{
	m_bRunThread = false;

	if (m_events[0] != NULL && m_events[0] != INVALID_HANDLE_VALUE)
	{
		SetEvent(m_events[0]);
	}
	
	
	if (m_listenThread.joinable())
	{
		m_listenThread.join();
	}

	if (m_events[0] != NULL && m_events[0] != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_events[0]);
	}
}

bool MenuIPCServer::RegisterReader(std::weak_ptr<IMenuIPCReader> callback)
{
	if (callback.expired())
	{
		return false;
	}
	m_callback = callback;
	return true;
}

bool MenuIPCServer::WriteMessage(MenuIPCMessage& message, int clientIndex)
{
	if (message.Header.PayloadSize > IPC_PAYLOAD_SIZE)
	{
		g_logger->error("Menu IPC Server: WriteMessage: Payload too large!");
		return false;
	}

	std::shared_lock accessLock(m_connectionStateMutex);

	if (m_clientConnections.size() <= clientIndex || !m_clientConnections[clientIndex]->bConnected)
	{
		g_logger->error("Menu IPC Server: WriteMessage: Invalid client!");
		return false;
	}

	ClientConnection* connection = m_clientConnections[clientIndex].get();

	if (connection->bWritePending)
	{
		g_logger->error("Menu IPC Server: WriteMessage: Write already pending!");
		return false;
	}

	if (message.Header.PayloadSize > IPC_PAYLOAD_SIZE)
	{
		g_logger->error("Menu IPC Server: WriteMessage: Payload too large!");
		return false;
	}

	int writeSize = IPC_HEADER_SIZE + message.Header.PayloadSize;
	connection->WriteSize = writeSize;
	memcpy(connection->WriteBuffer, &message, writeSize);

	DWORD bytesWritten = 0;

	bool bSucceeded = WriteFile(connection->Pipe, connection->WriteBuffer, writeSize, &bytesWritten, &connection->WriteOverlap);
	DWORD error = GetLastError();

	if (bSucceeded)
	{
		if (writeSize != bytesWritten)
		{
			g_logger->error("Menu IPC Server: WriteMessage: Incorrect bytes written: {}, expected {}", writeSize, bytesWritten);
			return false;
		}		
	}
	else if (error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE)
	{
		g_logger->info("Menu IPC Server: Client {} disconnected.", clientIndex);
		connection->bConnected = false;
		connection->bShuttingDown = true;
		SetEvent(connection->ReadOverlap.hEvent);
		return false;
	}
	else if (error == ERROR_IO_PENDING)
	{
		connection->bWritePending = true;
	}
	else
	{
		g_logger->error("Menu IPC Server: WriteMessage: WriteFile error: {}", error);
		return false;
	}
	return true;
}

bool MenuIPCServer::BroadcastMessage(MenuIPCMessage& message)
{
	if (message.Header.PayloadSize > IPC_PAYLOAD_SIZE)
	{
		g_logger->error("Menu IPC Server: BroadcastMessage: Payload too large!");
		return false;
	}
	int writeSize = IPC_HEADER_SIZE + message.Header.PayloadSize;

	std::shared_lock accessLock(m_connectionStateMutex);

	int clIdx = 0;

	for (std::unique_ptr<ClientConnection>& connection : m_clientConnections)
	{
		clIdx++;

		if (!connection->bConnected)
		{
			continue;
		}

		// Block with timeout if busy
		if (connection->bWritePending)
		{
			std::this_thread::yield();

			bool bSucceeded = false;
			for(int i = 0; i < 10; i++)
			{
				if (!connection->bWritePending)
				{
					bSucceeded = true;
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			if (!bSucceeded || !connection->bConnected)
			{
				continue;
			}
		}
		
		connection->WriteSize = writeSize;
		memcpy(connection->WriteBuffer, &message, writeSize);

		DWORD bytesWritten = 0;

		bool bSucceeded = WriteFile(connection->Pipe, connection->WriteBuffer, writeSize, &bytesWritten, &connection->WriteOverlap);
		DWORD error = GetLastError();

		if(bSucceeded)
		{
			if (writeSize != bytesWritten)
			{
				g_logger->error("Menu IPC Server: BroadcastMessage: Incorrect bytes written: {}, expected {}", writeSize, bytesWritten);
			}
		}
		else if (error == ERROR_IO_PENDING)
		{
			connection->bWritePending = true;
		}
		else if (error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE)
		{
			g_logger->info("Menu IPC Server: Client {} disconnected", clIdx);
			connection->bConnected = false;
			connection->bShuttingDown = true;
			SetEvent(connection->ReadOverlap.hEvent);
		}
		else
		{
			g_logger->error("Menu IPC Server: BroadcastMessage: WriteFile error: {}", GetLastError());
		}
	}
	
	return true;
}

void MenuIPCServer::Listen()
{
	if (!AddPipe())
	{
		m_bRunThread = false;
	}

	while (m_bRunThread)
	{
		DWORD eventIndex = WaitForMultipleObjects((DWORD)m_events.size(), m_events.data(), FALSE, 100);

		if (eventIndex == WAIT_TIMEOUT)
		{
			continue;
		}
		else if (eventIndex >= m_events.size())
		{
			g_logger->error("Menu IPC Server: WaitForMultipleObjects failure: {}", eventIndex);
			m_bRunThread = false;
			break;
		}
		else if (eventIndex == 0)
		{
			m_bRunThread = false;
			break;
		}

		LARGE_INTEGER time;
		QueryPerformanceCounter(&time);
		uint64_t currentTime = time.QuadPart;

		// Check for timeouts
		for (int i = 0; i < m_clientConnections.size(); i++)
		{
			std::unique_lock<std::mutex> clientLock(m_clientConnections[i]->Mutex);

			if (m_clientConnections[i]->bConnected &&
				currentTime > m_clientConnections[i]->LastMessageTime +
				(CLIENT_TIMEOUT_MS * m_counterFreq / 1000))
			{
				g_logger->info("Menu IPC Server: Client {} timed out", i);
				m_clientConnections[i]->bConnected = false;
				m_clientConnections[i]->bShuttingDown = true;
			}
		}

		DWORD clIdx = eventIndex - 1;	

		if (m_clientConnections[clIdx]->bShuttingDown)
		{
			RemovePipe(clIdx);

			if (m_numIdlePipes < 1)
			{
				AddPipe();
			}

			continue;
		}

		std::unique_lock<std::mutex> clientLock(m_clientConnections[clIdx]->Mutex);

		DWORD numBytes = 0;

		if (!m_clientConnections[clIdx]->bConnected)
		{
			bool bSuccess = GetOverlappedResult(m_clientConnections[clIdx]->Pipe, &m_clientConnections[clIdx]->ReadOverlap, &numBytes, FALSE);
			DWORD error = GetLastError();

			if (bSuccess)
			{
				g_logger->info("Menu IPC Server: Client connected to pipe {}", clIdx);
				m_clientConnections[clIdx]->bConnected = true;
				m_clientConnections[clIdx]->LastMessageTime = currentTime;
				m_numIdlePipes--;
				CueRead(clIdx);

				// Add another pipe to wait for new connections
				if (m_numIdlePipes < 1)
				{
					AddPipe();
				}

				if (auto callback = m_callback.lock())
				{
					callback->MenuIPCClientConnected(clIdx);
				}
			}
			else if (error == ERROR_BROKEN_PIPE)
			{
				g_logger->error("Menu IPC Server: Client {} failed to connect!", clIdx);
				m_clientConnections[clIdx]->bConnected = false;
				m_clientConnections[clIdx]->bShuttingDown = true;
				SetEvent(m_clientConnections[clIdx]->ReadOverlap.hEvent);
				continue;
			}
			else if (error != ERROR_IO_PENDING)
			{
				g_logger->error("Menu IPC Server: Pipe error: {}", error);
				m_clientConnections[clIdx]->bConnected = false;
				m_clientConnections[clIdx]->bShuttingDown = true;
				SetEvent(m_clientConnections[clIdx]->ReadOverlap.hEvent);
			}

			continue;
		}

		if (m_clientConnections[clIdx]->bReadPending)
		{
			bool bSuccess = GetOverlappedResult(m_clientConnections[clIdx]->Pipe, &m_clientConnections[clIdx]->ReadOverlap, &numBytes, FALSE);
			DWORD error = GetLastError();

			if (bSuccess)
			{
				m_clientConnections[clIdx]->bReadPending = false;
				m_clientConnections[clIdx]->LastMessageTime = currentTime;

				int offset = 0;
				bool bReply = false;

				while (numBytes > 0)
				{
					auto message = reinterpret_cast<MenuIPCMessage*>(m_clientConnections[clIdx]->ReadBuffer + offset);
					uint32_t messageSize = IPC_HEADER_SIZE + message->Header.PayloadSize;

					if (numBytes < IPC_HEADER_SIZE)
					{
						g_logger->error("Menu IPC Server: Incomplete IPC message: size {}, expected {}", numBytes, messageSize);
						break;
					}
					else if (memcmp(message->Header.Magic, MENU_IPC_MAGIG_STR, 4) != 0 || message->Header.Version != MENU_IPC_VERSION || message->Header.Type >= MessageType_MAX || message->Header.Type <= MessageType_Invalid)
					{
						g_logger->error("Menu IPC Server: Invalid IPC message header!");
						break;
					}
					else if (message->Header.Type == MessageType_KeepAlive && !m_clientConnections[clIdx]->bWritePending)
					{
						bReply = true;
					}
					else if (numBytes < messageSize)
					{
						g_logger->error("Menu IPC Server: Invalid IPC message size: {}, expected {}, type {}", numBytes, messageSize, static_cast<int32_t>(message->Header.Type));
						break;
					}
					else if (auto callback = m_callback.lock())
					{
						callback->MenuIPCMessageReceived(*message, clIdx);
					}
					offset += messageSize;
					numBytes -= messageSize;
				}

				if (bReply)
				{
					MenuIPCMessage sendMessage = {};
					sendMessage.Header.Type = MessageType_KeepAlive;
					clientLock.unlock();
					WriteMessage(sendMessage, clIdx);
					CueRead(clIdx);
					continue;
				}

				CueRead(clIdx);
			}
			else if (error == ERROR_IO_PENDING || error == ERROR_IO_INCOMPLETE)
			{
				// Still waiting
			}
			else if (error == ERROR_BROKEN_PIPE)
			{
				g_logger->info("Menu IPC Server: Client {} disconnected", clIdx);
				m_clientConnections[clIdx]->bConnected = false;
				m_clientConnections[clIdx]->bShuttingDown = true;
				SetEvent(m_clientConnections[clIdx]->ReadOverlap.hEvent);
				continue;
			}
			else
			{
				g_logger->error("Menu IPC Server: Pipe read error: {}", error);
				m_clientConnections[clIdx]->bConnected = false;
				m_clientConnections[clIdx]->bShuttingDown = true;
				SetEvent(m_clientConnections[clIdx]->ReadOverlap.hEvent);
				continue;
			}
		}
		else
		{
			CueRead(clIdx);
		}

		if (m_clientConnections[clIdx]->bWritePending)
		{
			bool bSuccess = GetOverlappedResult(m_clientConnections[clIdx]->Pipe, &m_clientConnections[clIdx]->WriteOverlap, &numBytes, FALSE);
			DWORD error = GetLastError();

			if (bSuccess)
			{
				if (numBytes != m_clientConnections[clIdx]->WriteSize)
				{
					g_logger->error("Menu IPC Server: Wrong number of bytes written to pipe: {}, expected {}", numBytes, m_clientConnections[clIdx]->WriteSize);
				}
				m_clientConnections[clIdx]->bWritePending = false;

			}
			else if (error == ERROR_IO_PENDING || error == ERROR_IO_INCOMPLETE)
			{
				// Still waiting
			}
			else if (error == ERROR_BROKEN_PIPE)
			{
				g_logger->info("Menu IPC Server: Client {} disconnected", clIdx);
				m_clientConnections[clIdx]->bConnected = false;
				m_clientConnections[clIdx]->bShuttingDown = true;
				SetEvent(m_clientConnections[clIdx]->ReadOverlap.hEvent);
				continue;
			}
			else
			{
				g_logger->error("Menu IPC Server: Pipe write error: {}", error);
				m_clientConnections[clIdx]->bConnected = false;
				m_clientConnections[clIdx]->bShuttingDown = true;
				m_clientConnections[clIdx]->bWritePending = false;
				SetEvent(m_clientConnections[clIdx]->ReadOverlap.hEvent);
				continue;
			}
		}
	}

	g_logger->info("Menu IPC Server: Shutting down...");

	for (int i = static_cast<int>(m_clientConnections.size()) - 1; i >= 0; i--)
	{
		RemovePipe(i);
	}
}

bool MenuIPCServer::CueRead(int index)
{
	DWORD numBytes = 0;

	bool result = ReadFile(m_clientConnections[index]->Pipe, m_clientConnections[index]->ReadBuffer, IPC_BUFFER_SIZE, &numBytes, &m_clientConnections[index]->ReadOverlap);
	if (result && numBytes > 0)
	{
		m_clientConnections[index]->bReadPending = true;
		SetEvent(m_clientConnections[index]->ReadOverlap.hEvent);
		return true;
	}

	if (GetLastError() == ERROR_IO_PENDING)
	{
		m_clientConnections[index]->bReadPending = true;
		return true;
	}
	g_logger->error("Menu IPC Server: ReadFile failed: {}", GetLastError());
	return false;
}

bool MenuIPCServer::AddPipe()
{
	std::unique_lock editLock(m_connectionStateMutex);

	HANDLE pipe = CreateNamedPipeW(IPC_PIPE_NAME,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
		PIPE_UNLIMITED_INSTANCES,
		IPC_BUFFER_SIZE, IPC_BUFFER_SIZE, 0, NULL);

	if (pipe == INVALID_HANDLE_VALUE)
	{
		LPSTR messageBuffer;

		DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)&messageBuffer, 0, NULL);

		g_logger->error("Menu IPC Server: CreateNamedPipeW failed: {}", messageBuffer);

		LocalFree(messageBuffer);

		return false;
	}

	m_clientConnections.push_back(std::make_unique<ClientConnection>());
	std::unique_ptr<ClientConnection>& connection = m_clientConnections.back();

	connection->ReadOverlap.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
	connection->WriteOverlap.hEvent = connection->ReadOverlap.hEvent;

	if (connection->ReadOverlap.hEvent == INVALID_HANDLE_VALUE || connection->ReadOverlap.hEvent == NULL)
	{
		m_clientConnections.pop_back();
		g_logger->error("Menu IPC Server: CreateEvent failed");
		return false;
	}

	if (ConnectNamedPipe(pipe, &connection->ReadOverlap) == 0)
	{
		DWORD ret = GetLastError();

		m_events.push_back(connection->ReadOverlap.hEvent);
		connection->Pipe = pipe;
		connection->bConnected = false;

		if (ret == (ERROR_PIPE_CONNECTED))
		{
			SetEvent(connection->ReadOverlap.hEvent);
		}
		else
		{
			m_numIdlePipes++;
		}
		return true;
	}

	m_clientConnections.pop_back();

	g_logger->error("Menu IPC Server: Pipe connection failed");

	return false;
}

void MenuIPCServer::RemovePipe(int index)
{
	std::unique_lock editLock(m_connectionStateMutex);
	DisconnectNamedPipe(m_clientConnections[index]->Pipe);

	CloseHandle(m_clientConnections[index]->Pipe);
	CloseHandle(m_events[index + 1]);

	m_clientConnections.erase(m_clientConnections.begin() + index);
	m_events.erase(m_events.begin() + index + 1);

	if (auto callback = m_callback.lock())
	{
		callback->MenuIPCClientDisconnected(index);
	}
}

