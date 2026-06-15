#include "pch.h"
#include "menu_handler.h"
#include "menu_ipc_client.h"
#include "spdlog_ipc_sink.h"


MenuHandler::MenuHandler(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<MenuIPCClient> IPCClient)
	: m_dllModule(dllModule)
	, m_configManager(configManager)
	, m_IPCClient(IPCClient)
	, m_clientData()
{
	m_logIPCSink = std::make_shared<spdlog_ipc_sink_mt>(m_IPCClient, 32);
	m_logIPCSink->set_pattern("%v");
	g_logSinkAggregator->add_sink(m_logIPCSink);
}

MenuHandler::~MenuHandler()
{
	if (g_logSinkAggregator.get())
	{
		g_logSinkAggregator->remove_sink(m_logIPCSink);
	}
}

void MenuHandler::DispatchClientDataValues()
{
	m_bHasClientData = true;
	MenuIPCMessage message = {};
	message.Header.Type = MessageType_SetClientDataValues;
	message.Header.PayloadSize = sizeof(ClientDataValues); 
	memcpy(message.Payload, &m_clientData.Values, sizeof(ClientDataValues));
	m_IPCClient->WriteMessage(message, false);

	g_logger->flush();
}

void MenuHandler::DispatchApplicationModuleName()
{
	m_bHasApplicationModuleName = true;
	MenuIPCMessage message = {};
	message.Header.Type = MessageType_SetAppModuleName;
	int length = static_cast<int>(min(m_clientData.ApplicationModuleName.length() + 1, IPC_PAYLOAD_SIZE));
	message.Header.PayloadSize = length;
	memcpy(message.Payload, m_clientData.ApplicationModuleName.data(), length);
	m_IPCClient->WriteMessage(message, true);
}

void MenuHandler::DispatchApplicationName()
{
	m_bHasApplicationName = true;
	MenuIPCMessage message = {};
	message.Header.Type = MessageType_SetAppName;
	int length = static_cast<int>(min(m_clientData.ApplicationName.length() + 1, IPC_PAYLOAD_SIZE));
	message.Header.PayloadSize = length;
	memcpy(message.Payload, m_clientData.ApplicationName.data(), length);
	m_IPCClient->WriteMessage(message, true);
}

void MenuHandler::DispatchEngineName()
{
	m_bHasEngineName = true;
	MenuIPCMessage message = {};
	message.Header.Type = MessageType_SetAppEngineName;
	int length = static_cast<int>(min(m_clientData.EngineName.length() + 1, IPC_PAYLOAD_SIZE));
	message.Header.PayloadSize = length;
	memcpy(message.Payload, m_clientData.EngineName.data(), length);
	m_IPCClient->WriteMessage(message, true);
}

bool inline CopyConfig(void* destination, MenuIPCMessage& message, size_t size)
{
	if (message.Header.PayloadSize == size)
	{
		memcpy(destination, message.Payload, size);
		return true;
	}
	else
	{
		g_logger->error("Incorrect payload size for config update: {}, expected {}", message.Header.PayloadSize, size);
		return false;
	}
}

void MenuHandler::MenuIPCConnectedToServer()
{
	if (m_bHasClientData)
	{
		DispatchClientDataValues();
	}

	if (m_bHasApplicationModuleName)
	{
		DispatchApplicationModuleName();
	}

	if (m_bHasApplicationName)
	{
		DispatchApplicationName();
	}

	if (m_bHasEngineName)
	{
		DispatchEngineName();
	}
}

void MenuHandler::MenuIPCMessageReceived(MenuIPCMessage& message, int clientIndex)
{
	//g_logger->info("IPC message received: type {}, len {}", message.Header.Type, message.Header.PayloadSize);

	switch (message.Header.Type)
	{
	case MessageType_KeepAlive:
		// Do nothing
		break;

	case MessageType_SendCommand_ApplyCameraParamChanges:

		m_configManager->SetCameraParamChangesPending();
		break;

	case MessageType_SendCommand_ApplyRendererReset:

		m_configManager->SetRendererResetPending();
		break;

	case MessageType_SendCommand_DumpFrameTexture:

		m_configManager->SetFrameTextureDumpPending();
		break;

	case MessageType_InformReloadConfigFile:

		m_configManager->ReadConfigFile();
		break;

	case MessageType_SendConfig_Main:

		if (!CopyConfig(&m_configManager->GetConfig_Main(), message, sizeof(Config_Main)))
		{
			// Read config file to update settings on invalid size
			m_configManager->ReadConfigFile();
		}
		
		break;

	case MessageType_SendConfig_Core:

		if (!CopyConfig(&m_configManager->GetConfig_Core(), message, sizeof(Config_Core)))
		{
			// Read config file to update settings on invalid size
			m_configManager->ReadConfigFile();
		}

		break;

	case MessageType_SendConfig_Extensions:

		if (!CopyConfig(&m_configManager->GetConfig_Extensions(), message, sizeof(Config_Extensions)))
		{
			// Read config file to update settings on invalid size
			m_configManager->ReadConfigFile();
		}

		break;

	case MessageType_SendConfig_Stereo:

		if (!CopyConfig(&m_configManager->GetConfig_CustomStereo(), message, sizeof(Config_Stereo)))
		{
			// Read config file to update settings on invalid size
			m_configManager->ReadConfigFile();
		}

		if (m_configManager->GetConfig_Main().StereoPreset == StereoPreset_Custom)
		{
			m_configManager->GetConfig_Stereo() = m_configManager->GetConfig_CustomStereo();
		}

		break;

	case MessageType_SendConfig_Depth:

		if (!CopyConfig(&m_configManager->GetConfig_Depth(), message, sizeof(Config_Depth)))
		{
			// Read config file to update settings on invalid size
			m_configManager->ReadConfigFile();
		}

		break;

	case MessageType_SendConfig_Camera:
	{
		if (!CopyConfig(&m_configManager->GetConfig_Camera(), message, sizeof(Config_Camera)))
		{
			// Read config file to update settings on invalid size
			m_configManager->ReadConfigFile();
		}

		break;
	}

	default:
		g_logger->info("Unhandled IPC message received: type {}, len {}", static_cast<int32_t>(message.Header.Type), message.Header.PayloadSize);
	}
	
}

