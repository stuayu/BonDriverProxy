#define _CRT_SECURE_NO_WARNINGS
#include "BonDriverProxy.h"

#define STRICT_LOCK

#if _DEBUG
#define DETAILLOG	0
#endif

#ifdef HAVE_UI
#define WM_TASKTRAY			(WM_USER + 1)
#define ID_TASKTRAY			0
#define ID_TASKTRAY_SHOW	1
#define ID_TASKTRAY_HIDE	2
#define ID_TASKTRAY_RELOAD	3
#define ID_TASKTRAY_EXIT	4
HINSTANCE g_hInstance;
HWND g_hWnd;
HMENU g_hMenu;
#endif

static int Init(HMODULE hModule)
{
	char szIniPath[MAX_PATH + 16] = { '\0' };
	GetModuleFileNameA(hModule, szIniPath, MAX_PATH);
	char *p = strrchr(szIniPath, '.');
	if (!p)
		return -1;
	p++;
	strcpy(p, "ini");

	HANDLE hFile = CreateFileA(szIniPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return -2;
	CloseHandle(hFile);

	GetPrivateProfileStringA("OPTION", "ADDRESS", "127.0.0.1", g_Host, sizeof(g_Host), szIniPath);
	GetPrivateProfileStringA("OPTION", "PORT", "1192", g_Port, sizeof(g_Port), szIniPath);
	g_SandBoxedRelease = GetPrivateProfileIntA("OPTION", "SANDBOXED_RELEASE", 0, szIniPath);
	g_DisableUnloadBonDriver = GetPrivateProfileIntA("OPTION", "DISABLE_UNLOAD_BONDRIVER", 0, szIniPath);

	g_PacketFifoSize = GetPrivateProfileIntA("SYSTEM", "PACKET_FIFO_SIZE", 64, szIniPath);
	g_TsPacketBufSize = GetPrivateProfileIntA("SYSTEM", "TSPACKET_BUFSIZE", (188 * 1024), szIniPath);

	char szPriority[128];
	GetPrivateProfileStringA("SYSTEM", "PROCESSPRIORITY", "NORMAL", szPriority, sizeof(szPriority), szIniPath);
	if (strcmp(szPriority, "REALTIME") == 0)
		g_ProcessPriority = REALTIME_PRIORITY_CLASS;
	else if (strcmp(szPriority, "HIGH") == 0)
		g_ProcessPriority = HIGH_PRIORITY_CLASS;
	else if (strcmp(szPriority, "ABOVE_NORMAL") == 0)
		g_ProcessPriority = ABOVE_NORMAL_PRIORITY_CLASS;
	else if (strcmp(szPriority, "BELOW_NORMAL") == 0)
		g_ProcessPriority = BELOW_NORMAL_PRIORITY_CLASS;
	else if (strcmp(szPriority, "IDLE") == 0)
		g_ProcessPriority = IDLE_PRIORITY_CLASS;
	else
		g_ProcessPriority = NORMAL_PRIORITY_CLASS;
	SetPriorityClass(GetCurrentProcess(), g_ProcessPriority);

	GetPrivateProfileStringA("SYSTEM", "THREADPRIORITY_TSREADER", "NORMAL", szPriority, sizeof(szPriority), szIniPath);
	if (strcmp(szPriority, "CRITICAL") == 0)
		g_ThreadPriorityTsReader = THREAD_PRIORITY_TIME_CRITICAL;
	else if (strcmp(szPriority, "HIGHEST") == 0)
		g_ThreadPriorityTsReader = THREAD_PRIORITY_HIGHEST;
	else if (strcmp(szPriority, "ABOVE_NORMAL") == 0)
		g_ThreadPriorityTsReader = THREAD_PRIORITY_ABOVE_NORMAL;
	else if (strcmp(szPriority, "BELOW_NORMAL") == 0)
		g_ThreadPriorityTsReader = THREAD_PRIORITY_BELOW_NORMAL;
	else if (strcmp(szPriority, "LOWEST") == 0)
		g_ThreadPriorityTsReader = THREAD_PRIORITY_LOWEST;
	else if (strcmp(szPriority, "IDLE") == 0)
		g_ThreadPriorityTsReader = THREAD_PRIORITY_IDLE;
	else
		g_ThreadPriorityTsReader = THREAD_PRIORITY_NORMAL;

	GetPrivateProfileStringA("SYSTEM", "THREADPRIORITY_SENDER", "NORMAL", szPriority, sizeof(szPriority), szIniPath);
	if (strcmp(szPriority, "CRITICAL") == 0)
		g_ThreadPrioritySender = THREAD_PRIORITY_TIME_CRITICAL;
	else if (strcmp(szPriority, "HIGHEST") == 0)
		g_ThreadPrioritySender = THREAD_PRIORITY_HIGHEST;
	else if (strcmp(szPriority, "ABOVE_NORMAL") == 0)
		g_ThreadPrioritySender = THREAD_PRIORITY_ABOVE_NORMAL;
	else if (strcmp(szPriority, "BELOW_NORMAL") == 0)
		g_ThreadPrioritySender = THREAD_PRIORITY_BELOW_NORMAL;
	else if (strcmp(szPriority, "LOWEST") == 0)
		g_ThreadPrioritySender = THREAD_PRIORITY_LOWEST;
	else if (strcmp(szPriority, "IDLE") == 0)
		g_ThreadPrioritySender = THREAD_PRIORITY_IDLE;
	else
		g_ThreadPrioritySender = THREAD_PRIORITY_NORMAL;

	OSVERSIONINFOEXA osvi = {};
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwMajorVersion = 6;	// >= Vista
	if (VerifyVersionInfoA(&osvi, VER_MAJORVERSION, VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL)))
		g_ThreadExecutionState = ES_SYSTEM_REQUIRED | ES_CONTINUOUS | ES_AWAYMODE_REQUIRED;
	else
		g_ThreadExecutionState = ES_SYSTEM_REQUIRED | ES_CONTINUOUS;

	return 0;
}

#if defined(HAVE_UI) || defined(BUILD_AS_SERVICE)
static void ShutdownInstances()
{
	// �V���b�g�_�E���C�x���g�g���K
	if (!g_ShutdownEvent.IsSet())
		g_ShutdownEvent.Set();

	// �܂��҂��󂯃X���b�h�̏I����҂�
	g_Lock.Enter();
	if (g_hListenThread != NULL)
	{
		WaitForSingleObject(g_hListenThread, INFINITE);
		CloseHandle(g_hListenThread);
		g_hListenThread = NULL;
	}
	g_Lock.Leave();

	// �S�N���C�A���g�C���X�^���X�̏I����҂�
	for (;;)
	{
		// g_InstanceList�̐��m�F�ł킴�킴���b�N���Ă�̂́AcProxyServer�C���X�^���X��
		// "���X�g����͍폜����Ă��Ă��f�X�g���N�^���I�����Ă��Ȃ�"��Ԃ�r�������
		g_Lock.Enter();
		size_t num = g_InstanceList.size();
		g_Lock.Leave();
		if (num == 0)
			break;
		Sleep(10);
	}

	// �V���b�g�_�E���C�x���g�N���A
	g_ShutdownEvent.Reset();
}
#endif

static void CleanUp()
{
	if (g_DisableUnloadBonDriver)
	{
		while (!g_LoadedDriverList.empty())
		{
			stLoadedDriver *pLd = g_LoadedDriverList.front();
			g_LoadedDriverList.pop_front();
			FreeLibrary(pLd->hModule);
#if _DEBUG
			_RPT1(_CRT_WARN, "[%s] unloaded\n", pLd->strBonDriver);
#endif
			delete pLd;
		}
	}
}

cProxyServer::cProxyServer() : m_Error(TRUE, FALSE)
{
	m_s = INVALID_SOCKET;
	m_hModule = NULL;
	m_pIBon = m_pIBon2 = m_pIBon3 = NULL;
	m_strBonDriver[0] = '\0';
	m_bTunerOpen = FALSE;
	m_bChannelLock = 0;
	m_hTsRead = NULL;
	m_pTsReaderArg = NULL;
}

cProxyServer::~cProxyServer()
{
	LOCK(g_Lock);
	BOOL bRelease = TRUE;
	std::list<cProxyServer *>::iterator it = g_InstanceList.begin();
	while (it != g_InstanceList.end())
	{
		if (*it == this)
			g_InstanceList.erase(it++);
		else
		{
			if ((m_hModule != NULL) && (m_hModule == (*it)->m_hModule))
				bRelease = FALSE;
			++it;
		}
	}
	if (bRelease)
	{
		if (m_hTsRead)
		{
			m_pTsReaderArg->StopTsRead = TRUE;
			::WaitForSingleObject(m_hTsRead, INFINITE);
			::CloseHandle(m_hTsRead);
			delete m_pTsReaderArg;
		}

		Release();

		if (m_hModule)
		{
			if (!g_DisableUnloadBonDriver)
			{
				::FreeLibrary(m_hModule);
#if _DEBUG
				_RPT1(_CRT_WARN, "[%s] unloaded\n", m_strBonDriver);
#endif
			}
		}
	}
	else
	{
		if (m_hTsRead)
		{
			m_pTsReaderArg->TsLock.Enter();
			it = m_pTsReaderArg->TsReceiversList.begin();
			while (it != m_pTsReaderArg->TsReceiversList.end())
			{
				if (*it == this)
				{
					m_pTsReaderArg->TsReceiversList.erase(it);
					break;
				}
				++it;
			}
			m_pTsReaderArg->TsLock.Leave();

			// ���̃C���X�^���X�̓`�����l���r�����������Ă��邩�H
			if (m_bChannelLock == 0xff)
			{
				// �����Ă����ꍇ�́A�r�����擾�҂��̃C���X�^���X�͑��݂��Ă��邩�H
				if (m_pTsReaderArg->WaitExclusivePrivList.size() > 0)
				{
					// ���݂���ꍇ�́A���X�g�擪�̃C���X�^���X�ɔr�����������p���A���X�g����폜
					cProxyServer *p = m_pTsReaderArg->WaitExclusivePrivList.front();
					m_pTsReaderArg->WaitExclusivePrivList.pop_front();
					p->m_bChannelLock = 0xff;
				}
			}
			else
			{
				// �����Ă��Ȃ��ꍇ�́A�r�����擾�҂����X�g�Ɏ��g���܂܂�Ă��邩������Ȃ��̂ō폜
				m_pTsReaderArg->WaitExclusivePrivList.remove(this);
			}

			// �\���͒Ⴂ���[���ł͂Ȃ��c
			if (m_pTsReaderArg->TsReceiversList.empty())
			{
				m_pTsReaderArg->StopTsRead = TRUE;
				::WaitForSingleObject(m_hTsRead, INFINITE);
				::CloseHandle(m_hTsRead);
				delete m_pTsReaderArg;
			}
		}
	}

	if (m_s != INVALID_SOCKET)
		::closesocket(m_s);
}

DWORD WINAPI cProxyServer::Reception(LPVOID pv)
{
	cProxyServer *pProxy = static_cast<cProxyServer *>(pv);

	// ������COM���g�p���Ă���BonDriver�ɑ΂���΍�
	HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY);

	// �ڑ��N���C�A���g������Ԃ̓X���[�v�}�~
	EXECUTION_STATE es = ::SetThreadExecutionState(g_ThreadExecutionState);

	DWORD ret = pProxy->Process();
	delete pProxy;

#ifdef HAVE_UI
	::InvalidateRect(g_hWnd, NULL, TRUE);
#endif

	if (es != NULL)
		::SetThreadExecutionState(es);

	if (SUCCEEDED(hr))
		::CoUninitialize();

	return ret;
}

DWORD cProxyServer::Process()
{
	HANDLE hThread[2];
	hThread[0] = ::CreateThread(NULL, 0, cProxyServer::Sender, this, 0, NULL);
	if (hThread[0] == NULL)
		return 1;
	::SetThreadPriority(hThread[0], g_ThreadPrioritySender);

	hThread[1] = ::CreateThread(NULL, 0, cProxyServer::Receiver, this, 0, NULL);
	if (hThread[1] == NULL)
	{
		m_Error.Set();
		::WaitForSingleObject(hThread[0], INFINITE);
		::CloseHandle(hThread[0]);
		return 2;
	}

	HANDLE h[3] = { m_Error, m_fifoRecv.GetEventHandle(), g_ShutdownEvent };
	for (;;)
	{
		DWORD dwRet = ::WaitForMultipleObjects(3, h, FALSE, INFINITE);
		switch (dwRet)
		{
		case WAIT_OBJECT_0:
			goto end;

		case WAIT_OBJECT_0 + 1:
		{
#ifdef STRICT_LOCK
			LOCK(g_Lock);
#endif
			cPacketHolder *pPh;
			m_fifoRecv.Pop(&pPh);
			switch (pPh->GetCommand())
			{
			case eSelectBonDriver:
			{
				if (pPh->GetBodyLength() <= sizeof(char))
					makePacket(eSelectBonDriver, FALSE);
				else
				{
					LPCSTR p = (LPCSTR)(pPh->m_pPacket->payload);
					if (::strlen(p) > (sizeof(m_strBonDriver) - 1))
						makePacket(eSelectBonDriver, FALSE);
					else
					{
						BOOL bFind = FALSE;
#ifndef STRICT_LOCK
						LOCK(g_Lock);
#endif
						for (std::list<cProxyServer *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
						{
							if (::strcmp(p, (*it)->m_strBonDriver) == 0)
							{
								bFind = TRUE;
								m_hModule = (*it)->m_hModule;
								::strcpy(m_strBonDriver, (*it)->m_strBonDriver);
								m_pIBon = (*it)->m_pIBon;	// (*it)->m_pIBon��NULL�̉\���̓[���ł͂Ȃ�
								m_pIBon2 = (*it)->m_pIBon2;
								m_pIBon3 = (*it)->m_pIBon3;
								break;
							}
						}
						BOOL bSuccess;
						if (!bFind)
						{
							bSuccess = SelectBonDriver(p);
							if (bSuccess)
							{
								g_InstanceList.push_back(this);
								::strcpy(m_strBonDriver, p);
							}
						}
						else
						{
							g_InstanceList.push_back(this);
							bSuccess = TRUE;
						}
						makePacket(eSelectBonDriver, bSuccess);
#ifdef HAVE_UI
						if (bSuccess)
							::InvalidateRect(g_hWnd, NULL, TRUE);
#endif
					}
				}
				break;
			}

			case eCreateBonDriver:
			{
				if (m_pIBon == NULL)
				{
					BOOL bFind = FALSE;
					BOOL bLoop = FALSE;
					{
#ifndef STRICT_LOCK
						LOCK(g_Lock);
#endif
						for (std::list<cProxyServer *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
						{
							if (*it == this)
								continue;
							if (m_hModule == (*it)->m_hModule)
							{
								if ((*it)->m_pIBon != NULL)
								{
									bFind = TRUE;	// �����ɗ���̂͂��Ȃ�̃��A�P�[�X�̃n�Y
									m_pIBon = (*it)->m_pIBon;
									m_pIBon2 = (*it)->m_pIBon2;
									m_pIBon3 = (*it)->m_pIBon3;
									break;
								}
								else
								{
									// �����ɗ���̂͏���X�Ƀ��A�P�[�X�A���邢�̓N���C�A���g��
									// BonDriver_Proxy.dll��v�����A�T�[�o����BonDriver_Proxy.dll��
									// �����T�[�o�ɑ΂��Ď������g��v�����閳�����[�v��Ԃ����̃n�Y
									// �Ȃ��ASTRICT_LOCK����`���Ă���ꍇ�́A���������f�b�h���b�N��
									// �N�����̂ŁA��҂̏󋵂͔������Ȃ�

									// �C�x�߂̎G�ȃ`�F�b�N
									if (!::_memicmp(m_strBonDriver, "BonDriver_Proxy", 15))
									{
										bLoop = TRUE;
										break;
									}

									// �������[�v��ԈȊO�̏ꍇ�͈ꉞ���X�g�̍Ō�܂Ō������Ă݂āA
									// ����ł�������Ȃ�������CreateBonDriver()����点�Ă݂�
								}
							}
						}
					}
					if (!bFind && !bLoop)
					{
						if ((CreateBonDriver() != NULL) && (m_pIBon2 != NULL))
							makePacket(eCreateBonDriver, TRUE);
						else
						{
							makePacket(eCreateBonDriver, FALSE);
							m_Error.Set();
						}
					}
					else
					{
						if (!bLoop)
							makePacket(eCreateBonDriver, TRUE);
						else
						{
							makePacket(eCreateBonDriver, FALSE);
							m_Error.Set();
						}
					}
				}
				else
					makePacket(eCreateBonDriver, TRUE);
				break;
			}

			case eOpenTuner:
			{
				BOOL bFind = FALSE;
				{
#ifndef STRICT_LOCK
					LOCK(g_Lock);
#endif
					for (std::list<cProxyServer *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
					{
						if (*it == this)
							continue;
						if ((m_pIBon != NULL) && (m_pIBon == (*it)->m_pIBon))
						{
							if ((*it)->m_bTunerOpen)
							{
								bFind = TRUE;
								m_bTunerOpen = TRUE;
								break;
							}
						}
					}
				}
				if (!bFind)
					m_bTunerOpen = OpenTuner();
				makePacket(eOpenTuner, m_bTunerOpen);
				break;
			}

			case eCloseTuner:
			{
				BOOL bFind = FALSE;
				{
#ifndef STRICT_LOCK
					LOCK(g_Lock);
#endif
					for (std::list<cProxyServer *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
					{
						if (*it == this)
							continue;
						if ((m_pIBon != NULL) && (m_pIBon == (*it)->m_pIBon))
						{
							if ((*it)->m_bTunerOpen)
							{
								bFind = TRUE;
								break;
							}
						}
					}
				}
				if (!bFind)
				{
					if (m_hTsRead)
					{
						m_pTsReaderArg->StopTsRead = TRUE;
						::WaitForSingleObject(m_hTsRead, INFINITE);
						::CloseHandle(m_hTsRead);
						delete m_pTsReaderArg;
					}
					CloseTuner();
				}
				else
				{
					if (m_hTsRead)
					{
#ifndef STRICT_LOCK
						LOCK(g_Lock);
#endif
						m_pTsReaderArg->TsLock.Enter();
						std::list<cProxyServer *>::iterator it = m_pTsReaderArg->TsReceiversList.begin();
						while (it != m_pTsReaderArg->TsReceiversList.end())
						{
							if (*it == this)
							{
								m_pTsReaderArg->TsReceiversList.erase(it);
								break;
							}
							++it;
						}
						m_pTsReaderArg->TsLock.Leave();

						// ���̃C���X�^���X�̓`�����l���r�����������Ă��邩�H
						if (m_bChannelLock == 0xff)
						{
							// �����Ă����ꍇ�́A�r�����擾�҂��̃C���X�^���X�͑��݂��Ă��邩�H
							if (m_pTsReaderArg->WaitExclusivePrivList.size() > 0)
							{
								// ���݂���ꍇ�́A���X�g�擪�̃C���X�^���X�ɔr�����������p���A���X�g����폜
								cProxyServer *p = m_pTsReaderArg->WaitExclusivePrivList.front();
								m_pTsReaderArg->WaitExclusivePrivList.pop_front();
								p->m_bChannelLock = 0xff;
							}
						}
						else
						{
							// �����Ă��Ȃ��ꍇ�́A�r�����擾�҂����X�g�Ɏ��g���܂܂�Ă��邩������Ȃ��̂ō폜
							m_pTsReaderArg->WaitExclusivePrivList.remove(this);
						}

						// �\���͒Ⴂ���[���ł͂Ȃ��c
						if (m_pTsReaderArg->TsReceiversList.empty())
						{
							m_pTsReaderArg->StopTsRead = TRUE;
							::WaitForSingleObject(m_hTsRead, INFINITE);
							::CloseHandle(m_hTsRead);
							delete m_pTsReaderArg;
						}
					}
				}
				m_bChannelLock = 0;
				m_hTsRead = NULL;
				m_pTsReaderArg = NULL;
				m_bTunerOpen = FALSE;
				break;
			}

			case ePurgeTsStream:
			{
				if (m_hTsRead)
				{
					m_pTsReaderArg->TsLock.Enter();
					if (m_pTsReaderArg->TsReceiversList.size() <= 1)
					{
						PurgeTsStream();
						m_pTsReaderArg->pos = 0;
					}
					m_pTsReaderArg->TsLock.Leave();
					makePacket(ePurgeTsStream, TRUE);
				}
				else
					makePacket(ePurgeTsStream, FALSE);
				break;
			}

			case eRelease:
				m_Error.Set();
				break;

			case eEnumTuningSpace:
			{
				if (pPh->GetBodyLength() != sizeof(DWORD))
					makePacket(eEnumTuningSpace, _T(""));
				else
				{
					LPCTSTR p = EnumTuningSpace(::ntohl(*(DWORD *)(pPh->m_pPacket->payload)));
					if (p)
						makePacket(eEnumTuningSpace, p);
					else
						makePacket(eEnumTuningSpace, _T(""));
				}
				break;
			}

			case eEnumChannelName:
			{
				if (pPh->GetBodyLength() != (sizeof(DWORD) * 2))
					makePacket(eEnumChannelName, _T(""));
				else
				{
					LPCTSTR p = EnumChannelName(::ntohl(*(DWORD *)(pPh->m_pPacket->payload)), ::ntohl(*(DWORD *)&(pPh->m_pPacket->payload[sizeof(DWORD)])));
					if (p)
						makePacket(eEnumChannelName, p);
					else
						makePacket(eEnumChannelName, _T(""));
				}
				break;
			}

			case eSetChannel2:
			{
				if (pPh->GetBodyLength() != ((sizeof(DWORD) * 2) + sizeof(BYTE)))
					makePacket(eSetChannel2, (DWORD)0xff);
				else
				{
					m_bChannelLock = pPh->m_pPacket->payload[sizeof(DWORD) * 2];
					BOOL bLocked = FALSE;
					cProxyServer *pHavePriv = NULL;
					{
#ifndef STRICT_LOCK
						LOCK(g_Lock);
#endif
						for (std::list<cProxyServer *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
						{
							if (*it == this)
								continue;
							if ((m_pIBon != NULL) && (m_pIBon == (*it)->m_pIBon))
							{
								if ((*it)->m_bChannelLock > m_bChannelLock)
									bLocked = TRUE;
								else if ((*it)->m_bChannelLock == 0xff)
								{
									// �Ώۃ`���[�i�ɑ΂��ėD��x255�̃C���X�^���X�����ɂ����ԂŁA���̃C���X�^���X��
									// �v�����Ă���D��x��255�̏ꍇ�A���̃C���X�^���X�̗D��x���b��I��254�ɂ���
									// (�������Ȃ��ƁA�D��x255�̃C���X�^���X���`�����l���ύX�ł��Ȃ��Ȃ��)
									m_bChannelLock = 0xfe;
									bLocked = TRUE;
									pHavePriv = *it;
								}
								if ((m_hTsRead == NULL) && ((*it)->m_hTsRead != NULL))
								{
									m_hTsRead = (*it)->m_hTsRead;
									m_pTsReaderArg = (*it)->m_pTsReaderArg;
									m_pTsReaderArg->TsLock.Enter();
									m_pTsReaderArg->TsReceiversList.push_back(this);
									m_pTsReaderArg->TsLock.Leave();
								}
							}
						}
						// ���̃C���X�^���X�̗D��x��������ꂽ�ꍇ
						if (pHavePriv != NULL)
						{
							if (m_hTsRead)
							{
								// �r�����擾�҂����X�g�ɂ܂����g���܂܂�Ă��Ȃ���Βǉ�
								BOOL bFind = FALSE;
								std::list<cProxyServer *>::iterator it = m_pTsReaderArg->WaitExclusivePrivList.begin();
								while (it != m_pTsReaderArg->WaitExclusivePrivList.end())
								{
									if (*it == this)
									{
										bFind = TRUE;
										break;
									}
									++it;
								}
								if (!bFind)
									m_pTsReaderArg->WaitExclusivePrivList.push_back(this);
							}
							else
							{
								// ���̃C���X�^���X�̗D��x��������ꂽ���A�r�����������Ă���C���X�^���X�ւ̔z�M��
								// �J�n����Ă��Ȃ��ꍇ�́A���̃C���X�^���X����r������D��
								// �������鎖�������Ƃ��Ė]�܂����̂��ǂ����͔��������A�������������ɗ���̂́A
								// ���Y�C���X�^���X�ł�SetChannel()�̎��s��A���������ɐڑ����������Ă����Ԃł���A
								// �\���Ƃ��Ă̓[���ł͂Ȃ����̂́A���Ȃ�̃��A�P�[�X�Ɍ�����͂�
								pHavePriv->m_bChannelLock = 0;
								m_bChannelLock = 0xff;
							}
						}
					}
					if (bLocked)
						makePacket(eSetChannel2, (DWORD)0x01);
					else
					{
						if (m_hTsRead)
							m_pTsReaderArg->TsLock.Enter();
						BOOL b = SetChannel(::ntohl(*(DWORD *)(pPh->m_pPacket->payload)), ::ntohl(*(DWORD *)&(pPh->m_pPacket->payload[sizeof(DWORD)])));
						if (m_hTsRead)
						{
							// ��U���b�N���O���ƃ`�����l���ύX�O�̃f�[�^�����M����Ȃ�����ۏ؂ł��Ȃ��Ȃ�ׁA
							// �`�����l���ύX�O�̃f�[�^�̔j����CNR�̍X�V�w���͂����ōs��
							if (b)
							{
								m_pTsReaderArg->pos = 0;
								m_pTsReaderArg->ChannelChanged = TRUE;
							}
							m_pTsReaderArg->TsLock.Leave();
						}
						if (b)
						{
							makePacket(eSetChannel2, (DWORD)0x00);
							if (m_hTsRead == NULL)
							{
#ifndef STRICT_LOCK
								// ������Ō������Ă�̂ɂȂ��ēx��������̂��ƌ����ƁA����BonDriver��v�����Ă��镡����
								// �N���C�A���g����A�قړ����̃^�C�~���O�ōŏ���eSetChannel2�����N�G�X�g���ꂽ�ꍇ�̈�
								// eSetChannel2�S�̂��܂Ƃ߂ă��b�N����ΕK�v�����Ȃ邪�ABonDriver_Proxy�����[�h����A
								// ���ꂪ�������g�ɐڑ����Ă����ꍇ�f�b�h���b�N���鎖�ɂȂ�
								// �Ȃ��A���l�̗��R��eCreateBonDriver, eOpenTuner, eCloseTuner�̃��b�N�͎��͕s���S
								// �������A�������g�ւ̍ċA�ڑ����s��Ȃ��Ȃ�Ί��S�ȃ��b�N���\
								// ���ۂ̏��A�e�X�g�p�r�ȊO�Ŏ������g�ւ̍Đڑ����K�v�ɂȂ�󋵂ƌ����̂͂܂�������
								// �v���̂ŁASTRICT_LOCK����`���Ă���ꍇ�͊��S�ȃ��b�N���s�����ɂ���
								// ���������̂����ɁABonDriver_Proxy�����[�h���A��������̃v���L�V�`�F�[���̂ǂ�����
								// �������g�ɍċA�ڑ������ꍇ�̓f�b�h���b�N�ƂȂ�̂Œ���
								BOOL bFind = FALSE;
								LOCK(g_Lock);
								for (std::list<cProxyServer *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
								{
									if (*it == this)
										continue;
									if (m_pIBon == (*it)->m_pIBon)
									{
										if ((*it)->m_hTsRead != NULL)
										{
											bFind = TRUE;
											m_hTsRead = (*it)->m_hTsRead;
											m_pTsReaderArg = (*it)->m_pTsReaderArg;
											m_pTsReaderArg->TsLock.Enter();
											m_pTsReaderArg->TsReceiversList.push_back(this);
											m_pTsReaderArg->TsLock.Leave();
											break;
										}
									}
								}
								if (!bFind)
								{
#endif
									m_pTsReaderArg = new stTsReaderArg();
									m_pTsReaderArg->TsReceiversList.push_back(this);
									m_pTsReaderArg->pIBon = m_pIBon;
									m_hTsRead = ::CreateThread(NULL, 0, cProxyServer::TsReader, m_pTsReaderArg, 0, NULL);
									if (m_hTsRead == NULL)
									{
										delete m_pTsReaderArg;
										m_pTsReaderArg = NULL;
										m_Error.Set();
									}
									else
										::SetThreadPriority(m_hTsRead, g_ThreadPriorityTsReader);
#ifndef STRICT_LOCK
								}
#endif
							}
						}
						else
							makePacket(eSetChannel2, (DWORD)0xff);
					}
				}
				break;
			}

			case eGetTotalDeviceNum:
				makePacket(eGetTotalDeviceNum, GetTotalDeviceNum());
				break;

			case eGetActiveDeviceNum:
				makePacket(eGetActiveDeviceNum, GetActiveDeviceNum());
				break;

			case eSetLnbPower:
			{
				if (pPh->GetBodyLength() != sizeof(BYTE))
					makePacket(eSetLnbPower, FALSE);
				else
					makePacket(eSetLnbPower, SetLnbPower((BOOL)(pPh->m_pPacket->payload[0])));
				break;
			}

			case eGetClientInfo:
			{
				union {
					SOCKADDR_STORAGE ss;
					SOCKADDR_IN si4;
					SOCKADDR_IN6 si6;
				};
				char addr[INET6_ADDRSTRLEN], buf[512], info[1024], *p, *exinfo;
				int port, len, num = 0;
				size_t left, size;
				p = info;
				p[0] = '\0';
				left = size = sizeof(info);
				exinfo = NULL;
				std::list<cProxyServer *>::iterator it = g_InstanceList.begin();
				while (it != g_InstanceList.end())
				{
					len = sizeof(ss);
					if (::getpeername((*it)->m_s, (SOCKADDR *)&ss, &len) == 0)
					{
						if (ss.ss_family == AF_INET)
						{
							// IPv4
#ifdef _WIN64
							::inet_ntop(AF_INET, &(si4.sin_addr), addr, sizeof(addr));
#else
							//::lstrcpyA(addr, ::inet_ntoa(si4.sin_addr));
							::inet_ntop(AF_INET, &(si4.sin_addr), addr, sizeof(addr));
#endif
							port = ::ntohs(si4.sin_port);
						}
						else
						{
							// IPv6
#ifdef _WIN64
							::inet_ntop(AF_INET6, &(si6.sin6_addr), addr, sizeof(addr));
#else
							::inet_ntop(AF_INET6, &(si6.sin6_addr), addr, sizeof(addr));
							/*char *cp = addr;
							for (int i = 0; i < 16; i += 2)
								cp += ::wsprintfA(cp, "%02x%02x%c", si6.sin6_addr.s6_addr[i], si6.sin6_addr.s6_addr[i + 1], (i != 14) ? ':' : '\0');*/
#endif
							port = ::ntohs(si6.sin6_port);
						}
					}
					else
					{
						::lstrcpyA(addr, "unknown host...");
						port = 0;
					}
					len = ::wsprintfA(buf, "%02d: [%s]:[%d] / [%s]\n", num, addr, port, (*it)->m_strBonDriver);
					if ((size_t)len >= left)
					{
						left += size;
						size *= 2;
						if (exinfo != NULL)
						{
							char *bp = exinfo;
							exinfo = new char[size];
							::lstrcpyA(exinfo, bp);
							delete[] bp;
						}
						else
						{
							exinfo = new char[size];
							::lstrcpyA(exinfo, info);
						}
						p = exinfo + ::lstrlenA(exinfo);
					}
					::lstrcpyA(p, buf);
					p += len;
					left -= len;
					num++;
					++it;
				}
				if (exinfo != NULL)
				{
					size = (p - exinfo) + 1;
					p = exinfo;
				}
				else
				{
					size = (p - info) + 1;
					p = info;
				}
				cPacketHolder *ph = new cPacketHolder(eGetClientInfo, size);
				::memcpy(ph->m_pPacket->payload, p, size);
				m_fifoSend.Push(ph);
				if (exinfo != NULL)
					delete[] exinfo;
				break;
			}

			default:
				break;
			}
			delete pPh;
			break;
		}

		case WAIT_OBJECT_0 + 2:
			// �I���v��
			// fall-through
		default:
			// �����̃G���[
			m_Error.Set();
			goto end;
		}
	}
end:
	::WaitForMultipleObjects(2, hThread, TRUE, INFINITE);
	::CloseHandle(hThread[0]);
	::CloseHandle(hThread[1]);
	return 0;
}

int cProxyServer::ReceiverHelper(char *pDst, DWORD left)
{
	int len, ret;
	fd_set rd;
	timeval tv;

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	while (left > 0)
	{
		if (m_Error.IsSet())
			return -1;

		FD_ZERO(&rd);
		FD_SET(m_s, &rd);
		if ((len = ::select(0/*(int)(m_s + 1)*/, &rd, NULL, NULL, &tv)) == SOCKET_ERROR)
		{
			ret = -2;
			goto err;
		}

		if (len == 0)
			continue;

		// MSDN��recv()�̃\�[�X��Ƃ��������A"SOCKET_ERROR"�����̒l�Ȃ͕̂ۏ؂���Ă���ۂ�
		if ((len = ::recv(m_s, pDst, left, 0)) <= 0)
		{
			ret = -3;
			goto err;
		}
		left -= len;
		pDst += len;
	}
	return 0;
err:
	m_Error.Set();
	return ret;
}

DWORD WINAPI cProxyServer::Receiver(LPVOID pv)
{
	cProxyServer *pProxy = static_cast<cProxyServer *>(pv);
	char *p;
	DWORD left, ret;
	cPacketHolder *pPh = NULL;

	for (;;)
	{
		pPh = new cPacketHolder(16);
		left = sizeof(stPacketHead);
		p = (char *)&(pPh->m_pPacket->head);
		if (pProxy->ReceiverHelper(p, left) != 0)
		{
			ret = 201;
			goto end;
		}

		if (!pPh->IsValid())
		{
			pProxy->m_Error.Set();
			ret = 202;
			goto end;
		}

		left = pPh->GetBodyLength();
		if (left == 0)
		{
			pProxy->m_fifoRecv.Push(pPh);
			continue;
		}

		if (left > 16)
		{
			if (left > 512)
			{
				pProxy->m_Error.Set();
				ret = 203;
				goto end;
			}
			cPacketHolder *pTmp = new cPacketHolder(left);
			pTmp->m_pPacket->head = pPh->m_pPacket->head;
			delete pPh;
			pPh = pTmp;
		}

		p = (char *)(pPh->m_pPacket->payload);
		if (pProxy->ReceiverHelper(p, left) != 0)
		{
			ret = 204;
			goto end;
		}

		pProxy->m_fifoRecv.Push(pPh);
	}
end:
	delete pPh;
	return ret;
}

void cProxyServer::makePacket(enumCommand eCmd, BOOL b)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(BYTE));
	p->m_pPacket->payload[0] = (BYTE)b;
	m_fifoSend.Push(p);
}

void cProxyServer::makePacket(enumCommand eCmd, DWORD dw)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(DWORD));
	DWORD *pos = (DWORD *)(p->m_pPacket->payload);
	*pos = ::htonl(dw);
	m_fifoSend.Push(p);
}

void cProxyServer::makePacket(enumCommand eCmd, LPCTSTR str)
{
	register size_t size = (::_tcslen(str) + 1) * sizeof(TCHAR);
	cPacketHolder *p = new cPacketHolder(eCmd, size);
	::memcpy(p->m_pPacket->payload, str, size);
	m_fifoSend.Push(p);
}

void cProxyServer::makePacket(enumCommand eCmd, BYTE *pSrc, DWORD dwSize, float fSignalLevel)
{
	register size_t size = (sizeof(DWORD) * 2) + dwSize;
	cPacketHolder *p = new cPacketHolder(eCmd, size);
	union {
		DWORD dw;
		float f;
	} u;
	u.f = fSignalLevel;
	DWORD *pos = (DWORD *)(p->m_pPacket->payload);
	*pos++ = ::htonl(dwSize);
	*pos++ = ::htonl(u.dw);
	if (dwSize > 0)
		::memcpy(pos, pSrc, dwSize);
	m_fifoSend.Push(p);
}

DWORD WINAPI cProxyServer::Sender(LPVOID pv)
{
	cProxyServer *pProxy = static_cast<cProxyServer *>(pv);
	DWORD ret;
	HANDLE h[2] = { pProxy->m_Error, pProxy->m_fifoSend.GetEventHandle() };
	for (;;)
	{
		DWORD dwRet = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);
		switch (dwRet)
		{
		case WAIT_OBJECT_0:
			ret = 101;
			goto end;

		case WAIT_OBJECT_0 + 1:
		{
			cPacketHolder *pPh;
			pProxy->m_fifoSend.Pop(&pPh);
			int left = (int)pPh->m_Size;
			char *p = (char *)(pPh->m_pPacket);
			while (left > 0)
			{
				int len = ::send(pProxy->m_s, p, left, 0);
				if (len == SOCKET_ERROR)
				{
					pProxy->m_Error.Set();
					break;
				}
				left -= len;
				p += len;
			}
			delete pPh;
			break;
		}

		default:
			// �����̃G���[
			pProxy->m_Error.Set();
			ret = 102;
			goto end;
		}
	}
end:
	return ret;
}

DWORD WINAPI cProxyServer::TsReader(LPVOID pv)
{
	stTsReaderArg *pArg = static_cast<stTsReaderArg *>(pv);
	IBonDriver *pIBon = pArg->pIBon;
	volatile BOOL &StopTsRead = pArg->StopTsRead;
	volatile BOOL &ChannelChanged = pArg->ChannelChanged;
	DWORD &pos = pArg->pos;
	std::list<cProxyServer *> &TsReceiversList = pArg->TsReceiversList;
	cCriticalSection &TsLock = pArg->TsLock;
	DWORD dwSize, dwRemain, now, before = 0;
	float fSignalLevel = 0;
	DWORD ret = 300;
	const DWORD TsPacketBufSize = g_TsPacketBufSize;
	BYTE *pBuf, *pTsBuf = new BYTE[TsPacketBufSize];
#if _DEBUG && DETAILLOG
	DWORD Counter = 0;
#endif

	// ������COM���g�p���Ă���BonDriver�ɑ΂���΍�
	HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY);
	// TS�ǂݍ��݃��[�v
	while (!StopTsRead)
	{
		dwSize = dwRemain = 0;
		{
			LOCK(TsLock);
			if ((((now = ::GetTickCount()) - before) >= 1000) || ChannelChanged)
			{
				fSignalLevel = pIBon->GetSignalLevel();
				before = now;
				ChannelChanged = FALSE;
			}
			if (pIBon->GetTsStream(&pBuf, &dwSize, &dwRemain) && (dwSize != 0))
			{
				if ((pos + dwSize) < TsPacketBufSize)
				{
					::memcpy(&pTsBuf[pos], pBuf, dwSize);
					pos += dwSize;
					if (dwRemain == 0)
					{
						for (std::list<cProxyServer *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
							(*it)->makePacket(eGetTsStream, pTsBuf, pos, fSignalLevel);
#if _DEBUG && DETAILLOG
						_RPT3(_CRT_WARN, "makePacket0() : %u : size[%x] / dwRemain[%d]\n", Counter++, pos, dwRemain);
#endif
						pos = 0;
					}
				}
				else
				{
					DWORD left, dwLen = TsPacketBufSize - pos;
					::memcpy(&pTsBuf[pos], pBuf, dwLen);
					for (std::list<cProxyServer *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
						(*it)->makePacket(eGetTsStream, pTsBuf, TsPacketBufSize, fSignalLevel);
#if _DEBUG && DETAILLOG
					_RPT3(_CRT_WARN, "makePacket1() : %u : size[%x] / dwRemain[%d]\n", Counter++, TsPacketBufSize, dwRemain);
#endif
					left = dwSize - dwLen;
					pBuf += dwLen;
					while (left >= TsPacketBufSize)
					{
						for (std::list<cProxyServer *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
							(*it)->makePacket(eGetTsStream, pBuf, TsPacketBufSize, fSignalLevel);
#if _DEBUG && DETAILLOG
						_RPT2(_CRT_WARN, "makePacket2() : %u : size[%x]\n", Counter++, TsPacketBufSize);
#endif
						left -= TsPacketBufSize;
						pBuf += TsPacketBufSize;
					}
					if (left != 0)
					{
						if (dwRemain == 0)
						{
							for (std::list<cProxyServer *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
								(*it)->makePacket(eGetTsStream, pBuf, left, fSignalLevel);
#if _DEBUG && DETAILLOG
							_RPT3(_CRT_WARN, "makePacket3() : %u : size[%x] / dwRemain[%d]\n", Counter++, left, dwRemain);
#endif
							left = 0;
						}
						else
							::memcpy(pTsBuf, pBuf, left);
					}
					pos = left;
				}
			}
		}
		if (dwRemain == 0)
			::Sleep(WAIT_TIME);
	}
	if (SUCCEEDED(hr))
		::CoUninitialize();
	delete[] pTsBuf;
	return ret;
}

BOOL cProxyServer::SelectBonDriver(LPCSTR p)
{
	if (p[0] == '\\' && p[1] == '\\')
		return FALSE;

	HMODULE hModule = NULL;
	BOOL bLoaded = FALSE;
	for (std::list<stLoadedDriver *>::iterator it = g_LoadedDriverList.begin(); it != g_LoadedDriverList.end(); ++it)
	{
		if (::strcmp(p, (*it)->strBonDriver) == 0)
		{
			hModule = (*it)->hModule;
			bLoaded = TRUE;
			break;
		}
	}
	if (hModule == NULL)
	{
		hModule = ::LoadLibraryA(p);
		if (hModule == NULL)
			return FALSE;
#if _DEBUG
		_RPT1(_CRT_WARN, "[%s] loaded\n", p);
#endif
	}

	m_hModule = hModule;

	if (g_DisableUnloadBonDriver && !bLoaded)
	{
		stLoadedDriver *pLd = new stLoadedDriver;
		::strcpy(pLd->strBonDriver, p);	// stLoadedDriver::strBonDriver�̃T�C�Y��ProxyServer::m_strBonDriver�Ɠ���
		pLd->hModule = hModule;
		g_LoadedDriverList.push_back(pLd);
	}

	return TRUE;
}

IBonDriver *cProxyServer::CreateBonDriver()
{
	if (m_hModule)
	{
		IBonDriver *(*f)() = (IBonDriver *(*)())::GetProcAddress(m_hModule, "CreateBonDriver");
		if (f)
		{
			try { m_pIBon = f(); }
			catch (...) {}
			if (m_pIBon)
			{
				m_pIBon2 = dynamic_cast<IBonDriver2 *>(m_pIBon);
				m_pIBon3 = dynamic_cast<IBonDriver3 *>(m_pIBon);
			}
		}
	}
	return m_pIBon;
}

const BOOL cProxyServer::OpenTuner(void)
{
	BOOL b = FALSE;
	if (m_pIBon)
		b = m_pIBon->OpenTuner();
	return b;
}

void cProxyServer::CloseTuner(void)
{
	if (m_pIBon)
		m_pIBon->CloseTuner();
}

void cProxyServer::PurgeTsStream(void)
{
	if (m_pIBon)
		m_pIBon->PurgeTsStream();
}

void cProxyServer::Release(void)
{
	if (m_pIBon)
	{
		if (g_SandBoxedRelease)
		{
			__try { m_pIBon->Release(); }
			__except (EXCEPTION_EXECUTE_HANDLER){}
		}
		else
			m_pIBon->Release();
	}
}

LPCTSTR cProxyServer::EnumTuningSpace(const DWORD dwSpace)
{
	LPCTSTR pStr = NULL;
	if (m_pIBon2)
		pStr = m_pIBon2->EnumTuningSpace(dwSpace);
	return pStr;
}

LPCTSTR cProxyServer::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	LPCTSTR pStr = NULL;
	if (m_pIBon2)
		pStr = m_pIBon2->EnumChannelName(dwSpace, dwChannel);
	return pStr;
}

const BOOL cProxyServer::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	BOOL b = FALSE;
	if (m_pIBon2)
		b = m_pIBon2->SetChannel(dwSpace, dwChannel);
	return b;
}

const DWORD cProxyServer::GetTotalDeviceNum(void)
{
	DWORD d = 0;
	if (m_pIBon3)
		d = m_pIBon3->GetTotalDeviceNum();
	return d;
}

const DWORD cProxyServer::GetActiveDeviceNum(void)
{
	DWORD d = 0;
	if (m_pIBon3)
		d = m_pIBon3->GetActiveDeviceNum();
	return d;
}

const BOOL cProxyServer::SetLnbPower(const BOOL bEnable)
{
	BOOL b = FALSE;
	if (m_pIBon3)
		b = m_pIBon3->SetLnbPower(bEnable);
	return b;
}

#if defined(HAVE_UI) || defined(BUILD_AS_SERVICE)
struct HostInfo{
	char *host;
	char *port;
};
static DWORD WINAPI Listen(LPVOID pv)
{
	HostInfo *phi = static_cast<HostInfo *>(pv);
	char *host = phi->host;
	char *port = phi->port;
	delete phi;
#else
static int Listen(char *host, char *port)
{
#endif
	addrinfo hints, *results, *rp;
	SOCKET lsock[MAX_HOSTS], csock;
	int i, j, nhost, len;
	char *p, *hostbuf, *h[MAX_HOSTS];
	fd_set rd;
	timeval tv;

	hostbuf = new char[strlen(host) + 1];
	strcpy(hostbuf, host);
	nhost = 0;
	p = hostbuf;
	do
	{
		h[nhost++] = p;
		if ((p = strchr(p, ',')) != NULL)
		{
			char *q = p - 1;
			while (*q == ' ' || *q == '\t')
				*q-- = '\0';
			*p++ = '\0';
			while (*p == ' ' || *p == '\t')
				*p++ = '\0';
		}
		if (nhost >= MAX_HOSTS)
			break;
	} while ((p != NULL) && (*p != '\0'));

	for (i = 0; i < nhost; i++)
	{
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
		if (getaddrinfo(h[i], port, &hints, &results) != 0)
		{
			hints.ai_flags = AI_PASSIVE;
			if (getaddrinfo(h[i], port, &hints, &results) != 0)
			{
				for (j = 0; j < i; j++)
					closesocket(lsock[j]);
				delete[] hostbuf;
				return 1;
			}
		}

		for (rp = results; rp != NULL; rp = rp->ai_next)
		{
			lsock[i] = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
			if (lsock[i] == INVALID_SOCKET)
				continue;

			BOOL exclusive = TRUE;
			setsockopt(lsock[i], SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof(exclusive));

			if (bind(lsock[i], rp->ai_addr, (int)(rp->ai_addrlen)) != SOCKET_ERROR)
				break;

			closesocket(lsock[i]);
		}
		freeaddrinfo(results);
		if (rp == NULL)
		{
			for (j = 0; j < i; j++)
				closesocket(lsock[j]);
			delete[] hostbuf;
			return 2;
		}

		if (listen(lsock[i], 4) == SOCKET_ERROR)
		{
			for (j = 0; j <= i; j++)
				closesocket(lsock[j]);
			delete[] hostbuf;
			return 3;
		}
	}
	delete[] hostbuf;

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	while (!g_ShutdownEvent.IsSet())
	{
		FD_ZERO(&rd);
		for (i = 0; i < nhost; i++)
			FD_SET(lsock[i], &rd);
		if ((len = select(0/*(int)(max(lsock) + 1)*/, &rd, NULL, NULL, &tv)) == SOCKET_ERROR)
		{
			for (i = 0; i < nhost; i++)
				closesocket(lsock[i]);
			return 4;
		}
		if (len > 0)
		{
			for (i = 0; i < nhost; i++)
			{
				if (FD_ISSET(lsock[i], &rd))
				{
					len--;
					if ((csock = accept(lsock[i], NULL, NULL)) != INVALID_SOCKET)
					{
						cProxyServer *pProxy = new cProxyServer();
						pProxy->setSocket(csock);
						HANDLE hThread = CreateThread(NULL, 0, cProxyServer::Reception, pProxy, 0, NULL);
						if (hThread)
							CloseHandle(hThread);
						else
							delete pProxy;
					}
				}
				if (len == 0)
					break;
			}
		}
	}

	for (i = 0; i < nhost; i++)
		closesocket(lsock[i]);
	return 0;
}

#ifndef BUILD_AS_SERVICE
#ifdef HAVE_UI
void NotifyIcon(int mode)
{
	NOTIFYICONDATA nid = {};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_hWnd;
	nid.uID = ID_TASKTRAY;
	if (mode == 0)
	{
		// ADD
		nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
		nid.uCallbackMessage = WM_TASKTRAY;
		nid.hIcon = LoadIcon(g_hInstance, _T("BDP_ICON"));
		lstrcpy(nid.szTip, _T("BonDriverProxy"));
		for (;;)
		{
			if (Shell_NotifyIcon(NIM_ADD, &nid))
				break;	// �o�^����
			if (GetLastError() != ERROR_TIMEOUT)
				break;	// �^�C���A�E�g�ȊO�̃G���[�Ȃ̂Œ��߂�
			Sleep(500);	// ������Ƒ҂��Ă���m�F
			if (Shell_NotifyIcon(NIM_MODIFY, &nid))
				break;	// �o�^�������Ă�
		}
	}
	else
	{
		// DEL
		nid.uFlags = 0;
		Shell_NotifyIcon(NIM_DELETE, &nid);
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	static UINT s_iTaskbarRestart;

	switch (iMsg)
	{
	case WM_CREATE:
		s_iTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_CLOSE:
		ModifyMenu(g_hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TASKTRAY_SHOW, _T("���E�B���h�E�\��"));
		ShowWindow(hWnd, SW_HIDE);
		return 0;

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hDc = BeginPaint(hWnd, &ps);
		TEXTMETRIC tm;
		GetTextMetrics(hDc, &tm);
		union {
			SOCKADDR_STORAGE ss;
			SOCKADDR_IN si4;
			SOCKADDR_IN6 si6;
		};
		char addr[INET6_ADDRSTRLEN];
		int port, len, num = 0;
		char buf[512];
		g_Lock.Enter();
		std::list<cProxyServer *>::iterator it = g_InstanceList.begin();
		while (it != g_InstanceList.end())
		{
			len = sizeof(ss);
			if (getpeername((*it)->m_s, (SOCKADDR *)&ss, &len) == 0)
			{
				if (ss.ss_family == AF_INET)
				{
					// IPv4
#ifdef _WIN64
					inet_ntop(AF_INET, &(si4.sin_addr), addr, sizeof(addr));
#else
					inet_ntop(AF_INET, &(si4.sin_addr), addr, sizeof(addr));
					//lstrcpyA(addr, inet_ntoa(si4.sin_addr));
#endif
					port = ntohs(si4.sin_port);
				}
				else
				{
					// IPv6
#ifdef _WIN64
					inet_ntop(AF_INET6, &(si6.sin6_addr), addr, sizeof(addr));
#else
					char *p = addr;
					for (int i = 0; i < 16; i += 2)
						p += wsprintfA(p, "%02x%02x%c", si6.sin6_addr.s6_addr[i], si6.sin6_addr.s6_addr[i + 1], (i != 14) ? ':' : '\0');
#endif
					port = ntohs(si6.sin6_port);
				}
			}
			else
			{
				lstrcpyA(addr, "unknown host...");
				port = 0;
			}
			wsprintfA(buf, "%02d: [%s]:[%d] / [%s]", num, addr, port, (*it)->m_strBonDriver);
			TextOutA(hDc, 5, 5 + (num * tm.tmHeight), buf, lstrlenA(buf));
			num++;
			++it;
		}
		g_Lock.Leave();
		EndPaint(hWnd, &ps);
		return 0;
	}

	case WM_TASKTRAY:
	{
		switch (LOWORD(lParam))
		{
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hWnd);
			TrackPopupMenu(g_hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
			PostMessage(hWnd, WM_NULL, 0, 0);
			return 0;
		}
		break;
	}

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case ID_TASKTRAY_SHOW:
		{
			ModifyMenu(g_hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TASKTRAY_HIDE, _T("���E�B���h�E��\��"));
			ShowWindow(hWnd, SW_SHOW);
			return 0;
		}

		case ID_TASKTRAY_HIDE:
		{
			ModifyMenu(g_hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TASKTRAY_SHOW, _T("���E�B���h�E�\��"));
			ShowWindow(hWnd, SW_HIDE);
			return 0;
		}

		case ID_TASKTRAY_RELOAD:
		{
			if (g_InstanceList.size() != 0)
			{
				if (MessageBox(hWnd, _T("�ڑ����̃N���C�A���g�����݂��Ă��܂��B�ؒf����܂�����낵���ł����H"), _T("Caution"), MB_YESNO) != IDYES)
					return 0;
			}
			ShutdownInstances();
			CleanUp();
			if (Init(g_hInstance) != 0)
			{
				MessageBox(NULL, _T("ini�t�@�C����������܂���B�������ݒu�����̂��ēǂݍ��݂��ĉ������B"), _T("Error"), MB_OK);
				return 0;
			}
			HostInfo *phi = new HostInfo;
			phi->host = g_Host;
			phi->port = g_Port;
			g_hListenThread = CreateThread(NULL, 0, Listen, phi, 0, NULL);
			if (g_hListenThread == NULL)
			{
				delete phi;
				MessageBox(NULL, _T("�҂��󂯃X���b�h�̍쐬�Ɏ��s���܂����B�I�����܂��B"), _T("Error"), MB_OK);
				PostQuitMessage(0);
			}
			else
				MessageBox(hWnd, _T("�ēǂݍ��݂��܂����B"), _T("Info"), MB_OK);
			return 0;
		}

		case ID_TASKTRAY_EXIT:
		{
			if (g_InstanceList.size() != 0)
			{
				if (MessageBox(hWnd, _T("�ڑ����̃N���C�A���g�����݂��Ă��܂����A��낵���ł����H"), _T("Caution"), MB_YESNO) != IDYES)
					return 0;
			}
			PostQuitMessage(0);
			return 0;
		}
		}
		break;
	}

	default:
		if (iMsg == s_iTaskbarRestart)
			NotifyIcon(0);
		break;
	}
	return DefWindowProc(hWnd, iMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE/*hPrevInstance*/, LPSTR/*lpCmdLine*/, int/*nCmdShow*/)
{
#if _DEBUG
	HANDLE hLogFile = CreateFile(_T("dbglog.txt"), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	_CrtMemState ostate, nstate, dstate;
	_CrtMemCheckpoint(&ostate);
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_WARN, hLogFile);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_ERROR, hLogFile);
	_RPT0(_CRT_WARN, "--- PROCESS_START ---\n");
//	int *p = new int[2];	// ���[�N���o�e�X�g�p
#endif

	if (Init(hInstance) != 0)
	{
		MessageBox(NULL, _T("ini�t�@�C����������܂���B"), _T("Error"), MB_OK);
		return -1;
	}

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		MessageBox(NULL, _T("winsock�̏������Ɏ��s���܂����B"), _T("Error"), MB_OK);
		return -2;
	}

	HostInfo *phi = new HostInfo;
	phi->host = g_Host;
	phi->port = g_Port;
	g_hListenThread = CreateThread(NULL, 0, Listen, phi, 0, NULL);
	if (g_hListenThread == NULL)
	{
		delete phi;
		MessageBox(NULL, _T("�҂��󂯃X���b�h�̍쐬�Ɏ��s���܂����B"), _T("Error"), MB_OK);
		return -3;
	}

	MSG msg;
	WNDCLASSEX wndclass;

	wndclass.cbSize = sizeof(wndclass);
	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = hInstance;
	wndclass.hIcon = LoadIcon(hInstance, _T("BDP_ICON"));
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = _T("bdp");
	wndclass.hIconSm = LoadIcon(hInstance, _T("BDP_ICON"));

	RegisterClassEx(&wndclass);

	g_hWnd = CreateWindow(_T("bdp"), _T("Information"), WS_OVERLAPPED | WS_SYSMENU | WS_THICKFRAME, CW_USEDEFAULT, 0, 640, 320, NULL, NULL, hInstance, NULL);

//	ShowWindow(g_hWnd, nCmdShow);
//	UpdateWindow(g_hWnd);

	g_hInstance = hInstance;
	g_hMenu = CreatePopupMenu();
	InsertMenu(g_hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TASKTRAY_SHOW, _T("���E�B���h�E�\��"));
	InsertMenu(g_hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TASKTRAY_RELOAD, _T("ini�ēǂݍ���"));
	InsertMenu(g_hMenu, 2, MF_BYPOSITION | MF_STRING, ID_TASKTRAY_EXIT, _T("�I��"));
	NotifyIcon(0);

	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	ShutdownInstances();	// g_hListenThread�͂��̒���CloseHandle()�����
	CleanUp();	// ShutdownInstances()��g_LoadedDriverList�ɃA�N�Z�X����X���b�h�͖����Ȃ��Ă���͂�

	NotifyIcon(1);
	DestroyMenu(g_hMenu);

	WSACleanup();

#if _DEBUG
	_CrtMemCheckpoint(&nstate);
	if (_CrtMemDifference(&dstate, &ostate, &nstate))
	{
		_CrtMemDumpStatistics(&dstate);
		_CrtMemDumpAllObjectsSince(&ostate);
	}
	_RPT0(_CRT_WARN, "--- PROCESS_END ---\n");
	CloseHandle(hLogFile);
#endif

	return (int)msg.wParam;
}
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE/*hPrevInstance*/, LPSTR/*lpCmdLine*/, int/*nCmdShow*/)
{
	if (Init(hInstance) != 0)
		return -1;

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return -2;

	int ret = Listen(g_Host, g_Port);

	{
		// ���Ȃ����ǈꉞ
		LOCK(g_Lock);
		CleanUp();
	}

	WSACleanup();
	return ret;
}
#endif
#else
#include "ServiceMain.cpp"
#endif
