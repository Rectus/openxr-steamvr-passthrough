#include "pch.h"
#include "menu_handler.h"
#include "menu_ipc_client.h"


MenuHandler::MenuHandler(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<MenuIPCClient> IPCClient)
	: m_dllModule(dllModule)
	, m_configManager(configManager)
	, m_IPCClient(IPCClient)
	, m_displayValues()
{
	m_bRunThread = true;
	m_menuThread = std::thread(&MenuHandler::RunThread, this);
}

MenuHandler::~MenuHandler()
{
	m_bRunThread = false;
	if (m_menuThread.joinable())
	{
		m_menuThread.join();
	}
}

void MenuHandler::DispatchDisplayValues()
{
	m_bHasDisplayValues = true;
	MenuIPCMessage message = {};
	message.Header.Type = MessageType_SetDisplayValues;
	message.Header.PayloadSize = sizeof(MenuDisplayValues); 
	memcpy(message.Payload, &m_displayValues, sizeof(MenuDisplayValues));
	reinterpret_cast<MenuDisplayValues*>(&message.Payload)->currentApplication = {};
	m_IPCClient->WriteMessage(message, false);
}

void MenuHandler::DispatchApplicationName()
{
	m_bHasApplicationName = true;
	MenuIPCMessage message = {};
	message.Header.Type = MessageType_SetAppName;
	size_t length = min(m_displayValues.currentApplication.length(), IPC_PAYLOAD_SIZE - 1);
	message.Header.PayloadSize = length;
	memcpy(message.Payload, &m_displayValues.currentApplication, length);
	m_IPCClient->WriteMessage(message, true);
}

void inline CopyConfig(void* destination, MenuIPCMessage& message, size_t size)
{
	if (message.Header.PayloadSize == size)
	{
		memcpy(destination, message.Payload, size);
	}
	else
	{
		ErrorLog("Incorrect payload size for config update: %d, expected %d\n", message.Header.PayloadSize, message.Payload);
	}
}

void MenuHandler::MenuIPCConnectedToServer()
{
	if (m_bHasDisplayValues)
	{
		DispatchDisplayValues();
	}

	if (m_bHasApplicationName)
	{
		DispatchApplicationName();
	}
}

void MenuHandler::MenuIPCMessageReceived(MenuIPCMessage& message, int clientIndex)
{
	//Log("IPC message received: type %d, len %d\n", message.Header.Type, message.Header.PayloadSize);

	switch (message.Header.Type)
	{

	case MessageType_SendCommand_ApplyCameraParamChanges:

		m_configManager->SetCameraParamChangesPending();
		break;

	case MessageType_SendCommand_ApplyRendererReset:

		m_configManager->SetRendererResetPending();
		break;

	case MessageType_SendCommand_DumpFrameTexture:

		m_configManager->SetFrameTextureDumpPending();
		break;

	case MessageType_SendConfig_Main:

		CopyConfig(&m_configManager->GetConfig_Main(), message, sizeof(Config_Main));
		
		break;

	case MessageType_SendConfig_Core:

		CopyConfig(&m_configManager->GetConfig_Core(), message, sizeof(Config_Core));

		break;

	case MessageType_SendConfig_Extensions:

		CopyConfig(&m_configManager->GetConfig_Extensions(), message, sizeof(Config_Extensions));

		break;

	case MessageType_SendConfig_Stereo:

		CopyConfig(&m_configManager->GetConfig_CustomStereo(), message, sizeof(Config_Stereo));

		break;

	case MessageType_SendConfig_Depth:

		CopyConfig(&m_configManager->GetConfig_Depth(), message, sizeof(Config_Depth));

		break;

	case MessageType_SendConfig_Camera:
	{
		Config_Camera& config = m_configManager->GetConfig_Camera();
		std::string deviceSerialNumber = config.TrackedDeviceSerialNumber;
		CopyConfig(&config, message, sizeof(Config_Camera));
		config.TrackedDeviceSerialNumber = deviceSerialNumber;
		break;
	}

	case MessageType_SendConfig_Camera_TrackedDeviceSerialNumber:
	{
		if (strnlen_s(reinterpret_cast<const char*>(message.Payload), IPC_PAYLOAD_SIZE) < message.Header.PayloadSize)
		{
			m_configManager->GetConfig_Camera().TrackedDeviceSerialNumber = std::string(reinterpret_cast<const char*>(message.Payload));
		}
		else
		{
			ErrorLog("Invalid string for config update Camera_TrackedDeviceSerialNumber!\n");
		}
		break;
	}

	default:
		Log("Unhandled IPC message received: type %d, len %d\n", message.Header.Type, message.Header.PayloadSize);
	}
	
}

void MenuHandler::RunThread()
{
	while (m_bRunThread)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
