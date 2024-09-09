
// test00Dlg.cpp : implementation file
//

#include "stdafx.h"
#include "test00.h"
#include "test00Dlg.h"
#include "mpeg2ts\mpegts.h"
#include "Hooking\Helper.h"
#include "DMO\appstream.h"
#include "EasyHook.h"
#include "ntstatus.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

static HANDLE s_hMapFile = NULL;
static HANDLE s_hMutex = NULL;
static HANDLE s_hEvent = NULL;
static BYTE* s_pBuf = NULL;

//static BYTE* s_pScreenBuf[MAX_OVERLAP];
static CRITICAL_SECTION s_csEncoding;
static CRITICAL_SECTION s_csScreenBuf[MAX_OVERLAP];
static CDC s_desktopDC;
static CDC s_captureDC[MAX_OVERLAP];
static CBitmap s_capturedBitmap[MAX_OVERLAP];

static CAppStream* s_pEncoderDmo = NULL;
static bool s_bInactive = true;
static int s_LatencyCounter = 0;

static void sendTsPacket(const BYTE *, const int);
#if !(USE_MFC_SOCK)
static SOCKET s_socket = INVALID_SOCKET;
#else
static CMySocket s_mfc_sock;
#endif
static struct sockaddr_in s_address;

static const GUID CLSID_DP = {0x3EFB6510,0xAE3B,0x4465,0xA5,0x1E,0x5E,0xC9,0x7E,0x55,0xA0,0xAC};

//static void CALLBACK timer_handler(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);
//static TIMECALLBACK timer_handler;
//static WAITORTIMERCALLBACK timer_handler;
static void CALLBACK timer_handler(PVOID lpParam, BOOLEAN TimerOrWaitFired);

// CMySocket
CMySocket::CMySocket() :
	m_bReadyToSend(false),
	m_nBytesSent(0),
	m_nBytesBufferSize(0)
{
	// Create a socket for sending
	// **DO NOT SPECIFY DESTINATION ADDRESS HERE**
	m_hSocket = INVALID_SOCKET;
}

CMySocket::~CMySocket()
{
}

void CMySocket::init(void)
{
	int i;
	for (i = 0; i < g_iMaxUdpDg; i += TS_PACKET_SIZE) {
		m_iMaxUdpDg = i;
	}
}

void CMySocket::try_send(void)
{
	while (m_nBytesSent < m_nBytesBufferSize)
	{
		int dwBytes;
		const int to_send = min(m_nBytesBufferSize - m_nBytesSent, m_iMaxUdpDg);

		if ((dwBytes = Send(m_sendBuffer + m_nBytesSent, to_send)) == SOCKET_ERROR)
		{
			if (GetLastError() == WSAEWOULDBLOCK)
			{
				m_bReadyToSend = false;
				break;
			}
			else
			{
				add_log("CAsyncSocket::Send failed (0x%X)", GetLastError());
				break;
			}
		}
		else
		{
			m_nBytesSent += dwBytes;
		}
	}

	if (m_nBytesSent == m_nBytesBufferSize)
	{
		m_nBytesSent = m_nBytesBufferSize = 0;
		//m_sendBuffer = _T("");
	}
}

// UdpSendSocket member functions
void CMySocket::OnSend(int nErrorCode)
{
	m_bReadyToSend = true;    // Unused
	try_send();
	CAsyncSocket::OnSend(nErrorCode);
}

bool CMySocket::send(const void* lpBuf, const int nBufLen)
{
	if (m_nBytesBufferSize + nBufLen >= SOCK_BUF_SIZE ) return false;
	memcpy(m_sendBuffer + m_nBytesBufferSize, lpBuf, nBufLen);
	m_nBytesBufferSize += nBufLen;
	try_send();

	return true;
}

// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()


// Ctest00Dlg dialog




Ctest00Dlg::Ctest00Dlg(CWnd* pParent /*=NULL*/)
	: CDialog(Ctest00Dlg::IDD, pParent)
	, m_dwIPAddr(0)
	, m_nPort(1234)
	, m_nPID(0)
	, m_nWidth(1280)
	, m_nHeight(720)
	, m_fFPS(30.0)
	, m_strExe(_T(""))
	, m_bCheckFile(FALSE)
	, m_strFileName(_T("test.ts"))
	, m_nLatencyEntries(0)
	, m_strLatencyFile(_T("latency.txt"))
	, m_nByPacket(1)
	, m_bStealth(FALSE)
	, m_nDelay(0)
	, m_nOverlap(DEFAULT_OVERLAP)
	, m_bLowJitter(FALSE)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	//m_hMapFile = NULL;
	//m_pBuf = NULL;
	//m_pEncoderDmo = NULL;
	m_bProcessStarted = false;
	m_bIsRunning = false;
	m_bIsExiting = false;
	m_bCaptureScreen = false;
	m_nPrevPID = 0;
	//m_fps = 30.0;
	//m_nTimer = 0;
	m_hTimer = INVALID_HANDLE_VALUE;
	m_fpTs = NULL;
	m_fpLatency = NULL;
}

Ctest00Dlg::~Ctest00Dlg()
{
	OnBnClickedCancel();
	IpcDestroy();
}

void Ctest00Dlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_IPAddress(pDX, IDC_IPADDRESS1, m_dwIPAddr);
	DDX_Text(pDX, IDC_EDIT_PORT, m_nPort);
	DDX_Text(pDX, IDC_EDIT_PID, m_nPID);
	DDX_Text(pDX, IDC_EDIT_WIDTH, m_nWidth);
	DDX_Text(pDX, IDC_EDIT_HEIGHT, m_nHeight);
	DDX_Text(pDX, IDC_EDIT_FPS, m_fFPS);
	DDV_MinMaxDouble(pDX, m_fFPS, 1.0, 60.0);
	DDX_Text(pDX, IDC_EDIT_PID2, m_strExe);
	DDX_Check(pDX, IDC_CHECK_FILE, m_bCheckFile);
	DDX_Text(pDX, IDC_EDIT_FILENAME, m_strFileName);
	DDX_Text(pDX, IDC_EDIT_LATENCY_ENTRIES, m_nLatencyEntries);
	DDX_Text(pDX, IDC_EDIT_LATENCY_FILE, m_strLatencyFile);
	DDX_Radio(pDX, IDC_RADIO_BY_PACKET, m_nByPacket);
	DDX_Check(pDX, IDC_CHECK_STEALTH, m_bStealth);
	DDX_Text(pDX, IDC_EDIT_DELAY, m_nDelay);
	DDX_Text(pDX, IDC_EDIT_OVERLAP, m_nOverlap);
	DDV_MinMaxInt(pDX, m_nOverlap, 1, MAX_OVERLAP);
	DDX_Check(pDX, IDC_CHECK_LOW_JITTER, m_bLowJitter);
}

BEGIN_MESSAGE_MAP(Ctest00Dlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDOK, &Ctest00Dlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &Ctest00Dlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_BUTTON_EXE, &Ctest00Dlg::OnBnClickedButtonExe)
	ON_BN_CLICKED(IDC_BUTTON_FILE, &Ctest00Dlg::OnBnClickedButtonFile)
	ON_BN_CLICKED(IDC_CHECK_FILE, &Ctest00Dlg::OnBnClickedCheckFile)
	ON_BN_CLICKED(IDC_BUTTON_LATENCY_FILE, &Ctest00Dlg::OnBnClickedButtonLatencyFile)
END_MESSAGE_MAP()


// Ctest00Dlg message handlers

BOOL Ctest00Dlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here
	if (IpcInit() != 0) {
		GetDlgItem(IDOK)->EnableWindow(FALSE);
	}

#if (USE_MFC_SOCK)
	s_mfc_sock.init();
#endif

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void Ctest00Dlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void Ctest00Dlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR Ctest00Dlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

//static const TCHAR s_szPrefix[]=TEXT("Global\\GameVideo");
static const TCHAR s_szPrefix[]=TEXT("GameVideo");

#if (RED_TEST || LINE_TEST)
static BYTE s_TestBuf[BUF_SIZE];	// test
#endif

int Ctest00Dlg::IpcInit(void)
{
	TCHAR szName[128];
	TCHAR szBuf[256];
	int i;

	_stprintf(szName, _T("%s%u"), s_szPrefix, m_MyPid = GetCurrentProcessId());

	s_hMapFile = CreateFileMapping(
		INVALID_HANDLE_VALUE,    // use paging file
		NULL,                    // default security 
		PAGE_READWRITE,          // read/write access
		0,                       // maximum object size (high-order DWORD) 
		BUF_SIZE,                // maximum object size (low-order DWORD)  
		szName);                 // name of mapping object
	if (s_hMapFile == NULL) 
	{ 
		_stprintf(szBuf, _T("Could not create file mapping object (0x%X)"), GetLastError());
		MessageBox(szBuf, TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		return 1;
	}

	s_pBuf = (BYTE*)MapViewOfFile(s_hMapFile,
		FILE_MAP_READ,
		0,                   
		0,                   
		BUF_SIZE);
	if (s_pBuf == NULL) 
	{ 
		MessageBox(TEXT("Could not map view of file."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		CloseHandle(s_hMapFile);
		s_hMapFile = NULL;
		return 1;
	}

	_stprintf(szName, _T("%sMutex%u"), s_szPrefix, m_MyPid);

	s_hMutex = CreateMutex(NULL, FALSE, szName);
	if (s_hMutex == NULL) 
	{ 
		_stprintf(szBuf, _T("Could not create mutex object (0x%X)"), GetLastError());
		MessageBox(szBuf, TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );

		UnmapViewOfFile(s_pBuf);
		s_pBuf = NULL;

		CloseHandle(s_hMapFile);
		s_hMapFile = NULL;

		return 1;
	}

	_stprintf(szName, _T("%sEvent%u"), s_szPrefix, m_MyPid);

	s_hEvent = CreateEvent(NULL, FALSE, FALSE, szName);
	if (s_hEvent == NULL) 
	{ 
		_stprintf(szBuf, _T("Could not create event object (0x%X)"), GetLastError());
		MessageBox(szBuf, TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );

		UnmapViewOfFile(s_pBuf);
		s_pBuf = NULL;

		CloseHandle(s_hMapFile);
		s_hMapFile = NULL;

		CloseHandle(s_hMutex);
		s_hMutex = NULL;

		return 1;
	}

#if 0
	s_pScreenBuf = (BYTE*)_aligned_malloc(BUF_SIZE, 16);
	if (s_pScreenBuf == NULL)
	{
		_stprintf(szBuf, _T("Could not allocate memory (0x%X)"), GetLastError());
		MessageBox(szBuf, TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );

		UnmapViewOfFile(s_pBuf);
		s_pBuf = NULL;

		CloseHandle(s_hMapFile);
		s_hMapFile = NULL;

		CloseHandle(s_hMutex);
		s_hMutex = NULL;

		CloseHandle(s_hEvent);
		s_hEvent = NULL;

		return 1;
	}
#endif

	InitializeCriticalSection(&s_csEncoding);

	for (i = 0; i < MAX_OVERLAP; i++) {
		InitializeCriticalSection(&s_csScreenBuf[i]);
	}

#if (RED_TEST)
	int i;
	memset(s_TestBuf, 0, BUF_SIZE);
	for (i = 2; i < BUF_SIZE; i+= SRC_RGB_BYTES) {
		s_TestBuf[i] = 255;
	}
#elif (LINE_TEST)
	int i;
	memset(s_TestBuf, 0, BUF_SIZE);
	for (i = 2; i < BUF_SIZE; i+= SRC_RGB_BYTES * 2) {
		s_TestBuf[i] = 255;
	}
#endif

	return 0;
}

void Ctest00Dlg::IpcDestroy(void)
{
	int i;

	if (s_pBuf) {
		UnmapViewOfFile(s_pBuf);
		s_pBuf = NULL;
	}
	if (s_hMapFile) {
		CloseHandle(s_hMapFile);
		s_hMapFile = NULL;
	}
	if (s_hMutex) {
		CloseHandle(s_hMutex);
		s_hMutex = NULL;
	}
	if (s_hEvent) {
		CloseHandle(s_hEvent);
		s_hEvent = NULL;
	}
#if 0
	if (s_pScreenBuf) {
		_aligned_free(s_pScreenBuf);
		s_pScreenBuf = NULL;
	}
#endif
	DeleteCriticalSection(&s_csEncoding);
	for (i = 0; i < MAX_OVERLAP; i++) {
		DeleteCriticalSection(&s_csScreenBuf[i]);
	}
}


#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS 0
#define STATUS_WOW_ASSERTION 0xC0009898L
#define STATUS_ACCESS_DENIED 0xC0000022L
#define STATUS_ACCESS_DENIED 0xC0000022L
#define STATUS_NO_MEMORY 0xC0000017L
#define STATUS_INTERNAL_ERROR 0xC00000E5L
#define STATUS_NOT_SUPPORTED 0xC00000BBL
#endif

// Start...
void Ctest00Dlg::OnBnClickedOk()
{
	DMO_MEDIA_TYPE mt;
	VIDEOINFOHEADER vih;
	int width, height;
	void (*pCallback)(const BYTE*, const int);
	ULONG inject_option = EASYHOOK_INJECT_DEFAULT;
	int i;

	if (m_bIsRunning == true) return;

	UpdateData(TRUE);

	if (m_nPID == 0 && m_strExe.GetLength() == 0) {
#if 0
		const int nScreenWidth = GetSystemMetrics(SM_CXSCREEN);
		const int nScreenHeight = GetSystemMetrics(SM_CYSCREEN);
#endif
		HWND hDesktopWnd = ::GetDesktopWindow();

		if (s_desktopDC.Attach(::GetDC(hDesktopWnd)) == FALSE) {
			MessageBox(TEXT("CDC::Attach failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
			return;
		}

		for (i = 0; i < m_nOverlap; i++) {
			if (s_captureDC[i].CreateCompatibleDC(&s_desktopDC) == FALSE) {
				MessageBox(TEXT("CDC::CreateCompatibleDC failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
				return;
			}
#if 0
			if (s_capturedBitmap.CreateCompatibleBitmap(&s_desktopDC, nScreenWidth, nScreenHeight) == FALSE) {
#else
			if (s_capturedBitmap[i].CreateCompatibleBitmap(&s_desktopDC, m_nWidth, m_nHeight) == FALSE) {
#endif
				MessageBox(TEXT("CBitmap::CreateCompatibleBitmap failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
				return;
			}
			s_captureDC[i].SelectObject(&s_capturedBitmap[i]);
		}

		m_bCaptureScreen = true;
	} else {
		m_bCaptureScreen = false;
	}

	//add_log("Started...");

	if (m_bStealth == TRUE) inject_option = EASYHOOK_INJECT_STEALTH;

	if (m_nByPacket == 0) pCallback = sendTsPacket;
	else pCallback = NULL;

	if (s_hMutex == NULL && IpcInit() != 0) {
		MessageBox(TEXT("IpcInit() failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		return;
	}

	if (init_mpegtsenc(m_fFPS, pCallback, m_nDelay) != 0)
	{
		MessageBox(TEXT("Could not initialize mpeg2ts encoder."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		return;
	}

	if (s_pEncoderDmo) 
	{
		delete s_pEncoderDmo;
		s_pEncoderDmo = NULL;
	}

	memset(&mt, 0, sizeof(mt));
	memset(&vih, 0, sizeof(vih));
	width = (int)m_nWidth;						// ??? => Yes
	height = (int)m_nHeight;

	mt.bFixedSizeSamples = TRUE;
	mt.bTemporalCompression = FALSE;
	mt.majortype = MEDIATYPE_Video;
#if (SRC_RGB_BYTES == 3)
	mt.subtype = MEDIASUBTYPE_RGB24;
#else
	mt.subtype = MEDIASUBTYPE_RGB32;
#endif
	mt.formattype = FORMAT_VideoInfo;
	mt.cbFormat = sizeof(VIDEOINFOHEADER);
	mt.pbFormat = (BYTE*)&vih;
	mt.pUnk = NULL;
	mt.lSampleSize = 0;							// not used by me.
	vih.AvgTimePerFrame = (REFERENCE_TIME)(10000000.0 / m_fFPS + 0.5);	// actually it is not used.
	vih.dwBitErrorRate = 0;
	vih.dwBitRate = 0;							// ?
	vih.rcSource.left = 0;
	vih.rcSource.top = 0;
	vih.rcSource.right = width;					// stride? => No
	vih.rcSource.bottom = height;
	vih.rcTarget = vih.rcSource;
	vih.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	vih.bmiHeader.biBitCount = 24;
	vih.bmiHeader.biWidth = width;
	vih.bmiHeader.biHeight = -height;			// top-down!!!

	s_pEncoderDmo = new CAppStream();

	if (s_pEncoderDmo->Init(CLSID_DP, &mt, this->m_hWnd) != S_OK)
	{
		destroy_mpegtsenc();
		delete s_pEncoderDmo;
		s_pEncoderDmo = NULL;
		return;
	}

	if (m_bCheckFile == TRUE && m_strFileName.GetLength() > 0) {
		m_fpTs = _tfopen(m_strFileName.GetBuffer(), _T("wb"));
	}

	if (m_nLatencyEntries > 0 && m_strLatencyFile.GetLength() > 0) {
		m_fpLatency = _tfopen(m_strLatencyFile.GetBuffer(), _T("w"));
	}

	if (m_dwIPAddr != 0 && m_nPort != 0)
	{
#if (USE_MFC_SOCK)
		const int send_buffer_size = 0;

		s_mfc_sock.Create(0,SOCK_DGRAM,FD_WRITE);
		s_mfc_sock.SetSockOpt(SO_SNDBUF, &send_buffer_size, sizeof(int));

		s_address.sin_family = AF_INET;
		s_address.sin_addr.s_addr = htonl(m_dwIPAddr);
		//s_address.sin_addr.s_addr = inet_addr("127.0.0.1");
		s_address.sin_port = htons(m_nPort);

		if (s_mfc_sock.Connect((SOCKADDR*)&s_address, sizeof(s_address)) == FALSE) {
			MessageBox(TEXT("connect failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
			goto ERROR_ABORT;
		}
#else
		if ((s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
			MessageBox(TEXT("socket failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
			goto ERROR_ABORT;
		}

		s_address.sin_family = AF_INET;
		s_address.sin_addr.s_addr = htonl(m_dwIPAddr);
		//s_address.sin_addr.s_addr = inet_addr("127.0.0.1");
		s_address.sin_port = htons(m_nPort);

		if (connect(s_socket, (SOCKADDR*)&s_address, sizeof(s_address)) == SOCKET_ERROR) {
			MessageBox(TEXT("connect failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
			goto ERROR_ABORT;
		}
#endif
	}

	if (m_bCaptureScreen == false && (m_nPID == 0 || m_nPrevPID != m_nPID))
	{
		NTSTATUS code;
		inject_payload payload;

		payload.id = m_MyPid;
		payload.inject_type = (m_nPID == 0)? inject_payload::INJECTOR_STARTER : inject_payload::INJECTOR_NORMAL;
		payload.width = m_nWidth;
		payload.height = m_nHeight;

		if (m_nPID != 0) {
			code = RhInjectLibrary(m_nPID, 0, inject_option, L"test01.dll", L"test01.dll", (PVOID) &payload, sizeof(payload));
		} 
		else {
			ULONG pid;
			if ((code = RhCreateAndInject(m_strExe.GetBuffer(), NULL, 0, inject_option, L"test01.dll", L"test01.dll", (PVOID) &payload, sizeof(payload), &pid)) == 0) {
				m_nPID = pid;
				m_bProcessStarted = true;
			}
		}
		
		if (code == STATUS_WOW_ASSERTION) {
			MessageBox(TEXT("Injection through WOW64 boundaries is not supported."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		}
		else if (code == STATUS_NOT_FOUND) {
			MessageBox(TEXT("The target process could not be found."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		}
		else if (code == STATUS_ACCESS_DENIED) {
			MessageBox(TEXT("The target process could not be accessed properly or remote thread creation failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		}
		else if (code != 0) {
			TCHAR strBuf[128];
			_stprintf(strBuf, TEXT("RhInjectLibrary/RhCreateAndInject failed (0x%X)"), code);
			MessageBox(strBuf, TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		}

		if (code != 0)
		{
			goto ERROR_ABORT;
		}
		else
		{
			m_nPrevPID = m_nPID;
		}
	}

	s_LatencyCounter = 0;
	QueryPerformanceFrequency(&m_ticksPerSecond);

	if (CreateTimerQueueTimer(&m_hTimer, NULL, timer_handler, this, 
		(DWORD)(1000.0 / m_fFPS + 0.5),
		(DWORD)(1000.0 / m_fFPS + 0.5),
		WT_EXECUTEDEFAULT) == FALSE)
	{
		MessageBox(TEXT("CreateTimerQueueTimer failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		goto ERROR_ABORT;
	}

#if 0
	m_nTimer = timeSetEvent(
		(UINT)(1000000.0 / m_fFPS + 0.5),
		100,
		timer_handler,
		(DWORD_PTR)this,
		TIME_PERIODIC | TIME_CALLBACK_FUNCTION);

	if (m_nTimer == 0)
	{
		MessageBox(TEXT("timeSetEvent failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
		destroy_mpegtsenc();
		delete s_pEncoderDmo;
		s_pEncoderDmo = NULL;
		return;
	}
#endif

	s_bInactive = true;
	this->SetWindowText(_T(DEMO_NAME_INACTIIVE));
	m_bIsRunning = true;
	return;

ERROR_ABORT:
	destroy_mpegtsenc();
	if (m_fpTs) {
		fclose(m_fpTs);
		m_fpTs = NULL;
	}
	if (m_fpLatency) {
		fclose(m_fpLatency);
		m_fpLatency = NULL;
	}
	if (s_pEncoderDmo) {
		delete s_pEncoderDmo;
		s_pEncoderDmo = NULL;
	}
#if (USE_MFC_SOCK)
	s_mfc_sock.Close();
	s_mfc_sock.m_hSocket = INVALID_SOCKET;
#else
	if (s_socket != INVALID_SOCKET) {
		closesocket(s_socket);
		s_socket = INVALID_SOCKET;
	}
#endif
}

static BOOL CALLBACK TerminateAppEnum( HWND hwnd, LPARAM lParam )
{
	DWORD dwID ;

    GetWindowThreadProcessId(hwnd, &dwID) ;

    if(dwID == (DWORD)lParam)
    {
       PostMessage(hwnd, WM_CLOSE, 0, 0) ;
    }

    return TRUE ;
}

// Stop...
void Ctest00Dlg::OnBnClickedCancel()
{
	int i;

	if (m_bIsRunning == false) {
		if (m_bIsExiting == false) {
			m_bIsExiting = true;
			OnCancel();
		}
		return;
	}

	//timeKillEvent(m_nTimer);
	DeleteTimerQueueTimer(NULL, m_hTimer, INVALID_HANDLE_VALUE);
	m_hTimer = INVALID_HANDLE_VALUE;

	//Sleep(1000);	// wait for timer handler thread to finish...The multimedia timer runs in its own thread.

	destroy_mpegtsenc();

	if (s_pEncoderDmo) {
		delete s_pEncoderDmo;
		s_pEncoderDmo = NULL;
	}

	if (m_fpTs) {
		fclose(m_fpTs);
		m_fpTs = NULL;
	}

	if (m_fpLatency) {
		fclose(m_fpLatency);
		m_fpLatency = NULL;
	}

#if (USE_MFC_SOCK)
	s_mfc_sock.Close();
	s_mfc_sock.m_hSocket = INVALID_SOCKET;
#else
	if (s_socket != INVALID_SOCKET) {
		closesocket(s_socket);
		s_socket = INVALID_SOCKET;
	}
#endif

	if (m_bCaptureScreen == true) {
		::ReleaseDC(::GetDesktopWindow(), s_desktopDC.Detach());
		
		for (i = 0; i < m_nOverlap; i++) {
			s_captureDC[i].DeleteDC();
			s_capturedBitmap[i].DeleteObject();
		}
	}

	this->SetWindowText(_T(DEMO_NAME));
	m_bIsRunning = false;

	if (m_bProcessStarted == true) {	// stop started process
		SetEvent(s_hEvent);
		Sleep(1000);
		EnumWindows((WNDENUMPROC)TerminateAppEnum, (LPARAM) m_nPID);
		m_bProcessStarted = false;
	}
}

void Ctest00Dlg::WriteTsFile(void)
{
	if (m_fpTs) 
	{
		fwrite(g_TsBuffer, 1, g_TsBufferLen, m_fpTs);
	}
}

void Ctest00Dlg::SendTsFrame(void)
{
#if (USE_MFC_SOCK)
	if (s_mfc_sock.m_hSocket != INVALID_SOCKET) {
		if (s_mfc_sock.send(g_TsBuffer, g_TsBufferLen) == false) {
			add_log("send buffer overflow.");
		}
	}
#else
	if (s_socket != INVALID_SOCKET) {
		if (send(s_socket, (const char*)g_TsBuffer, g_TsBufferLen, 0) == SOCKET_ERROR) {
			add_log("send failed (0x%X).", WSAGetLastError());
		}
	}
#endif
}

static void sendTsPacket(const BYTE *data, const int size)
{
#if (USE_MFC_SOCK)
	if (s_mfc_sock.m_hSocket != INVALID_SOCKET) {
		if (s_mfc_sock.send(data, size) == false) {
			add_log("send buffer overflow.");
		}
	}
#else
	if (s_socket != INVALID_SOCKET) {
		if (send(s_socket, (const char*)data, size, 0) == SOCKET_ERROR) {
			add_log("send failed (0x%X).", WSAGetLastError());
		}
	}
#endif
}

void Ctest00Dlg::SetRunning(void)
{
	this->SetWindowText(_T(DEMO_NAME_RUNNING));
}

void Ctest00Dlg::WriteLatency(LARGE_INTEGER& start, LARGE_INTEGER& stop)
{
	if (stop.LowPart != 0 || stop.HighPart != 0) {
		const __int64 diff = stop.QuadPart - start.QuadPart;
		const __int64 diff_u = av_rescale(diff, 1000000, (__int64)m_ticksPerSecond.QuadPart);
		if (m_fpLatency) {
			fprintf(m_fpLatency, "%I64u\n", diff_u);
		}
	}
	else if (m_fpLatency) {
		fprintf(m_fpLatency, "skipped\n");
	}
}

// It is slow and mono-threaded...
static bool CaptureScreen(const int i, const int width, const int height, BYTE* pBuf)
{

#if 0
	const int nScreenWidth = GetSystemMetrics(SM_CXSCREEN);
	const int nScreenHeight = GetSystemMetrics(SM_CYSCREEN);

	HWND hDesktopWnd = GetDesktopWindow();
	HDC hDesktopDC = GetDC(hDesktopWnd);
	HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);
	HBITMAP hCaptureBitmap =CreateCompatibleBitmap(hDesktopDC, 
		nScreenWidth, nScreenHeight);
	SelectObject(hCaptureDC,hCaptureBitmap); 
	BitBlt(hCaptureDC,0,0,nScreenWidth,nScreenHeight,
		hDesktopDC,0,0,SRCCOPY|CAPTUREBLT); 

	SaveCapturedBitmap(hCaptureBitmap); //Place holder - Put your code

	ReleaseDC(hDesktopWnd,hDesktopDC);
	DeleteDC(hCaptureDC);
	DeleteObject(hCaptureBitmap);

#else

	BITMAPINFO bmi;

#if 0
	if (s_captureDC.BitBlt(0,0,nScreenWidth,nScreenHeight,&s_desktopDC,0,0,SRCCOPY|CAPTUREBLT) == FALSE) {
#else
	if (s_captureDC[i].BitBlt(0,0,width,height,&s_desktopDC,0,0,SRCCOPY|CAPTUREBLT) == FALSE) {
#endif
		add_log("CaptureScreen: BitBlt failed (0x%X).", GetLastError());
		return false;
	}

	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
#if 0
    bmi.bmiHeader.biWidth = nScreenWidth;
    bmi.bmiHeader.biHeight = -nScreenHeight;	// top-down
#else
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;	// top-down
#endif
    bmi.bmiHeader.biPlanes = 1; 
    bmi.bmiHeader.biBitCount = SRC_RGB_BYTES * 8; 
    bmi.bmiHeader.biCompression = BI_RGB; 

    //Acquire bytes from bitmap
#if 0
	if (GetDIBits(s_captureDC,s_capturedBitmap,0,nScreenHeight,s_pScreenBuf,&bmi,DIB_RGB_COLORS) == 0) {
#else
	if (GetDIBits(s_captureDC[i],s_capturedBitmap[i],0,height,pBuf,&bmi,DIB_RGB_COLORS) == 0) {
#endif
		add_log("CaptureScreen: GetDIBits failed (0x%X).", GetLastError());
		return false;
	}

#if 0
	const int width_byte = width * SRC_RGB_BYTES;
	const int pitch = nScreenWidth * SRC_RGB_BYTES;
	const int clip_width_byte = min(width_byte, pitch);
	const int clip_height = min(height, nScreenHeight);
	register int i;
	register const BYTE *pSrc = (BYTE*)s_pScreenBuf;
	register BYTE *pDst = pBuf;

	for (i = 0; i < clip_height; i++) {
		memcpy(pDst, pSrc, clip_width_byte);
		pSrc += pitch;
		pDst += width_byte;
	}
#endif

#endif

	return true;
}

//static void CALLBACK timer_handler(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
static void CALLBACK timer_handler(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	static bool bFirst = true;
	Ctest00Dlg *pDlg = (Ctest00Dlg*)lpParam;
	unsigned width, height;
	void *pBCapture = s_pBuf + sizeof(unsigned) + sizeof(unsigned);
	void *pBIn = s_pEncoderDmo->GetInputBuffer();
	void *pBOut = s_pEncoderDmo->GetOutputBuffer();
	const DWORD cbInSize = pDlg->m_nWidth * pDlg->m_nHeight * SRC_RGB_BYTES;
	DWORD cbOutSize;
	bool bKeyFrame;
	HRESULT hr;
	const int nLatencyEntries = pDlg->m_nLatencyEntries;
	LARGE_INTEGER start, stop;
	const bool bCS = pDlg->m_bCaptureScreen;
	const int overlap = pDlg->m_nOverlap;
	int index;
	bool enter = false;
	const bool bLowJitter = (pDlg->m_bLowJitter == TRUE)? true : false;

	stop.LowPart = 0;
	stop.HighPart = 0;

#if 0
	EnterCriticalSection(&s_csScreenBuf);
#else
	for (index = 0; index < overlap; index++) {
		if (TryEnterCriticalSection(&s_csScreenBuf[index]) != 0) {
			enter = true;
			break;
		}
	}
	if (enter == false) {
		goto Done;
	}
#endif

	if (bCS == false) {
		memcpy(&width, s_pBuf, sizeof(unsigned));
		memcpy(&height, s_pBuf + sizeof(unsigned), sizeof(unsigned));

#if !(WHITE_TEST || RED_TEST || LINE_TEST)
		if (width != pDlg->m_nWidth || height != pDlg->m_nHeight) goto Done;
#endif
	} else {
		width = pDlg->m_nWidth;
		height = pDlg->m_nHeight;
	}

	if (s_bInactive == true) {
		s_bInactive = false;
		pDlg->SetRunning();
	}

	if (bLowJitter == false) set_sampling_time();
	QueryPerformanceCounter(&start);

#if (WHITE_TEST)
	memset(pBIn, 255, cbInSize);
#elif (RED_TEST || LINE_TEST)
	memset(pBIn, 0, cbInSize);
	memcpy((unsigned char *)pBIn + (pDlg->m_nWidth * SRC_RGB_BYTES), s_TestBuf, cbInSize / 16);
#elif 0
	if (bFirst == true) {
		memset(pBIn, 255, cbInSize);
		bFirst = false;
	}
	else
		memcpy(pBIn, pBCapture, cbInSize);
#else
	if (bCS == true) {
		if (CaptureScreen(index, width, height, (BYTE*)pBIn) == false) {
			goto Done;
		}
	}
	else {
		DWORD dwWaitResult = WaitForSingleObject(s_hMutex, 10);
		if (dwWaitResult != WAIT_OBJECT_0) {	// drop frame
			goto Done;
		}
		memcpy(pBIn, pBCapture, cbInSize);
		ReleaseMutex(s_hMutex);
	}
#endif

	EnterCriticalSection(&s_csEncoding);

	hr = s_pEncoderDmo->ProcessFrame(cbInSize, &cbOutSize, bKeyFrame);
	if (FAILED(hr)) {
		add_log("ProcessFrame failed (0x%X).", hr);
		goto Done0;
	}

	if (bLowJitter == true) set_sampling_time();

	if (feed_mpegtsenc(pBOut, cbOutSize, bKeyFrame, 0) != 0) {
		add_log("feed_mpegtsenc failed.");
		goto Done0;
	}

	QueryPerformanceCounter(&stop);

	if (pDlg->m_dwIPAddr != 0 && pDlg->m_nByPacket == 1) {
		pDlg->SendTsFrame();
	}

	if (pDlg->m_bCheckFile == TRUE) {
		pDlg->WriteTsFile();
	}

Done0:
	LeaveCriticalSection(&s_csEncoding);

Done:
	LeaveCriticalSection(&s_csScreenBuf[index]);

	if (s_LatencyCounter < nLatencyEntries) {
		pDlg->WriteLatency(start, stop);
		s_LatencyCounter++;
	}
}

void Ctest00Dlg::OnBnClickedButtonExe()
{
	// TODO: Add your control notification handler code here
	CFileDialog dlg(TRUE);

	if (dlg.DoModal() == IDOK)
	{
		UpdateData(TRUE);
		m_strExe = dlg.GetPathName();
		m_nPID = 0;
		UpdateData(FALSE);
	}
}

void Ctest00Dlg::OnBnClickedButtonFile()
{
	// TODO: Add your control notification handler code here
	CFileDialog dlg(FALSE);

	if (dlg.DoModal() == IDOK)
	{
		UpdateData(TRUE);
		m_strFileName = dlg.GetPathName();
		UpdateData(FALSE);
	}
}

void Ctest00Dlg::OnBnClickedCheckFile()
{
	// TODO: Add your control notification handler code here
}

void Ctest00Dlg::OnBnClickedButtonLatencyFile()
{
	// TODO: Add your control notification handler code here
	CFileDialog dlg(FALSE);

	if (dlg.DoModal() == IDOK)
	{
		UpdateData(TRUE);
		m_strLatencyFile = dlg.GetPathName();
		UpdateData(FALSE);
	}
}
