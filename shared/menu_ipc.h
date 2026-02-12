
#pragma once

#define IPC_PIPE_NAME L"\\\\.\\pipe\\XR_APILAYER_NOVENDOR_steamvr_passthrough_menu_IPC"

enum MenuIPCMessageType
{
	MessageType_SetAppName = 0,
	MessageType_SetDisplayValues,
	MessageType_RequestConfig,
	MessageType_SendConfig_Main,
	MessageType_SendConfig_Camera,
	MessageType_SendConfig_Camera_TrackedDeviceSerialNumber,
	MessageType_SendConfig_Core,
	MessageType_SendConfig_Extensions,
	MessageType_SendConfig_Stereo,
	MessageType_SendConfig_Depth,
	MessageType_SendCommand_ApplyRendererReset,
	MessageType_SendCommand_ApplyCameraParamChanges,
	MessageType_SendCommand_DumpFrameTexture
};

struct MenuIPCMessageHeader
{
	MenuIPCMessageType Type;
	int PayloadSize;
};

#define IPC_BUFFER_SIZE 1024
#define IPC_HEADER_SIZE (sizeof(MenuIPCMessageHeader))
#define IPC_PAYLOAD_SIZE (IPC_BUFFER_SIZE - IPC_HEADER_SIZE)

struct MenuIPCMessage
{
	MenuIPCMessageHeader Header;
	uint8_t Payload[IPC_PAYLOAD_SIZE];
};

class IMenuIPCReader
{
public:
	virtual void MenuIPCClientConnected(int clientIndex) {}
	virtual void MenuIPCClientDisconnected(int clientIndex) {}
	virtual void MenuIPCConnectedToServer() {}
	virtual void MenuIPCMessageReceived(MenuIPCMessage& message, int clientIndex) = 0;
};