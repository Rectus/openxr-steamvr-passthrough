
#include "pch.h"
#include "menu_ipc_client.h"

MenuIPCClient::MenuIPCClient()
{
	m_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (m_event == INVALID_HANDLE_VALUE || m_event == NULL)
	{ 
		g_logger->error("Failed to create IPC event!");
		return; 
	}

	m_readOverlap.hEvent = m_event;
	m_writeOverlap.hEvent = m_event;

	m_bRunThread = true;
	m_listenThread = std::thread(&MenuIPCClient::Listen, this);
}

MenuIPCClient::~MenuIPCClient()
{
	m_bRunThread = false;
	if (m_event != NULL && m_event != INVALID_HANDLE_VALUE)
	{
		SetEvent(m_event);
	}

	if (m_listenThread.joinable())
	{
		m_listenThread.join();
	}

	if (m_event != NULL && m_event != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_event);
	}
}

bool MenuIPCClient::RegisterReader(std::weak_ptr<IMenuIPCReader> callback)
{
	if (callback.expired())
	{
		return false;
	}
	m_callback = callback;
	return true;
}

bool MenuIPCClient::WriteMessage(MenuIPCMessage& message, bool bAllowBlocking)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_pipe == NULL || m_pipe == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	if (m_bWritePending)
	{
		if (!bAllowBlocking)
		{
			return false;
		}

		std::this_thread::yield();

		bool bSucceeded = false;
		for (int i = 0; i < 10; i++)
		{
			if (!m_bWritePending)
			{
				bSucceeded = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		if (!bSucceeded || m_pipe == NULL || m_pipe == INVALID_HANDLE_VALUE)
		{
			g_logger->error("IPC WriteMessage: Write timed out!");
			return false;
		}
	}

	if (message.Header.PayloadSize > IPC_PAYLOAD_SIZE)
	{
		g_logger->error("IPC WriteMessage: Payload too large!");
		return false;
	}

	m_writeSize = IPC_HEADER_SIZE + message.Header.PayloadSize;
	memcpy(m_writeBuffer, &message, m_writeSize);

	DWORD bytesWritten = 0;

	bool bSuccess = WriteFile(m_pipe, m_writeBuffer, m_writeSize, &bytesWritten, &m_writeOverlap);
	DWORD error = GetLastError();
	if (bSuccess)
	{
		if (m_writeSize != bytesWritten)
		{
			g_logger->error("IPC WriteMessage: Incorrect bytes written: {}, expected {}", m_writeSize, bytesWritten);
			return false;
		}
	}
	else if (error == ERROR_IO_PENDING || error == ERROR_IO_INCOMPLETE)
	{
		m_bWritePending = true;
	}
	else if (error == ERROR_BROKEN_PIPE)
	{
		g_logger->info("IPC disconnected from menu server");
		ClosePipe();
		return false;
	}
	else
	{
		g_logger->error("IPC WriteMessage: WriteFile error: {}", error);
		return false;
	}

	return true;
}

void MenuIPCClient::Listen()
{
	while (m_bRunThread)
	{
		if (m_pipe == NULL || m_pipe == INVALID_HANDLE_VALUE)
		{
			if (OpenPipe())
			{
				g_logger->info("IPC connected to menu server");
				m_bReadPending = false;
				m_bWritePending = false;

				if (auto callback = m_callback.lock())
				{
					callback->MenuIPCConnectedToServer();
				}
				
				CueRead();
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}
		}

		DWORD result = WaitForSingleObject(m_event, 100);
		if (result == WAIT_TIMEOUT && !m_bWritePending)
		{
			MenuIPCMessage message = {};
			message.Header.Type = MessageType_KeepAlive;
			WriteMessage(message, false);
			continue;
		}
		else if (result != WAIT_OBJECT_0)
		{
			
			continue;
		}

		if (!m_bRunThread) { break; }

		if (m_pipe == NULL) // If a write closed the pipe.
		{
			continue;
		}

		std::lock_guard<std::mutex> lock(m_mutex);

		DWORD numBytes = 0;

	
		if (m_bReadPending)
		{
			bool bSuccess = GetOverlappedResult(m_pipe, &m_readOverlap, &numBytes, FALSE);
			DWORD error = GetLastError();

			if (bSuccess)
			{
				m_bReadPending = false;

				int offset = 0;

				while (numBytes > 0)
				{
					auto message = reinterpret_cast<MenuIPCMessage*>(m_readBuffer + offset);
					uint32_t messageSize = IPC_HEADER_SIZE + message->Header.PayloadSize;

					if (numBytes < IPC_HEADER_SIZE)
					{
						g_logger->error("Incomplete IPC message: size {}, expected {}", numBytes, messageSize);
						break;
					}
					else if (memcmp(message->Header.Magic, MENU_IPC_MAGIG_STR, 4) != 0 || message->Header.Version != MENU_IPC_VERSION || message->Header.Type >= MessageType_MAX || message->Header.Type <= MessageType_Invalid)
					{
						g_logger->error("Invalid IPC message header!");
						break;
					}
					else if (numBytes < messageSize)
					{
						g_logger->error("Invalid IPC message size: {}, expected {}, type {}", numBytes, messageSize, static_cast<int32_t>(message->Header.Type));
						break;
					}

					if (auto callback = m_callback.lock())
					{
						callback->MenuIPCMessageReceived(*message, 0);
					}
					offset += messageSize;
					numBytes -= messageSize;
				}

				CueRead();
			}
			else if (error == ERROR_IO_PENDING || error == ERROR_IO_INCOMPLETE)
			{
				// Still waiting
			}
			else if (error == ERROR_PIPE_NOT_CONNECTED)
			{
				ClosePipe();
				continue;
			}
			else if (error == ERROR_BROKEN_PIPE)
			{
				g_logger->info("IPC disconnected from menu server");
				ClosePipe();
				continue;
			}
			else
			{
				g_logger->error("IPC Pipe read error: {}", error);

				ClosePipe();
				continue;
			}
		}
		else
		{
			CueRead();
		}

		if (m_bWritePending)
		{
			bool bSuccess = GetOverlappedResult(m_pipe, &m_writeOverlap, &numBytes, FALSE);
			DWORD error = GetLastError();
			if (bSuccess)
			{
				if (numBytes != m_writeSize)
				{
					g_logger->error("Invalid IPC write size {}, expected {}", numBytes, m_writeSize);
				}

				m_bWritePending = false;
			}
			else if (error == ERROR_IO_PENDING || error == ERROR_IO_INCOMPLETE)
			{
				// Still waiting
			}
			else if (error == ERROR_BROKEN_PIPE)
			{
				g_logger->info("IPC disconnected from menu server");
				ClosePipe();
				continue;
			}
			else
			{
				g_logger->error("IPC Pipe write error: {}", error);

				ClosePipe();
				m_bWritePending = false;
				continue;
			}
		}
	}

	g_logger->info("IPC client shutting down...");
	ClosePipe();
}

bool MenuIPCClient::CueRead()
{
	DWORD numBytes = 0;

	bool result = ReadFile(m_pipe, m_readBuffer, IPC_BUFFER_SIZE, &numBytes, &m_readOverlap);
	if (result && numBytes > 0)
	{
		m_bReadPending = true;
		SetEvent(m_event);
		return true;
	}

	if (GetLastError() == ERROR_IO_PENDING)
	{
		m_bReadPending = true;
		return true;
	}

	g_logger->error("CueRead error {}", GetLastError());
	m_bReadPending = false;
	return false;
}

bool MenuIPCClient::OpenPipe()
{
	m_pipe = CreateFileW(IPC_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

	if (m_pipe != INVALID_HANDLE_VALUE && m_pipe != NULL)
	{
		m_bNoServerLogged = true;
		return true;
	}

	DWORD error = GetLastError();

	if (error == ERROR_FILE_NOT_FOUND) // No pipe open on server.
	{
		if (!m_bNoServerLogged)
		{
			m_bNoServerLogged = true;
			g_logger->info("IPC client: No menu server found on start");
		}

		return false;
	}

	if (GetLastError() == ERROR_PIPE_BUSY)
	{
		return false;
	}

	g_logger->error("IPC client: Failed to connect to menu sever! Error {}", error);

	return false;
}

bool MenuIPCClient::ClosePipe()
{
	if (m_pipe != INVALID_HANDLE_VALUE && m_pipe != NULL)
	{
		CloseHandle(m_pipe);
		m_pipe = NULL;

		return true;
	}
	return false;
}
