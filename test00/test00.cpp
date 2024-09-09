
// test00.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "test00.h"
#include "test00Dlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// Ctest00App

BEGIN_MESSAGE_MAP(Ctest00App, CWinAppEx)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


// Ctest00App construction

Ctest00App::Ctest00App()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}


// The one and only Ctest00App object

Ctest00App theApp;


// Ctest00App initialization

int g_iMaxUdpDg;

BOOL Ctest00App::InitInstance()
{
	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinAppEx::InitInstance();

	WSAData wsaData;

	if (!AfxSocketInit(&wsaData))
	{
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
		return FALSE;
	}

	g_iMaxUdpDg = wsaData.iMaxUdpDg;
	if (g_iMaxUdpDg <=0) g_iMaxUdpDg = 8192;

	AfxEnableControlContainer();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	// of your final executable, you should remove from the following
	// the specific initialization routines you do not need
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(_T("Local AppWizard-Generated Applications"));

	Ctest00Dlg dlg;
	m_pMainWnd = &dlg;
	INT_PTR nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}
