
// test00Dlg.h : header file
//

#pragma once

extern int g_iMaxUdpDg;
#define SOCK_BUF_SIZE (0xfffff)
#define TS_PACKET_SIZE 188

class CMySocket : public CAsyncSocket
{
	bool m_bReadyToSend;
	BYTE m_sendBuffer[SOCK_BUF_SIZE];
	int m_nBytesSent;		// head
	int m_nBytesBufferSize; // tail
	int m_iMaxUdpDg;

	void try_send(void);

public:
	CMySocket();
	virtual ~CMySocket();
      
	void init(void);
	bool send(const void* lpBuf, const int nBufLen ); 
	virtual void OnSend(int nErrorCode);
};



// Ctest00Dlg dialog
class Ctest00Dlg : public CDialog
{
// Construction
public:
	Ctest00Dlg(CWnd* pParent = NULL);	// standard constructor
	~Ctest00Dlg();

// Dialog Data
	enum { IDD = IDD_TEST00_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;
	//HANDLE m_hMapFile;
	//void* m_pBuf;
	DWORD m_MyPid;
	//CAppStream* m_pEncoderDmo;
	bool m_bProcessStarted;
	bool m_bIsRunning;
	bool m_bIsExiting;
	UINT m_nPrevPID;
	//UINT m_nTimer;
	HANDLE m_hTimer;
	FILE *m_fpTs;
	FILE *m_fpLatency;
	LARGE_INTEGER m_ticksPerSecond;

	//double m_fps;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	int IpcInit(void);
	void IpcDestroy(void);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	CString m_strExe;
	UINT m_nPID;
	BOOL m_bStealth;
	bool m_bCaptureScreen;
	DWORD m_dwIPAddr;
	int m_nPort;
	ULONGLONG m_nDelay;
	int m_nByPacket;
	UINT m_nWidth;
	UINT m_nHeight;
	double m_fFPS;
	int m_nOverlap;
	BOOL m_bLowJitter;
	BOOL m_bCheckFile;
	CString m_strFileName;
	int m_nLatencyEntries;
	CString m_strLatencyFile;

	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnBnClickedButtonExe();
	afx_msg void OnBnClickedButtonFile();
	afx_msg void OnBnClickedCheckFile();
	afx_msg void OnBnClickedButtonLatencyFile();

	void WriteTsFile(void);
	void SendTsFrame(void);

	void SetRunning(void);

	void WriteLatency(LARGE_INTEGER& start, LARGE_INTEGER& stop);
};
