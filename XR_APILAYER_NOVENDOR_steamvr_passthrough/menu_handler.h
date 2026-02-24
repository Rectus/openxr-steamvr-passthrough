
#pragma once

#include "config_manager.h"
#include "menu_ipc.h"

class MenuIPCClient;
template<typename Mutex> class spdlog_ipc_sink;
using spdlog_ipc_sink_mt = spdlog_ipc_sink<std::mutex>;


class MenuHandler : public IMenuIPCReader
{
public:

	MenuHandler(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<MenuIPCClient> IPCClient);

	~MenuHandler();

	ClientData& GetClientData() { return m_clientData; }
	void DispatchClientDataValues();
	void DispatchApplicationModuleName();
	void DispatchApplicationName();
	void DispatchEngineName();

	virtual void MenuIPCConnectedToServer() override;
	virtual void MenuIPCMessageReceived(MenuIPCMessage& message, int clientIndex) override;

private:

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<MenuIPCClient> m_IPCClient;
	HMODULE m_dllModule;

	std::shared_ptr<spdlog_ipc_sink_mt> m_logIPCSink;

	ClientData m_clientData;
	bool m_bHasClientData = false;
	bool m_bHasApplicationModuleName = false;
	bool m_bHasApplicationName = false;
	bool m_bHasEngineName = false;
};

