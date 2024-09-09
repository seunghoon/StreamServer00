// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

//------------------------------------------------------------------------------
// File: AppStream.h
//
// Desc: DirectShow sample code - header file for CAppStream class.
//------------------------------------------------------------------------------


#ifndef __APPSTREAM_H__
#define __APPSTREAM_H__

#define DEMO_NAME           "Game Streamer"
#define DEMO_NAME_INACTIIVE	"Game Streamer - Inactive"
#define DEMO_NAME_RUNNING	"Game Streamer - Running"

#define SafeGlobalFree(p)    if((p)) { GlobalFree((p));  p = NULL; }

#pragma warning(disable:4100)  // Disable C4100: unreferenced formal parameter

///////////////////////////////////////////////////////////////////////////
// The following definitions come from the Platform SDK and are required if
// the application is being compiled with the headers from Visual C++ 6.0.
///////////////////////////////////////////////////////////////////////////
#ifndef GetWindowLongPtr
  #define GetWindowLongPtrA   GetWindowLongA
  #define GetWindowLongPtrW   GetWindowLongW
  #ifdef UNICODE
    #define GetWindowLongPtr  GetWindowLongPtrW
  #else
    #define GetWindowLongPtr  GetWindowLongPtrA
  #endif // !UNICODE
#endif // !GetWindowLongPtr

#ifndef GWLP_HINSTANCE
  #define GWLP_HINSTANCE      (-6)
#endif
///////////////////////////////////////////////////////////////////////////
// End Platform SDK definitions
///////////////////////////////////////////////////////////////////////////



class CMediaBuffer;

class CAppStream  
{
public:
    CAppStream();
    ~CAppStream();

	// My additions
	HRESULT Init(REFGUID rclsid, DMO_MEDIA_TYPE *pmt, HWND hDlg = NULL);
	HRESULT ProcessFrame(BYTE *pbInData, DWORD cbInSize, BYTE *pbOutData, DWORD *cbOutSize, bool& bKeyFrame);

	// No copy version
	void* GetInputBuffer(void) { return m_pbInputBuffer; }
	void* GetOutputBuffer(void)  { return m_pbOutputBuffer; }
	HRESULT ProcessFrame(DWORD cbInSize, DWORD *cbOutSize, bool& bKeyFrame);

private:
    IMediaObject        *m_pObject;
    IMediaObjectInPlace *m_pObjectInPlace;

	// My additions
	DMO_MEDIA_TYPE      m_mt_in;
	DMO_MEDIA_TYPE      m_mt_out;
	CMediaBuffer        *m_pInputBuffer;
	CMediaBuffer        *m_pOutputBuffer;
	BYTE				*m_pbInputBuffer;
	BYTE				*m_pbOutputBuffer;
};

#pragma warning(disable:4512)   // C4512: assignment operator could not be generated

//  CMediaBuffer object
class CMediaBuffer : public IMediaBuffer
{
public:
    CMediaBuffer(DWORD cbMaxLength) :
        m_cRef(0),
        m_cbMaxLength(cbMaxLength),
        m_cbLength(0),
        m_pbData(NULL)
    {
    }

    ~CMediaBuffer()
    {
        if (m_pbData) {
#if 0
            delete [] m_pbData;
#else
			_aligned_free(m_pbData);
#endif
        }
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (ppv == NULL) {
            return E_POINTER;
        }
        if (riid == IID_IMediaBuffer || riid == IID_IUnknown) {
            *ppv = static_cast<IMediaBuffer *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG) Release()
    {
        LONG lRef = InterlockedDecrement(&m_cRef);
        if (lRef == 0) {
            delete this;
        }
        return lRef;
    }

    STDMETHODIMP SetLength(DWORD cbLength)
    {
        if (cbLength > m_cbMaxLength) {
            return E_INVALIDARG;
        } else {
            m_cbLength = cbLength;
            return S_OK;
        }
    }

    STDMETHODIMP GetMaxLength(DWORD *pcbMaxLength)
    {
        if (pcbMaxLength == NULL) {
            return E_POINTER;
        }
        *pcbMaxLength = m_cbMaxLength;
        return S_OK;
    }

    STDMETHODIMP GetBufferAndLength(BYTE **ppbBuffer, DWORD *pcbLength)
    {
        if (ppbBuffer == NULL || pcbLength == NULL) {
            return E_POINTER;
        }
        *ppbBuffer = m_pbData;
        *pcbLength = m_cbLength;
        return S_OK;
    }

    HRESULT Init()
    {
#if 0
        m_pbData = new BYTE[m_cbMaxLength];
#else
		m_pbData = (BYTE*)_aligned_malloc(m_cbMaxLength, 16);
#endif
        if (NULL == m_pbData) {
            return E_OUTOFMEMORY;
        } else {
            return S_OK;
        }
    }

    DWORD        m_cbLength;
    const DWORD  m_cbMaxLength;
    LONG         m_cRef;
    BYTE         *m_pbData;
};

#pragma warning(default:4512)   // C4512: assignment operator could not be generated

HRESULT CreateBuffer( DWORD cbMaxLength, CMediaBuffer **ppBuffer );



#endif __APPSTREAM_H__
