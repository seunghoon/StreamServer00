// test01.cpp : Defines the exported functions for the DLL application.
//

//#include "stdafx.h"

#define USE_DDRAW 0			// should be 0
#define DST_RGB_BYTES 4		// should be 4
#define CLIP_TEST 1			// should be 1

#define EMPTY_HOOK 0		// should be 0
#define INSTALL_HOOK 1		// should be 1
#define LINE_TEST 0

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>
//#pragma comment(lib, "user32.lib")

#include "EasyHook.h"
#include "Helper.h"

#define BUF_SIZE (2000*1000*4*2)
//static const TCHAR s_szPrefix[]=TEXT("Global\\GameVideo");
static const TCHAR s_szPrefix[]=TEXT("GameVideo");
static void *s_pBuf = NULL;
static HANDLE s_hMapFile = NULL;
static HANDLE s_hMutex = NULL;
static HANDLE s_hEvent = NULL;

#include <d3d9.h>
#include <d3dx9.h>

static HOOK_TRACE_INFO hHook_EndScene = { NULL };
static HOOK_TRACE_INFO hHook_Reset = { NULL };

static IDirect3DSurface9 *s_pSurface0 = NULL;

#if USE_DDRAW
#include <ddraw.h>
static IDirectDraw7 *s_pDDraw = NULL;
static IDirectDrawSurface7 *s_pSurface1 = NULL;
static IDirectDrawSurface7 *s_pSurface2 = NULL;
static IDirectDrawSurface7 *s_pSurface3 = NULL;	// unused
#else
static IDirect3DSurface9 *s_pSurface1 = NULL;
static IDirect3DSurface9 *s_pSurface2 = NULL;
static IDirect3DSurface9 *s_pSurface3 = NULL;
#endif

static CRITICAL_SECTION s_csRenderTarget;

static UINT org_width = 0, org_height = 0;
static UINT new_width = 0, new_height = 0;
static UINT input_width, input_height;
static D3DFORMAT org_format;
#if (DST_RGB_BYTES == 3)
static const D3DFORMAT new_format = D3DFMT_R8G8B8;
#else
static const D3DFORMAT new_format = D3DFMT_X8R8G8B8;
#endif

//#define FORCE_OK(expr) {if((hRes = (expr)) != S_OK) { add_log("%s(%d): failed", __FILE__, __LINE__); goto ERROR_ABORT;}}
#define FORCE_OK(expr, func) {if((hRes = (expr)) != S_OK) { add_log("%s(%d): %s failed (0x%X)", __FILE__, __LINE__, (func), (hRes)); goto ERROR_ABORT;}}

int IpcInit(unsigned id);
int IpcSend(const unsigned pitch, const unsigned height, const void *data);
void IpcDestroy(void);

HRESULT EndScene_Hook(IDirect3DDevice9* pDevice)
{
#if !(EMPTY_HOOK)
	HRESULT hRes;
	IDirect3DSwapChain9 *pSC = NULL;
	IDirect3DSurface9 *pBB = NULL;
	int bEndHooking = 0;
	
	EnterCriticalSection(&s_csRenderTarget);

	if (s_pSurface0 == NULL) {
		D3DPRESENT_PARAMETERS pp;
		const D3DPOOL mem_pool = D3DPOOL_SYSTEMMEM;
#if USE_DDRAW
		DDSURFACEDESC2 surfdesc;
		DDPIXELFORMAT ddpf;
#endif

		if (s_pSurface1) {
			s_pSurface1->Release();
			s_pSurface1 = NULL;
		}

		if (s_pSurface2) {
			s_pSurface2->Release();
			s_pSurface2 = NULL;
		}

		if (s_pSurface3) {
			s_pSurface3->Release();
			s_pSurface3 = NULL;
		}

		FORCE_OK(pDevice->GetSwapChain(0, &pSC), "GetSwapChain");
		FORCE_OK(pSC->GetPresentParameters(&pp), "GetPresentParameters");

		org_width = pp.BackBufferWidth;
		org_height = pp.BackBufferHeight;
		org_format = pp.BackBufferFormat;

		new_width = input_width;
		new_height = input_height;
		if (new_height == 0) new_height = 720;
		if (new_width == 0) new_width = (UINT)((double)(new_height * org_width) / (double)org_height + 0.5);

		FORCE_OK(pDevice->CreateOffscreenPlainSurface(
			org_width, org_height, 
			org_format, mem_pool, 
			&s_pSurface0, NULL ), "CreateOffscreenPlainSurface");

#if USE_DDRAW
		ZeroMemory(&ddpf, sizeof(ddpf));
		ddpf.dwSize       = sizeof(DDPIXELFORMAT);
		ddpf.dwFlags      = DDPF_FOURCC | DDPF_RGB;
		ddpf.dwFourCC     = org_format;
		//ddpf.dwRGBBitCount = 32;

		ZeroMemory(&surfdesc, sizeof(surfdesc));
		surfdesc.dwSize = sizeof(surfdesc);
		surfdesc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_CAPS;
		surfdesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN/* | DDSCAPS_SYSTEMMEMORY*/;
		surfdesc.dwWidth = org_width;
		surfdesc.dwHeight = org_height;
		//surfdesc.ddpfPixelFormat = ddpf;
		surfdesc.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
		surfdesc.ddpfPixelFormat.dwFourCC = org_format;
		//surfdesc.ddpfPixelFormat.dwRGBBitCount = 32;
		FORCE_OK(s_pDDraw->CreateSurface(&surfdesc, &s_pSurface1, NULL), "CreateSurface");
		//add_log("org_format: 0x%X", org_format);	// test

		ZeroMemory(&ddpf, sizeof(ddpf));
		ddpf.dwSize       = sizeof(DDPIXELFORMAT);
		ddpf.dwFlags      = DDPF_FOURCC | DDPF_RGB;
		ddpf.dwFourCC     = new_format;

		ZeroMemory(&surfdesc, sizeof(surfdesc));
		surfdesc.dwSize = sizeof(surfdesc);
		surfdesc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_CAPS;
		surfdesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN/* | DDSCAPS_SYSTEMMEMORY*/;
		surfdesc.dwWidth = new_width;
		surfdesc.dwHeight = new_height;
		//surfdesc.ddpfPixelFormat = ddpf;
		//surfdesc.ddpfPixelFormat.dwFlags = DDPF_RGB;
		//surfdesc.ddpfPixelFormat.dwRGBBitCount = 32;
		surfdesc.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
		surfdesc.ddpfPixelFormat.dwFourCC = new_format;
		FORCE_OK(s_pDDraw->CreateSurface(&surfdesc, &s_pSurface2, NULL), "CreateSurface");
#elif !(CLIP_TEST)
		// Off-screen plain surfaces are always lockable, regardless of their pool types.
		// D3DPOOL_DEFAULT is the appropriate pool for use with the IDirect3DDevice9::StretchRect and IDirect3DDevice9::ColorFill.
		FORCE_OK(pDevice->CreateOffscreenPlainSurface(
			org_width, org_height, 
			org_format, D3DPOOL_DEFAULT, 
			&s_pSurface1, NULL ), 
			"CreateOffscreenPlainSurface");

		FORCE_OK(pDevice->CreateOffscreenPlainSurface(
			new_width, new_height, 
			new_format, D3DPOOL_DEFAULT, 
			&s_pSurface2, NULL ), 
			"CreateOffscreenPlainSurface");
#endif
	}

#if USE_DDRAW
	if (s_pSurface0 != NULL && s_pSurface1 != NULL && s_pSurface2 != NULL) {
#elif !(CLIP_TEST)
	if (s_pSurface0 != NULL && s_pSurface1 != NULL) {
#else
	if (s_pSurface0 != NULL) {
#endif
		FORCE_OK(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBB), "GetBackBuffer");

#if USE_DDRAW
		D3DLOCKED_RECT lockedSrcRect;
		DDSURFACEDESC2 lockedDstDesc;

		FORCE_OK(pDevice->GetRenderTargetData(pBB, s_pSurface0), "GetRenderTargetData");
		FORCE_OK(s_pSurface0->LockRect(&lockedSrcRect, NULL, 0), "LockRect");
		ZeroMemory(&lockedDstDesc, sizeof(lockedDstDesc));
		lockedDstDesc.dwSize = sizeof(lockedDstDesc);
		//lockedDstDesc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH;
		//lockedDstDesc.dwWidth = org_width;
		//lockedDstDesc.dwHeight = org_height;
		FORCE_OK(s_pSurface1->Lock(NULL, &lockedDstDesc, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL), "Lock");

		const int srcPitch = lockedSrcRect.Pitch;
		const int dstPitch = lockedDstDesc.lPitch;
		const int minPitch = min(srcPitch, dstPitch);
		register unsigned char *pSrc = (unsigned char *)lockedSrcRect.pBits;
		register unsigned char *pDst = (unsigned char *)lockedDstDesc.lpSurface;
		register unsigned i;

		for (i = 0; i < org_height; i++) {
			memcpy(pDst, pSrc, minPitch);
			pSrc += srcPitch;
			pDst += dstPitch;
		}
		s_pSurface0->UnlockRect();
		s_pSurface1->Unlock(NULL);

		FORCE_OK(s_pSurface2->Blt(NULL, s_pSurface1, NULL, DDBLT_WAIT, NULL), "Blt");

		ZeroMemory(&lockedDstDesc, sizeof(lockedDstDesc));
		lockedDstDesc.dwSize = sizeof(lockedDstDesc);
		FORCE_OK(s_pSurface2->Lock(NULL, &lockedDstDesc, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT | DDLOCK_READONLY, NULL), "Lock");
		IpcSend(lockedDstDesc.lPitch, new_height, lockedDstDesc.lpSurface);
		s_pSurface2->Unlock(NULL);
#else
		D3DLOCKED_RECT lockedSrcRect;

		FORCE_OK(pDevice->GetRenderTargetData(pBB, s_pSurface0), "GetRenderTargetData");
#if (CLIP_TEST)
		FORCE_OK(s_pSurface0->LockRect(&lockedSrcRect, NULL, 0), "LockRect");
		bEndHooking = IpcSend(lockedSrcRect.Pitch, org_height, lockedSrcRect.pBits);
		s_pSurface0->UnlockRect();
#else
		FORCE_OK(pDevice->UpdateSurface(s_pSurface0, NULL, s_pSurface1, NULL), "UpdateSurface");
		FORCE_OK(pDevice->StretchRect(s_pSurface1, NULL, s_pSurface2, NULL, D3DTEXF_NONE), "StretchRect");
		FORCE_OK(s_pSurface2->LockRect(&lockedSrcRect, NULL, 0), "LockRect");
		IpcSend(lockedSrcRect.Pitch, new_height, lockedSrcRect.pBits);
		s_pSurface2->UnlockRect();
#endif

#endif
	}
	else if (s_pSurface0 != NULL) {
		s_pSurface0->Release();
		s_pSurface0 = NULL;
	}

ERROR_ABORT:
	if (pSC) pSC->Release();
	if (pBB) pBB->Release();
	//add_log(".");

	LeaveCriticalSection(&s_csRenderTarget);

#endif
	return pDevice->EndScene();
}

HRESULT Reset_Hook(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS *pPresentationParameters)
{
	//add_log("Reset_Hook: Called");	// test
#if !(EMPTY_HOOK)
	EnterCriticalSection(&s_csRenderTarget);
	//add_log("Reset_Hook: Called");	// test
	if (s_pSurface0) {
		s_pSurface0->Release();
		s_pSurface0 = NULL;
	}
	LeaveCriticalSection(&s_csRenderTarget);
#endif
	return pDevice->Reset(pPresentationParameters);
}

EXTERN_C __declspec(dllexport) void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO *pInfo)
{
	inject_payload *payload = NULL;
	ULONG ACLEntries[1] = {0};
	DWORD dw;

	if (!pInfo || pInfo->UserDataSize != sizeof(inject_payload)) {
		add_log("NativeInjectionEntryPoint: UserDataSize does not match");
        return;
	}

	payload = (inject_payload *)pInfo->UserData;

#if 1
	if (IpcInit(payload->id) != 0) {
		add_log("NativeInjectionEntryPoint: IpcInit failed");
		return;
	}
#endif

	input_width = payload->width;
	input_height = payload->height;

	InitializeCriticalSection(&s_csRenderTarget);

#if USE_DDRAW
	IDirectDraw *pDDraw = NULL;
	if (DirectDrawCreate(NULL, &pDDraw, NULL) != DD_OK) {
		add_log("NativeInjectionEntryPoint: DirectDrawCreate failed");
        goto ERROR_ABORT;
	}
	if (pDDraw->QueryInterface(IID_IDirectDraw7, (LPVOID*)&s_pDDraw) != S_OK) {
		add_log("NativeInjectionEntryPoint: IDirectDraw::QueryInterface(IID_IDirectDraw7) failed");
        goto ERROR_ABORT;
	}
	pDDraw->Release();
	if (s_pDDraw->SetCooperativeLevel(NULL, DDSCL_NORMAL) != DD_OK) {
		add_log("NativeInjectionEntryPoint: IDirectDraw7::SetCooperativeLevel failed");
        goto ERROR_ABORT;
	}
#endif


	/*
	The following shows how to install and remove local hooks...
	*/
#if (INSTALL_HOOK)
	if (LhInstallHook(
		GetD3D9DeviceFunctionAddress(42)/*MmGetSystemRoutineAddress(&SymbolName)*/,
		EndScene_Hook,
		NULL,
		&hHook_EndScene) != 0) {
		add_log("NativeInjectionEntryPoint: LhInstallHook failed");
        goto ERROR_ABORT;
	}

	if (LhInstallHook(
		GetD3D9DeviceFunctionAddress(16)/*MmGetSystemRoutineAddress(&SymbolName)*/,
		Reset_Hook,
		NULL,
		&hHook_Reset) != 0) {
		add_log("NativeInjectionEntryPoint: LhInstallHook failed");
        goto ERROR_ABORT;
	}

	// activate the hook for the current thread
	//LhSetInclusiveACL(ACLEntries, 1, &hHook_EndScene);

	// activate the hook for all threads
#if 1
	if (LhSetExclusiveACL(ACLEntries, 0, &hHook_EndScene) != 0) {
		add_log("NativeInjectionEntryPoint: LhSetExclusiveACL failed");
        goto ERROR_ABORT;
	}
#endif
#if 1
	if (LhSetExclusiveACL(ACLEntries, 0, &hHook_Reset) != 0) {
		add_log("NativeInjectionEntryPoint: LhSetExclusiveACL failed");
        goto ERROR_ABORT;
	}
#endif

#endif

	if (payload->inject_type == inject_payload::INJECTOR_STARTER) {
        RhWakeUpProcess();
    }

	dw = WaitForSingleObject(s_hEvent, INFINITE);
	if (dw == WAIT_OBJECT_0) {
		add_log("\nNativeInjectionEntryPoint: Exiting...");
		LhUninstallAllHooks();
		LhWaitForPendingRemovals();

		//EnterCriticalSection(&s_csRenderTarget);
		if (s_pSurface0) {
			s_pSurface0->Release();
			s_pSurface0 = NULL;
		}
		if (s_pSurface1) {
			s_pSurface1->Release();
			s_pSurface1 = NULL;
		}
		if (s_pSurface2) {
			s_pSurface2->Release();
			s_pSurface2 = NULL;
		}
		//LeaveCriticalSection(&s_csRenderTarget);

		DeleteCriticalSection(&s_csRenderTarget);
		IpcDestroy();
	}

	return;

ERROR_ABORT:
	add_log("\nNativeInjectionEntryPoint: Aborting...");
	LhUninstallAllHooks();
	LhWaitForPendingRemovals();
	DeleteCriticalSection(&s_csRenderTarget);
	IpcDestroy();
#if USE_DDRAW
	if (s_pDDraw) {
		s_pDDraw->Release();
		s_pDDraw = NULL;
	}
#endif
	return;
}


#if (LINE_TEST)
static unsigned char s_TestBuf[BUF_SIZE];
#endif

static int IpcInit(unsigned id)
{
	TCHAR szName[128];

	_stprintf(szName, _T("%s%u"), s_szPrefix, id);

	s_hMapFile = OpenFileMapping(
		FILE_MAP_WRITE,
		FALSE,                 // do not inherit the name
		szName);               // name of mapping object 

	if (s_hMapFile == NULL) 
	{ 
		return 1;
	} 

	s_pBuf = MapViewOfFile(
		s_hMapFile,
		FILE_MAP_WRITE,
		0,                    
		0,                    
		BUF_SIZE);                   

	if (s_pBuf == NULL) 
	{ 
		CloseHandle(s_hMapFile);
		s_hMapFile = NULL;

		return 1;
	}

	_stprintf(szName, _T("%sMutex%u"), s_szPrefix, id);

	s_hMutex = OpenMutex(SYNCHRONIZE, FALSE, szName);
	if (s_hMutex == NULL)
	{
		UnmapViewOfFile(s_pBuf);
		s_pBuf = NULL;

		CloseHandle(s_hMapFile);
		s_hMapFile = NULL;

		return 1;
	}

	_stprintf(szName, _T("%sEvent%u"), s_szPrefix, id);

	s_hEvent = OpenEvent(SYNCHRONIZE, FALSE, szName);
	if (s_hEvent == NULL)
	{
		UnmapViewOfFile(s_pBuf);
		s_pBuf = NULL;

		CloseHandle(s_hMapFile);
		s_hMapFile = NULL;

		CloseHandle(s_hMutex);
		s_hMutex = NULL;

		return 1;
	}

#if (LINE_TEST)
	int i;
	memset(s_TestBuf, 0, BUF_SIZE);
	for (i = 2; i < BUF_SIZE; i+= DST_RGB_BYTES * 2) {
		s_TestBuf[i] = 255;
	}
#endif

	return 0;
}

static void IpcDestroy(void)
{
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
}

static int IpcSend(const unsigned pitch, const unsigned org_height, const void *data)
{
	if (s_pBuf)
	{
		const unsigned width = input_width;
		const unsigned height = input_height;
		const unsigned width_byte = width * DST_RGB_BYTES;
		register unsigned char *pSrc = (unsigned char *)data;
		register unsigned char *pDst = (unsigned char *)s_pBuf;
		register unsigned i = 0;

		memcpy(pDst, &width, sizeof(width));
		pDst += sizeof(width);
		memcpy(pDst, &height, sizeof(height));
		pDst += sizeof(height);

#if 0
		if (pitch == width_byte) {
			//memcpy(pDst, s_pTest, pitch * height);	// not slow
			memcpy(pDst, data, pitch * height);		// slow
		}
		else {
			for (i = 0; i < height; i++) {
				memcpy(pDst, pSrc, width_byte);
				pSrc += pitch;
				pDst += width_byte;
			}
		}
#elif (LINE_TEST && CLIP_TEST)
		const unsigned clip_width_byte = min(width_byte, pitch);
		const unsigned clip_height = min(height, org_height);
		memset(pDst, 0, width_byte * height);
		pSrc = s_TestBuf;
		for (i = 0; i < clip_height; i++) {
			memcpy(pDst, pSrc, width_byte);
			pSrc += width_byte;
			pDst += width_byte;
		}
#elif (CLIP_TEST)
		const unsigned clip_width_byte = min(width_byte, pitch);
		const unsigned clip_height = min(height, org_height);
		DWORD dwWaitResult = WaitForSingleObject(s_hMutex, 10);

		if (dwWaitResult == WAIT_OBJECT_0) {
			memset(pDst, 0, width_byte * height);
			for (i = 0; i < clip_height; i++) {
				memcpy(pDst, pSrc, clip_width_byte);
				pSrc += pitch;
				pDst += width_byte;
			}
			ReleaseMutex(s_hMutex);
		}
		else if (dwWaitResult != WAIT_TIMEOUT) {
			return 1;
		}
#else
		for (i = 0; i < height; i++) {
			memcpy(pDst, pSrc, width_byte);
			pSrc += pitch;
			pDst += width_byte;
		}
#endif
	}

	return 0;
}
