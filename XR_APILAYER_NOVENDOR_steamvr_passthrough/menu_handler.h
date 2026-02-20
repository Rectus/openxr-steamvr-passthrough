
#pragma once

#include "config_manager.h"
#include "menu_ipc.h"

class MenuIPCClient;



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

	void RunThread();

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<MenuIPCClient> m_IPCClient;
	HMODULE m_dllModule;

	std::thread m_menuThread;
	bool m_bRunThread;

	ClientData m_clientData;
	bool m_bHasClientData = false;
	bool m_bHasApplicationModuleName = false;
	bool m_bHasApplicationName = false;
	bool m_bHasEngineName = false;
};

