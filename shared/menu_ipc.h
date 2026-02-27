
#pragma once

#define IPC_PIPE_NAME L"\\\\.\\pipe\\XR_APILAYER_NOVENDOR_steamvr_passthrough_menu_IPC"
#define MENU_IPC_VERSION 1
#define MENU_IPC_MAGIC ('X', 'R', 'X', 'R')

constexpr uint8_t MENU_IPC_MAGIG_STR[4] = { MENU_IPC_MAGIC };

enum MenuIPCMessageType
{
	MessageType_Invalid = 0,
	MessageType_KeepAlive,
	MessageType_Log,
	MessageType_SetAppModuleName,
	MessageType_SetAppName,
	MessageType_SetAppEngineName,
	MessageType_SetClientDataValues,
	MessageType_RequestConfig,
	MessageType_SendConfig_Main,
	MessageType_SendConfig_Camera,
	MessageType_SendConfig_Core,
	MessageType_SendConfig_Extensions,
	MessageType_SendConfig_Stereo,
	MessageType_SendConfig_Depth,
	MessageType_SendCommand_ApplyRendererReset,
	MessageType_SendCommand_ApplyCameraParamChanges,
	MessageType_SendCommand_DumpFrameTexture,
	MessageType_MAX
};

struct alignas(4) MenuIPCMessageHeader
{
	uint8_t Magic[4] = { MENU_IPC_MAGIC };
	uint32_t Version = MENU_IPC_VERSION;
	MenuIPCMessageType Type = MessageType_Invalid;
	int PayloadSize = 0;
};

#define IPC_BUFFER_SIZE 512
#define IPC_HEADER_SIZE (sizeof(MenuIPCMessageHeader))
#define IPC_PAYLOAD_SIZE (IPC_BUFFER_SIZE - IPC_HEADER_SIZE)

struct alignas(4) MenuIPCMessage
{
	MenuIPCMessageHeader Header{};
	uint8_t Payload[IPC_PAYLOAD_SIZE] = {0};
};

class IMenuIPCReader
{
public:
	virtual void MenuIPCClientConnected(int clientIndex) {}
	virtual void MenuIPCClientDisconnected(int clientIndex) {}
	virtual void MenuIPCConnectedToServer() {}
	virtual void MenuIPCMessageReceived(MenuIPCMessage& message, int clientIndex) = 0;
};