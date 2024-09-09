// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

//------------------------------------------------------------------------------
// File: AppStream.cpp
//
// Desc: DirectShow sample code - implementation of CAppStream class.
//------------------------------------------------------------------------------

#include "..\stdafx.h"

#include <windows.h>
#include <mmsystem.h>

#include <malloc.h>
#include <mediaobj.h>

#include "Streams.h"		// My addition

#include "uuids.h"
#include "dmo.h"
#include "dmoimpl.h"		// My addition
#include "wave.h"
#include "appstream.h"
#include "dxutil.h"     //to use free memory macro
#include "..\Hooking\Helper.h"

//
// CAppStream - reads data from a WAV file, transforms it using a DMO and then
//              copies the results into an output buffer.
//
//-----------------------------------------------------------------------------
// Name: CAppStream::CAppStream()
// Desc: Constructor
//-----------------------------------------------------------------------------
CAppStream::CAppStream():
    m_pObject(NULL),
    m_pObjectInPlace(NULL)
{
	ZeroMemory(&m_mt_in, sizeof(m_mt_in));
	ZeroMemory(&m_mt_out, sizeof(m_mt_out));
	m_pInputBuffer = NULL;
	m_pOutputBuffer = NULL;
	m_pbInputBuffer = NULL;
	m_pbOutputBuffer = NULL;
}

//-----------------------------------------------------------------------------
// Name: CAppStream::~CAppStream()
// Desc: Destructor
//-----------------------------------------------------------------------------
CAppStream::~CAppStream()
{
	MoFreeMediaType(&m_mt_in);
	MoFreeMediaType(&m_mt_out);
	SAFE_RELEASE( m_pInputBuffer );
	SAFE_RELEASE( m_pOutputBuffer );

    SAFE_RELEASE( m_pObject);
    SAFE_RELEASE( m_pObjectInPlace);
}

HRESULT CAppStream::Init(REFGUID rclsid, DMO_MEDIA_TYPE *pmt, HWND hDlg)
{
	DWORD max_size, la, align;

    // create DMO
    HRESULT hr = CoCreateInstance(rclsid,
                         NULL,
                         CLSCTX_INPROC,
                         IID_IMediaObject,
                         (void **) &m_pObject);
    if ( FAILED( hr ) ){
        MessageBox( hDlg, TEXT("Can't create encoder DMO."), TEXT(DEMO_NAME),MB_OK|MB_ICONERROR );
        return hr;
    }

    hr = m_pObject->QueryInterface(IID_IMediaObjectInPlace, (void**)&m_pObjectInPlace);

	// Set input media type (should be freed with MoFreeMediaType)
	MoCopyMediaType(&m_mt_in, pmt);
    hr = m_pObject->SetInputType( 0,    //Input Stream index
                                  &m_mt_in,
                                  0 );  // Set, not Test.
    if ( FAILED( hr ) ){
        MessageBox( hDlg, TEXT("SetInputType failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
        return hr;
    }

	// Set output media type (should be freed with MoFreeMediaType) after setting input type.
    hr = m_pObject->GetOutputType( 0,       // Output Stream Index
									0,
                                   &m_mt_out);

    if ( FAILED( hr ) ){
       MessageBox( hDlg, TEXT("GetOutputType failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
       return hr;
    }

    hr = m_pObject->SetOutputType( 0,			// Output Stream Index
                                   &m_mt_out,
                                   0);			// Set, not Test.
    if ( FAILED( hr ) ){
       MessageBox( hDlg, TEXT("SetOutputType failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
       return hr;
    }

	// Create input media buffer
	hr = m_pObject->GetInputSizeInfo(0, &max_size, &la, &align);
    if ( FAILED( hr ) ){
       MessageBox( hDlg, TEXT("GetInputSizeInfo failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
       return hr;
    }
	//add_log("buffer size: %u", max_size);	// test
    hr = CreateBuffer(max_size, &m_pInputBuffer);
    if( FAILED( hr ) ){
		MessageBox( hDlg, TEXT("CreateBuffer failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
        return hr;
    }
	m_pInputBuffer->GetBufferAndLength(&m_pbInputBuffer, &la);

	// Create output media buffer
	hr = m_pObject->GetOutputSizeInfo(0, &max_size, &align);
    if ( FAILED( hr ) ){
       MessageBox( hDlg, TEXT("GetOutputSizeInfo failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
       return hr;
    }
    hr = CreateBuffer(max_size, &m_pOutputBuffer);
    if( FAILED( hr ) ){
		MessageBox( hDlg, TEXT("CreateBuffer failed."), TEXT(DEMO_NAME),MB_OK | MB_ICONERROR );
        return hr;
    }
	m_pOutputBuffer->GetBufferAndLength(&m_pbOutputBuffer, &la);

    return S_OK;
}

HRESULT CAppStream::ProcessFrame(DWORD cbInSize, DWORD *cbOutSize, bool& bKeyFrame)
{
	HRESULT hr = S_OK;
    BYTE*                   pBuffer;
    DWORD                   dwLength;
    const REFERENCE_TIME    rtStart     = 0;
    const REFERENCE_TIME    rtStop      = 0;

	DWORD					dwStatus=0;
	DMO_OUTPUT_DATA_BUFFER  dataBufferStruct;

	/* Input */
    hr = m_pInputBuffer->SetLength( cbInSize );
    if( FAILED( hr ) ){
        return hr;
    }

    hr = m_pObject->ProcessInput( 0,
                            m_pInputBuffer,
                            DMO_INPUT_DATA_BUFFERF_SYNCPOINT,
                            rtStart,
                            rtStop - rtStart);
    if( FAILED( hr ) ){
        return hr;
    }


	/* Output */
    hr = m_pOutputBuffer->SetLength( 0 );	// No header
    if( FAILED( hr ) ) {
        return hr;
    }

    dataBufferStruct.pBuffer      = m_pOutputBuffer;
    dataBufferStruct.dwStatus     = 0;  // No flag is set
    dataBufferStruct.rtTimestamp  = 0;  // not used in ProcessOutput()
    dataBufferStruct.rtTimelength = 0;  // not used in ProcessOutput()

    hr = m_pObject->ProcessOutput(  DMO_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER,
                                    1, //output buffer count
                                    &dataBufferStruct,
                                    &dwStatus );

	hr = m_pOutputBuffer->GetBufferAndLength( &pBuffer, &dwLength );
    if( FAILED( hr ) ){
        return hr;
    }

	*cbOutSize = dwLength;
	bKeyFrame = (dataBufferStruct.dwStatus & DMO_OUTPUT_DATA_BUFFERF_SYNCPOINT)? true : false;

	return hr;
}

HRESULT CAppStream::ProcessFrame(BYTE *pbInData, DWORD cbInSize, BYTE *pbOutData, DWORD *cbOutSize, bool& bKeyFrame)
{
	HRESULT hr = S_OK;
    BYTE* pBuffer;

	/* Input */
	pBuffer = (BYTE*)GetInputBuffer();
    CopyMemory(pBuffer, pbInData, cbInSize);

	ProcessFrame(cbInSize, cbOutSize, bKeyFrame);

	/* Output */
	pBuffer = (BYTE*)GetOutputBuffer();
    CopyMemory(pbOutData, pBuffer, *cbOutSize);

	return hr;
}

//-----------------------------------------------------------------------------
// Name: CreateBuffer()
// Desc: create a CMediaBuffer
//-----------------------------------------------------------------------------

HRESULT CreateBuffer(DWORD cbMaxLength, CMediaBuffer **ppBuffer)
{
    CMediaBuffer *pBuffer = new CMediaBuffer( cbMaxLength );

    if ( pBuffer == NULL || FAILED( pBuffer->Init() ) ) {
        delete pBuffer;
        *ppBuffer = NULL;

        return E_OUTOFMEMORY;
    }

    *ppBuffer = pBuffer;
    (*ppBuffer)->AddRef();

    return S_OK;
}
