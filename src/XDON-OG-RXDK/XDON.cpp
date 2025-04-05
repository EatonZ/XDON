// Copyright © Eaton Works 2025. All rights reserved.
// License: https://github.com/EatonZ/XDON/blob/main/LICENSE

#include "stdafx.h"
#include "XDON.h"

#pragma region

LPDIRECT3DDEVICE8 pd3dDevice;
LPDIRECT3DVERTEXBUFFER8 pVB; 
LPDIRECT3DTEXTURE8 pTexture;
//Order must match DEVICE_INDEX_INTERNAL.
DEVICE_INFO devices[] =
{
	{ "\\Device\\Harddisk0\\partition0", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\Harddisk1\\partition0", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\MU_0", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\MU_1", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\MU_2", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\MU_3", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\MU_4", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\MU_5", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\MU_6", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\MU_7", INVALID_HANDLE_VALUE, NULL },
	{ "\\Device\\CdRom0", INVALID_HANDLE_VALUE, NULL }
};
BOOLEAN mountedMUs[] =
{
	{ FALSE },
	{ FALSE },
	{ FALSE },
	{ FALSE },
	{ FALSE },
	{ FALSE },
	{ FALSE },
	{ FALSE }
};
const DWORD physicalMemoryAttributes = MAKE_XALLOC_ATTRIBUTES(
    0,                            //ObjectType
    FALSE,                        //HeapTracksAttributes
    FALSE,                        //MustSucceed
    FALSE,                        //FixedSize
    eXALLOCAllocatorId_GameMin,   //AllocatorID
    XALLOC_PHYSICAL_ALIGNMENT_4K, //Alignment
    XALLOC_MEMPROTECT_READWRITE,  //MemoryProtect
    FALSE,                        //ZeroInitialize
    XALLOC_MEMTYPE_PHYSICAL       //MemoryType
);
const DWORD heapMemoryAttributes = MAKE_XALLOC_ATTRIBUTES(
    0,                           //ObjectType
    FALSE,                       //HeapTracksAttributes
    FALSE,                       //MustSucceed
    FALSE,                       //FixedSize
    eXALLOCAllocatorId_GameMin,  //AllocatorID
    XALLOC_ALIGNMENT_16,         //Alignment
    XALLOC_MEMPROTECT_READWRITE, //MemoryProtect
    FALSE,                       //ZeroInitialize
    XALLOC_MEMTYPE_HEAP          //MemoryType
);
LPVOID sockDestroyBuffer = NULL;
#ifdef _DEBUG
BYTE verbositySetting = PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS | PRINT_VERBOSITY_FLAG_REQUESTS;
#else
BYTE verbositySetting = PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS;
#endif
//The reboot doesn't need to be done after writing to memory units because they will all be dismounted on exit.
BOOLEAN writeTookPlaceOnHDD = FALSE;

#pragma endregion Defs

#pragma region

VOID Print(PRINT_VERBOSITY_FLAG Verbosity, const PCHAR Format, ...)
{
	if ((Verbosity & verbositySetting) != Verbosity) return;

	CHAR formatStr[200];
	va_list args;
	va_start(args, Format);
	vsprintf(formatStr, Format, args);
	va_end(args);

	BOOLEAN am;
	SYSTEMTIME time;
	GetLocalTime(&time);
	if (time.wHour > 12)
	{
		time.wHour = time.wHour - 12;
		am = FALSE;
	}
	else if (time.wHour == 12) am = FALSE;
	else
	{
		time.wHour = time.wHour == 0 ? 12 : time.wHour;
		am = TRUE;
	}

	DbgPrint("[%d/%d/%d %02d:%02d:%02d %s] %s\n", time.wMonth, time.wDay, time.wYear, time.wHour, time.wMinute, time.wSecond, am ? "AM" : "PM", formatStr);
}

VOID FreeSocketThreadParam(PSOCKET_THREAD_PARAM ThreadParam)
{
    if (ThreadParam->Memories.RequestMemory != NULL) XMemFree(ThreadParam->Memories.RequestMemory, physicalMemoryAttributes);
    if (ThreadParam->Memories.ResponseMemory != NULL) XMemFree(ThreadParam->Memories.ResponseMemory, physicalMemoryAttributes);
    if (ThreadParam->Memories.ScratchMemory != NULL) XMemFree(ThreadParam->Memories.ScratchMemory, physicalMemoryAttributes);
    XMemFree(ThreadParam, heapMemoryAttributes);
}

PSOCKET_THREAD_PARAM CreateSocketThreadParam(SOCKET ClientSocket)
{
	PSOCKET_THREAD_PARAM socketThreadParam = (PSOCKET_THREAD_PARAM)XMemAlloc(sizeof(SOCKET_THREAD_PARAM), heapMemoryAttributes);
	if (socketThreadParam == NULL)
	{
		 Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to allocate memory for thread param.");
		 return NULL;
	}
	socketThreadParam->Memories.RequestMemory = XMemAlloc(REQUEST_MEMORY_SIZE, physicalMemoryAttributes);
	if (socketThreadParam->Memories.RequestMemory == NULL)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to allocate request memory of size %d: %d", REQUEST_MEMORY_SIZE, GetLastError());
		FreeSocketThreadParam(socketThreadParam);
		return NULL;
	}
	socketThreadParam->Memories.ResponseMemory = XMemAlloc(RESPONSE_MEMORY_SIZE, physicalMemoryAttributes);
	if (socketThreadParam->Memories.ResponseMemory == NULL)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to allocate response memory of size %d: %d", RESPONSE_MEMORY_SIZE, GetLastError());
        FreeSocketThreadParam(socketThreadParam);
		return NULL;
	}
	socketThreadParam->Memories.ScratchMemory = XMemAlloc(SCRATCH_MEMORY_SIZE, physicalMemoryAttributes);
	if (socketThreadParam->Memories.ScratchMemory == NULL)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to allocate scratch memory of size %d: %d", SCRATCH_MEMORY_SIZE, GetLastError());
        FreeSocketThreadParam(socketThreadParam);
		return NULL;
	}
	socketThreadParam->ClientSocket = ClientSocket;
	return socketThreadParam;
}

VOID MemSet(PVOID Dest, INT c, SIZE_T Count)
{
	memset(Dest, c, Count);
}

VOID MemZero(PVOID Dest, SIZE_T Count)
{
	MemSet(Dest, 0, Count);
}

VOID MemCpy(PVOID Dest, const PVOID Src, SIZE_T Count)
{
	memcpy(Dest, Src, Count);
}

BOOLEAN MemIsEmpty(const PVOID Memory, SIZE_T Size)
{
	PBYTE ptr = (PBYTE)Memory;
	while (Size > 0)
	{
		if (((SIZE_T)ptr & 7) == 0 && Size >= 8)
		{
			if (*(PULONGLONG)ptr != 0) return FALSE;
			ptr += 8;
			Size -= 8;
		}
		else
		{
			while (Size > 0)
			{
				if (*ptr != 0) return FALSE;
				ptr++;
				Size--;
				//If Memory is unaligned to start, this helps get back on the fast track.
				if (((SIZE_T)ptr & 7) == 0) break;
			}
		}
	}
	return TRUE;
}

HANDLE OpenDisk(const PSZ Path, BOOLEAN ReadOnly)
{
	ANSI_STRING str;
	RtlInitAnsiString(&str, Path);

	OBJECT_ATTRIBUTES oa;
	InitializeObjectAttributes(&oa, &str, OBJ_CASE_INSENSITIVE, NULL, NULL);

	HANDLE handle;
	IO_STATUS_BLOCK iosb;
	//For disks, NtOpenFile must be used (CreateFile doesn't work).
	NTSTATUS status = NtOpenFile(&handle, GENERIC_READ | (ReadOnly ? 0 : GENERIC_WRITE) | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
	if (!NT_SUCCESS(status))
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "OpenDisk (%s) failed: 0x%08X", Path, status);
		return INVALID_HANDLE_VALUE;
	}
	else return handle;
}

BOOLEAN GetDiskGeometry(HANDLE Handle, PDISK_GEOMETRY Geometry, BOOLEAN CDROM)
{
	IO_STATUS_BLOCK iosb;
	NTSTATUS status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &iosb, CDROM ? IOCTL_CDROM_GET_DRIVE_GEOMETRY : IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, Geometry, sizeof(DISK_GEOMETRY));
	if (!NT_SUCCESS(status))
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "GetDiskGeometry failed: 0x%08X", status);
		return FALSE;
	}
	else return TRUE;
}

NTSTATUS ReadWriteDisk(DEVICE_INDEX_EXTERNAL ExternalDeviceIndex, LONGLONG Offset, ULONG Length, PVOID Buffer, BOOLEAN Write)
{
	//No need to validate the handle here; NtRead/WriteFile will return STATUS_INVALID_HANDLE (0xC0000008) if the client has specified a device index that is not connected.

	DEVICE_INDEX_INTERNAL internalDeviceIndex = (DEVICE_INDEX_INTERNAL)ExternalDeviceIndex;
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER offset;
	offset.QuadPart = Offset;
	NTSTATUS status = STATUS_SUCCESS;
    
    DWORD waitResult = WaitForSingleObject(devices[internalDeviceIndex].Mutex, INFINITE);
    assert(waitResult == WAIT_OBJECT_0);
    if (waitResult != WAIT_OBJECT_0) return devices[internalDeviceIndex].Mutex == NULL ? STATUS_INVALID_HANDLE : STATUS_UNSUCCESSFUL;

	if (Write)
	{
		status = NtWriteFile(devices[internalDeviceIndex].Handle, NULL, NULL, NULL, &iosb, Buffer, Length, &offset);
		if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "WriteDisk (Offset=0x%016I64X, Length=0x%08X) failed: 0x%08X", offset.QuadPart, Length, status);
	}
	else
	{
		status = NtReadFile(devices[internalDeviceIndex].Handle, NULL, NULL, NULL, &iosb, Buffer, Length, &offset);
		if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReadDisk (Offset=0x%016I64X, Length=0x%08X) failed: 0x%08X", offset.QuadPart, Length, status);
	}
    BOOL result = ReleaseMutex(devices[internalDeviceIndex].Mutex);
    assert(result);
	return status;
}

VOID CheckMU(PHANDLE Handle, PBOOLEAN Availability, PDISK_GEOMETRY Geometry)
{
	*Availability = FALSE;

	if (*Handle == INVALID_HANDLE_VALUE) return;

	//Getting disk geometry is enough to determine whether a valid device is connected.
	*Availability = GetDiskGeometry(*Handle, Geometry, FALSE);
	if (!*Availability)
	{
		NtClose(*Handle);
		*Handle = INVALID_HANDLE_VALUE;
	}
}

VOID Identify(PXDON_COMMAND_IDENTIFY_RESPONSE Response)
{
	MemZero(Response, sizeof(XDON_COMMAND_IDENTIFY_RESPONSE));

    Response->XDONVersion = XDON_VERSION;
	MemCpy(&Response->XboxVersion, XboxKrnlVersion, sizeof(XBOX_KRNL_VERSION));
	MemCpy(&Response->XboxHardwareInfo, XboxHardwareInfo, sizeof(XBOX_HARDWARE_INFO));
    
	Response->MaxIOLength = MAX_IO_LENGTH;
}

VOID GetDevices(PXDON_COMMAND_GET_DEVICES_RESPONSE Response)
{
	MemZero(Response, sizeof(XDON_COMMAND_GET_DEVICES_RESPONSE));
    
	for (BYTE i = 0; i < ARRAYSIZE(devices); i++)
	{
        //Set up the mutexes. These prevent an issue where IO operations are ongoing and GetDevices is called. When that happens, the handles will be closed and result in errors.
        if (devices[i].Mutex == NULL)
        {
            devices[i].Mutex = CreateMutex(NULL, TRUE, NULL);
            assert(devices[i].Mutex != NULL);
            //Fail will be checked for in ReadWriteDisk.
        }
        else
        {
            DWORD waitResult = WaitForSingleObject(devices[i].Mutex, INFINITE);
            assert(waitResult == WAIT_OBJECT_0);
        }
    }

    OBJECT_STRING muName;
    CHAR muNameBuffer[13];
    muName.Length = 0;
    muName.MaximumLength = sizeof(muNameBuffer);
    muName.Buffer = muNameBuffer;
    for (BYTE i = 0; i < ARRAYSIZE(mountedMUs); i += 2) 
    {
        //Refresh view of memory units. MU_CreateDeviceObject is used over XMountMU because XMountMU creates a title directory structure via XapiMapLetterToDirectory (don't want that).
        if (mountedMUs[i])
		{
			MU_CloseDeviceObject(i / 2, XDEVICE_TOP_SLOT);
			mountedMUs[i] = FALSE;
		}        
        mountedMUs[i] = NT_SUCCESS(MU_CreateDeviceObject(i / 2, XDEVICE_TOP_SLOT, &muName));
        
        if (mountedMUs[i + 1])
		{
			MU_CloseDeviceObject(i / 2, XDEVICE_BOTTOM_SLOT);
			mountedMUs[i + 1] = FALSE;
		}
        mountedMUs[i + 1] = NT_SUCCESS(MU_CreateDeviceObject(i / 2, XDEVICE_BOTTOM_SLOT, &muName));
    }

	for (BYTE i = 0; i < ARRAYSIZE(devices); i++)
	{
		//Close any existing handles.
		if (devices[i].Handle != INVALID_HANDLE_VALUE)
		{
			NtClose(devices[i].Handle);
			devices[i].Handle = INVALID_HANDLE_VALUE;
		}

		//Open fresh, new handles.
		devices[i].Handle = OpenDisk(devices[i].Path, i == DEVICE_INDEX_INTERNAL_CDROM0);
	}

	HANDLE handle = devices[DEVICE_INDEX_INTERNAL_HARDDISK0_PARTITION0].Handle;
	if (handle != INVALID_HANDLE_VALUE)
	{
		Response->AvailableDevices.Harddisk0 = GetDiskGeometry(handle, &Response->Harddisk0Geometry, FALSE);
		if (Response->AvailableDevices.Harddisk0)
		{
			ATA_PASS_THROUGH ataPT;
			MemZero(&ataPT.IdeReg, sizeof(ataPT.IdeReg));
			ataPT.IdeReg.bCommandReg = IDE_COMMAND_IDENTIFY_DEVICE;
			ataPT.DataBufferSize = sizeof(Response->Harddisk0Info);
			ataPT.DataBuffer = &Response->Harddisk0Info;
			
			IO_STATUS_BLOCK iosb;
			NTSTATUS status = NtDeviceIoControlFile(handle, NULL, NULL, NULL, &iosb, IOCTL_IDE_PASS_THROUGH, &ataPT, sizeof(ataPT), &ataPT, sizeof(ataPT));
			if (NT_SUCCESS(status)) Response->Harddisk0InfoAvailable = TRUE;
			else Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to get Harddisk0 info: 0x%08X", status);
		}
		else
		{
			NtClose(handle);
			devices[DEVICE_INDEX_INTERNAL_HARDDISK0_PARTITION0].Handle = INVALID_HANDLE_VALUE;
		}
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Harddisk0 available: %s", Response->AvailableDevices.Harddisk0 ? "YES" : "NO");

	handle = devices[DEVICE_INDEX_INTERNAL_HARDDISK1_PARTITION0].Handle;
	if (handle != INVALID_HANDLE_VALUE)
	{
		Response->AvailableDevices.Harddisk1 = GetDiskGeometry(handle, &Response->Harddisk1Geometry, FALSE);
		if (Response->AvailableDevices.Harddisk1)
		{
			ATA_PASS_THROUGH ataPT;
			MemZero(&ataPT.IdeReg, sizeof(ataPT.IdeReg));
			ataPT.IdeReg.bCommandReg = IDE_COMMAND_IDENTIFY_DEVICE;
			ataPT.DataBufferSize = sizeof(Response->Harddisk1Info);
			ataPT.DataBuffer = &Response->Harddisk1Info;
			
			IO_STATUS_BLOCK iosb;
			NTSTATUS status = NtDeviceIoControlFile(handle, NULL, NULL, NULL, &iosb, IOCTL_IDE_PASS_THROUGH, &ataPT, sizeof(ataPT), &ataPT, sizeof(ataPT));
			if (NT_SUCCESS(status)) Response->Harddisk1InfoAvailable = TRUE;
			else Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to get Harddisk1 info: 0x%08X", status);
		}
		else
		{
			NtClose(handle);
			devices[DEVICE_INDEX_INTERNAL_HARDDISK1_PARTITION0].Handle = INVALID_HANDLE_VALUE;
		}
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Harddisk1 available: %s", Response->AvailableDevices.Harddisk1 ? "YES" : "NO");

	BOOLEAN availability;
	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU0].Handle, &availability, &Response->Mu0Geometry);
	Response->AvailableDevices.Mu0 = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu0 available: %s", Response->AvailableDevices.Mu0 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU1].Handle, &availability, &Response->Mu1Geometry);
	Response->AvailableDevices.Mu1 = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu1 available: %s", Response->AvailableDevices.Mu1 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU2].Handle, &availability, &Response->Mu2Geometry);
	Response->AvailableDevices.Mu2 = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu2 available: %s", Response->AvailableDevices.Mu2 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU3].Handle, &availability, &Response->Mu3Geometry);
	Response->AvailableDevices.Mu3 = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu3 available: %s", Response->AvailableDevices.Mu3 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU4].Handle, &availability, &Response->Mu4Geometry);
	Response->AvailableDevices.Mu4 = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu4 available: %s", Response->AvailableDevices.Mu4 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU5].Handle, &availability, &Response->Mu5Geometry);
	Response->AvailableDevices.Mu5 = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu5 available: %s", Response->AvailableDevices.Mu5 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU6].Handle, &availability, &Response->Mu6Geometry);
	Response->AvailableDevices.Mu6 = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu6 available: %s", Response->AvailableDevices.Mu6 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU7].Handle, &availability, &Response->Mu7Geometry);
	Response->AvailableDevices.Mu7 = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu7 available: %s", Response->AvailableDevices.Mu7 ? "YES" : "NO");

	handle = devices[DEVICE_INDEX_INTERNAL_CDROM0].Handle;
	if (handle != INVALID_HANDLE_VALUE)
	{
		Response->AvailableDevices.CdRom0 = GetDiskGeometry(handle, &Response->CdRom0Geometry, TRUE);
        //SCSIOP_INQUIRY doesn't seem to work on OG Xbox (STATUS_INVALID_DEVICE_REQUEST), so there is no additional info to get like on Xbox 360.
		if (!Response->AvailableDevices.CdRom0)
		{
			NtClose(handle);
			devices[DEVICE_INDEX_INTERNAL_CDROM0].Handle = INVALID_HANDLE_VALUE;
		}
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "CdRom0 available: %s", Response->AvailableDevices.CdRom0 ? "YES" : "NO");
    
    for (BYTE i = 0; i < ARRAYSIZE(devices); i++)
    {
        if (devices[i].Mutex != NULL)
        {
            BOOL result = ReleaseMutex(devices[i].Mutex);
            assert(result);
        }
    }
}

VOID HalWriteSMCLEDStates(ULONG LEDStates)
{
    HalWriteSMBusByte(SMC_SLAVE_ADDRESS, SMC_COMMAND_LED_STATES, LEDStates);
    HalWriteSMBusByte(SMC_SLAVE_ADDRESS, SMC_COMMAND_LED_OVERRIDE, SMC_LED_OVERRIDE_USE_REQUESTED_LED_STATES);
}

#pragma endregion Utilities

#pragma region

VOID InitDisplay()
{
	//Big thanks to EqUiNoX for helping with this display code.
    
	HANDLE handle = XGetSectionHandle("BKND.png");
    if (handle == INVALID_HANDLE_VALUE)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "InitDisplay: No BKND.png section. Did you rename it?");
		return;
	}

	DWORD imgSize = XGetSectionSize(handle);
	assert(imgSize != 0);
	PVOID img = XLoadSectionByHandle(handle);
	assert(img != NULL);

    UINT width = 640;
    UINT height = 480;
    DWORD flags = 0;
    DWORD videoFlags = XGetVideoFlags();
    if ((videoFlags & XC_VIDEO_FLAGS_HDTV_1080i) == XC_VIDEO_FLAGS_HDTV_1080i)
    {
        flags = D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_WIDESCREEN;
        width = 1920;
        height = 1080;
    }
    else if ((videoFlags & XC_VIDEO_FLAGS_HDTV_720p) == XC_VIDEO_FLAGS_HDTV_720p)
    {
        flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
        width = 1280;
        height = 720;
    }
    else if ((videoFlags & XC_VIDEO_FLAGS_HDTV_480p) == XC_VIDEO_FLAGS_HDTV_480p) flags = D3DPRESENTFLAG_PROGRESSIVE;
    
	D3DPRESENT_PARAMETERS d3dpp;
	MemZero(&d3dpp, sizeof(d3dpp));
	d3dpp.BackBufferWidth = width;
	d3dpp.BackBufferHeight = height;
	d3dpp.BackBufferFormat = D3DFMT_LIN_R5G6B5; //D3DFMT_LIN_X8R8G8B8 will not work for 1080i (D3DXCreateTextureFromFileInMemory fails). D3DFMT_LIN_R5G6B5 is an acceptable fallback given what is being displayed.
    d3dpp.Flags = flags;
	d3dpp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE_OR_IMMEDIATE;

	HRESULT hr = Direct3DCreate8(D3D_SDK_VERSION)->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &pd3dDevice);
	assert(!FAILED(hr));

	hr = D3DXCreateTextureFromFileInMemory(pd3dDevice, img, imgSize, &pTexture);
    assert(!FAILED(hr));

	D3DXMATRIX matProj;
	D3DXMatrixIdentity(&matProj);
    hr = pd3dDevice->SetTransform(D3DTS_PROJECTION, &matProj);
	assert(!FAILED(hr));

	D3DXMATRIX matView;
	D3DXMatrixIdentity(&matView);
    hr = pd3dDevice->SetTransform(D3DTS_VIEW, &matView);
	assert(!FAILED(hr));

	D3DXMATRIX matWorld;
	D3DXMatrixIdentity(&matWorld);
    hr = pd3dDevice->SetTransform(D3DTS_WORLD, &matWorld);
	assert(!FAILED(hr));

    VERTEX vertices[] =
    {
		{ -1.0f,  1.0f, 0.0f, 0.0f, 0.0f }, 
        {  1.0f,  1.0f, 0.0f, 1.0f, 0.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
		{  1.0f,  1.0f, 0.0f, 1.0f, 0.0f }, 
        {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f }
    };
    hr = pd3dDevice->CreateVertexBuffer(sizeof(vertices), 0, 0, 0, &pVB);
	assert(!FAILED(hr));

    PVERTEX pVertices;
    hr = pVB->Lock(0, 0, (PBYTE*)&pVertices, 0);
	assert(!FAILED(hr));
    memcpy(pVertices, vertices, sizeof(vertices));

    hr = pVB->Unlock();
	assert(!FAILED(hr));
    
    //On OG Xbox, we can get away with doing all this just once (no render loop).
    if (pTexture == NULL)
    {
        //Display a red screen if the image failed to load (usually a memory issue) to encourage people to report it so it can be investigated.
        hr = pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, D3DCOLOR_XRGB(255, 0, 0), 1.0f, 0);
        assert(!FAILED(hr));
    }
    else
    {
        hr = pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        assert(!FAILED(hr));
        hr = pd3dDevice->BeginScene();
        assert(!FAILED(hr));
        hr = pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetStreamSource(0, pVB, sizeof(VERTEX));
        assert(!FAILED(hr));
        hr = pd3dDevice->SetVertexShader(D3DFVF_XYZ | D3DFVF_TEX1);
        assert(!FAILED(hr));
        hr = pd3dDevice->SetTexture(0, pTexture);
        assert(!FAILED(hr));
        hr = pd3dDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
        assert(!FAILED(hr));   
    }
    hr = pd3dDevice->Present(NULL, NULL, NULL, NULL);
    assert(!FAILED(hr));
}

VOID UILoop()
{
	InitDisplay();

	HANDLE controllers[] =
	{
		{ INVALID_HANDLE_VALUE },
		{ INVALID_HANDLE_VALUE },
		{ INVALID_HANDLE_VALUE },
		{ INVALID_HANDLE_VALUE }
	};
	while (TRUE)
	{
		DWORD insertions;
		DWORD removals;
		if (XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &insertions, &removals))
		{
			for (BYTE i = 0; i < XGetPortCount(); i++)
			{
				DWORD mask = 1 << i;
                if ((removals & mask) == mask)
				{
                    if (controllers[i] != INVALID_HANDLE_VALUE)
                    {
                     	XInputClose(controllers[i]);
                        controllers[i] = INVALID_HANDLE_VALUE;
                    }
				}
				if ((insertions & mask) == mask) controllers[i] = XInputOpen(XDEVICE_TYPE_GAMEPAD, i, XDEVICE_NO_SLOT, NULL);
			}
		}

		XINPUT_STATE state;
		for (BYTE i = 0; i < XGetPortCount(); i++)
		{
            if (controllers[i] == INVALID_HANDLE_VALUE) continue;
            
			MemZero(&state, sizeof(state));
			if (XInputGetState(controllers[i], &state) == ERROR_SUCCESS)
			{
				if (state.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_B] > XINPUT_GAMEPAD_MAX_CROSSTALK)
				{
                    //Skeleton Key is a new device that came out in 2025 to make it easier for people to mod their Xboxes. They requested a cold reboot every time.
                    #ifdef _SKELETONKEY
                    Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Cold rebooting.");
                    HalReturnToFirmware(HalRebootRoutine);
                    #else
                    if (writeTookPlaceOnHDD)
                    {
                        Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Cold rebooting since a disk write happened.");
                        HalReturnToFirmware(HalRebootRoutine);
                    }
                    else
                    {
                        Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Rebooting to dashboard.");
                        LD_LAUNCH_DASHBOARD ld;
                        MemZero(&ld, sizeof(ld));
                        ld.dwReason = XLD_LAUNCH_DASHBOARD_MAIN_MENU;
                        XLaunchNewImage(NULL, (PLAUNCH_DATA)&ld);
                    }
                    #endif
                    return;
				}
			}
		}
	}
}

#pragma endregion Display / UI

#pragma region

BOOLEAN SockSetOptions(SOCKET Socket, BOOLEAN Reuse, BOOLEAN SetInsecure, BOOLEAN AllowBroadcast, INT SetTimeouts)
{
	INT setting;

	if (Reuse)
	{
		setting = 1;
		if (setsockopt(Socket, SOL_SOCKET, SO_REUSEADDR, (const PCHAR)&setting, sizeof(setting)) == SOCKET_ERROR)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SockSetOptions (%X): setsockopt (SO_REUSEADDR) failed: %d", Socket, WSAGetLastError());
			return FALSE;
		}
	}
    
    //On OG Xbox, requires linkage with xonline.lib, else WSAENOPROTOOPT. When linking with xnet.lib, use XNET_STARTUP_BYPASS_SECURITY with XNetStartup instead.
	if (SetInsecure)
	{
		setting = 1;
		if (setsockopt(Socket, SOL_SOCKET, SO_INSECURE, (const PCHAR)&setting, sizeof(setting)) == SOCKET_ERROR)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SockSetOptions (%X): setsockopt (SO_INSECURE) failed: %d", Socket, WSAGetLastError());
			return FALSE;
		}
	}

	if (AllowBroadcast)
	{
		setting = 1;
		if (setsockopt(Socket, SOL_SOCKET, SO_BROADCAST, (const PCHAR)&setting, sizeof(setting)) == SOCKET_ERROR)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SockSetOptions (%X): setsockopt (SO_BROADCAST) failed: %d", Socket, WSAGetLastError());
			return FALSE;
		}
	}

	if (SetTimeouts >= 0)
	{
		setting = SetTimeouts;
		if (setsockopt(Socket, SOL_SOCKET, SO_SNDTIMEO, (const PCHAR)&setting, sizeof(setting)) == SOCKET_ERROR)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SockSetOptions (%X): setsockopt (SO_SNDTIMEO) failed: %d", Socket, WSAGetLastError());
			return FALSE;
		}

		if (setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const PCHAR)&setting, sizeof(setting)) == SOCKET_ERROR)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SockSetOptions (%X): setsockopt (SO_RCVTIMEO) failed: %d", Socket, WSAGetLastError());
			return FALSE;
		}
	}

	return TRUE;
}

//Data2 is optional.
BOOLEAN SockSend(SOCKET Socket, const PVOID Data1, INT Data1Length, const PVOID Data2, INT Data2Length, const PSOCKADDR_IN To, INT ToLen)
{
	WSABUF wsaBufs[2];
	wsaBufs[0].len = Data1Length;
	wsaBufs[0].buf = (PCHAR)Data1;
	wsaBufs[1].len = Data2Length;
	wsaBufs[1].buf = (PCHAR)Data2;

	//UDP packets larger than this won't be received.
	if (To != NULL && Data1Length + Data2Length > 1472) Print(PRINT_VERBOSITY_FLAG_SOCK_SEND_RECV, "SockSend (%X): Large UDP send detected (%d). This likely won't be received by clients.", Socket, Data1Length + Data2Length);

	INT wsaBufCount = Data2 == NULL ? 1 : 2;
	INT wsaBufIndex = 0;
	DWORD dataSentTotal = 0;
	while (wsaBufCount > 0)
	{
		DWORD dataSent;
		INT err = WSASendTo(Socket, wsaBufs + wsaBufIndex, wsaBufCount, &dataSent, 0, (const sockaddr*)To, ToLen, NULL, NULL);
		if (err == SOCKET_ERROR) return FALSE;
		else dataSentTotal += dataSent;
		Print(PRINT_VERBOSITY_FLAG_SOCK_SEND_RECV, "SockSend (%X): %d/%d (%d chunk)", Socket, dataSentTotal, Data1Length + Data2Length, dataSent);
		while (dataSent > 0)
		{
			DWORD dataSentFromBuf = min(dataSent, wsaBufs[wsaBufIndex].len);
			wsaBufs[wsaBufIndex].len -= dataSentFromBuf;
            if (wsaBufs[wsaBufIndex].len == 0)
			{
				wsaBufCount--;
				wsaBufIndex++;
			}
            else wsaBufs[wsaBufIndex].buf += dataSentFromBuf;
            dataSent -= dataSentFromBuf;
		}
	}
	return TRUE;
}

BOOLEAN SockReceive(SOCKET Socket, PVOID DataOut, INT DataLength, PSOCKADDR_IN FromOut, PINT FromLenOut)
{ 
	INT totalRecv = 0;
	INT dataOutOffset = 0;
	while (totalRecv < DataLength)
	{
		INT recvLen = DataLength - totalRecv;
		recvLen = recvfrom(Socket, (PCHAR)DataOut + dataOutOffset, recvLen, 0, (sockaddr*)FromOut, FromLenOut);
		if (recvLen == SOCKET_ERROR) return FALSE;
		if (recvLen == 0)
		{
			WSASetLastError(WSAECONNRESET);
			return FALSE;
		}
		dataOutOffset += recvLen;
		totalRecv += recvLen;
		Print(PRINT_VERBOSITY_FLAG_SOCK_SEND_RECV, "SockReceive (%X): %d/%d (%d chunk)", Socket, totalRecv, DataLength, recvLen);
	}
	return totalRecv == DataLength;
}

VOID SockDestroy(SOCKET Socket)
{
	if (shutdown(Socket, SD_SEND) == SOCKET_ERROR) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SockDestroy (%X): shutdown failed: %d", Socket, WSAGetLastError());
	INT result = recv(Socket, (PCHAR)sockDestroyBuffer, SOCKET_BUFFER_SIZE, 0);
	while (result != NO_ERROR && result != SOCKET_ERROR) result = recv(Socket, (PCHAR)sockDestroyBuffer, SOCKET_BUFFER_SIZE, 0);
	if (closesocket(Socket) == SOCKET_ERROR) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SockDestroy (%X): closesocket failed: %d", Socket, WSAGetLastError());
}

#pragma endregion Sockets

#pragma region

BOOLEAN InitNetwork()
{
	XNetStartupParams xnsp;
	MemZero(&xnsp, sizeof(xnsp));
	xnsp.cfgSizeOfStruct = sizeof(xnsp);
	xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY; //Equivalent to setsockopt with SO_INSECURE.
	xnsp.cfgSockDefaultRecvBufsizeInK = SOCKET_BUFFER_SIZE / 0x400; //Equivalent to setsockopt with SO_RCVBUF.
	xnsp.cfgSockDefaultSendBufsizeInK = SOCKET_BUFFER_SIZE / 0x400; //Equivalent to setsockopt with SO_SNDBUF.
	INT result = XNetStartup(&xnsp);
	if (result != NO_ERROR)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "InitNetwork: XNetStartup failed: %d", result);
		return FALSE;
	}

	WSADATA wsaData;
	result = WSAStartup(WINSOCK_VERSION, &wsaData);
	if (result != NO_ERROR)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "InitNetwork: WSAStartup failed: %d", result);
		XNetCleanup();
		return FALSE;
	}

	return TRUE;
}

VOID FulfillRequest(PSOCKET_THREAD_PARAM ThreadParam, const PSOCKADDR_IN To, INT ToLen, XDON_COMMAND CommandID)
{
	XDON_COMMAND_RESPONSE_FRAME frame;
	frame.Identifier = RESPONSE_FRAME_IDENTIFIER;
	frame.Version = PROTOCOL_VERSION;
	frame.ConsoleType = CONSOLE_TYPE;
	switch (CommandID)
	{
		case XDON_COMMAND_IDENTIFY:
			{
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Fulfilling XDON_COMMAND_IDENTIFY.", ThreadParam->ClientSocket);
				XDON_COMMAND_IDENTIFY_RESPONSE response;
				Identify(&response);
				frame.StatusCode = STATUS_SUCCESS;

				if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), &response, sizeof(response), To, ToLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Failed to send response: %d", ThreadParam->ClientSocket, WSAGetLastError());

				break;
			}
		case XDON_COMMAND_GET_DEVICES:
			{
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Fulfilling XDON_COMMAND_GET_DEVICES.", ThreadParam->ClientSocket);
				XDON_COMMAND_GET_DEVICES_RESPONSE response;
				GetDevices(&response);
				frame.StatusCode = STATUS_SUCCESS;

				if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), &response, sizeof(response), To, ToLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Failed to send response: %d", ThreadParam->ClientSocket, WSAGetLastError());

				break;
			}
		case XDON_COMMAND_READ:
			{
				PXDON_COMMAND_READ_REQUEST request = (PXDON_COMMAND_READ_REQUEST)ThreadParam->Memories.RequestMemory;
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Fulfilling XDON_COMMAND_READ (DeviceIndex=%d, Offset=0x%016I64X, Length=0x%08X, CompressResponseData=%d).", ThreadParam->ClientSocket, request->DeviceIndex, request->Offset, request->Length, request->CompressResponseData);
				PXDON_COMMAND_READ_RESPONSE response = (PXDON_COMMAND_READ_RESPONSE)ThreadParam->Memories.ResponseMemory;

				frame.StatusCode = ReadWriteDisk((DEVICE_INDEX_EXTERNAL)request->DeviceIndex, request->Offset, request->Length, request->CompressResponseData ? ThreadParam->Memories.ScratchMemory : response->Data, FALSE);

				INT responseDataLength = 0;
				if (NT_SUCCESS(frame.StatusCode))
				{
					response->DataLength = MemIsEmpty(request->CompressResponseData ? ThreadParam->Memories.ScratchMemory : response->Data, request->Length) ? 0 : request->Length;
					response->UncompressedDataLength = response->DataLength;
					if (response->DataLength == 0) Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): MemIsEmpty=TRUE", ThreadParam->ClientSocket);
					else if (request->CompressResponseData)
					{
						//The LZ4 code won't compile on OG right now.
                        frame.StatusCode = STATUS_NOT_SUPPORTED;
					}
					responseDataLength = frame.StatusCode == STATUS_NOT_SUPPORTED ? 0 : sizeof(XDON_COMMAND_READ_RESPONSE) + response->DataLength;
				}
				else Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): No response to send due to error.", ThreadParam->ClientSocket);

				if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), responseDataLength == 0 ? NULL : response, responseDataLength, To, ToLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Failed to send response: %d", ThreadParam->ClientSocket, WSAGetLastError());

				break;
			}
		case XDON_COMMAND_WRITE:
			{
				PXDON_COMMAND_WRITE_REQUEST request = (PXDON_COMMAND_WRITE_REQUEST)ThreadParam->Memories.RequestMemory;
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Fulfilling XDON_COMMAND_WRITE (DeviceIndex=%d, Offset=0x%016I64X, DataLength=0x%08X, DataIsCompressed=%d).", ThreadParam->ClientSocket, request->DeviceIndex, request->Offset, request->DataLength, request->DataIsCompressed);

				if (request->DataIsCompressed)
				{
					//The LZ4 code won't compile on OG right now.
                    frame.StatusCode = STATUS_NOT_SUPPORTED;
				}
				else frame.StatusCode = ReadWriteDisk((DEVICE_INDEX_EXTERNAL)request->DeviceIndex, request->Offset, request->DataLength, request->Data, TRUE);

				writeTookPlaceOnHDD = NT_SUCCESS(frame.StatusCode) && ((DEVICE_INDEX_EXTERNAL)request->DeviceIndex == DEVICE_INDEX_EXTERNAL_HARDDISK0 || (DEVICE_INDEX_EXTERNAL)request->DeviceIndex == DEVICE_INDEX_EXTERNAL_HARDDISK1);

				if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), NULL, 0, To, ToLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Failed to send response frame: %d", ThreadParam->ClientSocket, WSAGetLastError());

				break;
			}
		case XDON_COMMAND_WRITE_SAME:
			{
				PXDON_COMMAND_WRITE_SAME_REQUEST request = (PXDON_COMMAND_WRITE_SAME_REQUEST)ThreadParam->Memories.RequestMemory;
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Fulfilling XDON_COMMAND_WRITE_SAME (DeviceIndex=%d, Offset=0x%016I64X, Length=0x%08X, Value=0x%02X).", ThreadParam->ClientSocket, request->DeviceIndex, request->Offset, request->Length, request->Value);
				
				MemSet(ThreadParam->Memories.ScratchMemory, request->Value, request->Length);
				frame.StatusCode = ReadWriteDisk((DEVICE_INDEX_EXTERNAL)request->DeviceIndex, request->Offset, request->Length, ThreadParam->Memories.ScratchMemory, TRUE);

				writeTookPlaceOnHDD = NT_SUCCESS(frame.StatusCode) && ((DEVICE_INDEX_EXTERNAL)request->DeviceIndex == DEVICE_INDEX_EXTERNAL_HARDDISK0 || (DEVICE_INDEX_EXTERNAL)request->DeviceIndex == DEVICE_INDEX_EXTERNAL_HARDDISK1);

				if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), NULL, 0, To, ToLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Failed to send response frame: %d", ThreadParam->ClientSocket, WSAGetLastError());

				break;
			}
		case XDON_COMMAND_ATA_CUSTOM_COMMAND:
			{
				PXDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST request = (PXDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST)ThreadParam->Memories.RequestMemory;
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Fulfilling XDON_COMMAND_ATA_CUSTOM_COMMAND (Registers=%02X %02X %02X %02X %02X %02X %02X %02X, DataInOutSize=0x%08X).", ThreadParam->ClientSocket, request->Registers.bFeaturesReg, request->Registers.bSectorCountReg, request->Registers.bSectorNumberReg, request->Registers.bCylLowReg, request->Registers.bCylHighReg, request->Registers.bDriveHeadReg, request->Registers.bCommandReg, request->Registers.bHostSendsData, request->DataInOutSize);
				BOOLEAN dataOut = request->Registers.bHostSendsData;
				PXDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE response = (PXDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE)ThreadParam->Memories.ResponseMemory;

				ATA_PASS_THROUGH ataPT;
				MemCpy(&ataPT.IdeReg, &request->Registers, sizeof(request->Registers));
				ataPT.DataBufferSize = request->DataInOutSize;
				ataPT.DataBuffer = dataOut ? request->DataOut : response->DataIn;

				IO_STATUS_BLOCK iosb;
				frame.StatusCode = NtDeviceIoControlFile(devices[DEVICE_INDEX_INTERNAL_HARDDISK0_PARTITION0].Handle, NULL, NULL, NULL, &iosb, IOCTL_IDE_PASS_THROUGH, &ataPT, sizeof(ataPT), &ataPT, sizeof(ataPT));
				
				INT responseDataLength = 0;
				if (NT_SUCCESS(frame.StatusCode))
				{
					MemCpy(&response->Registers, &ataPT.IdeReg, sizeof(ataPT.IdeReg));
					responseDataLength = sizeof(XDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE) + (dataOut ? 0 : request->DataInOutSize);
				}
				else Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): No response to send due to error.", ThreadParam->ClientSocket);

				if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), responseDataLength == 0 ? NULL : response, responseDataLength, To, ToLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Failed to send response: %d", ThreadParam->ClientSocket, WSAGetLastError());

				break;
			}
		case XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE:
			{
				PXDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST request = (PXDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST)ThreadParam->Memories.RequestMemory;
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Fulfilling XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE.", ThreadParam->ClientSocket);
				frame.StatusCode = STATUS_SUCCESS;

				if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), NULL, 0, To, ToLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Failed to send response: %d", ThreadParam->ClientSocket, WSAGetLastError());

				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Executing routine %d right now.", ThreadParam->ClientSocket, request->Routine);
                //Let's use max to determine whether to shutdown. There is no other valid use for it so we can use it to keep the command the same as the Xbox 360 version.
                if ((FIRMWARE_REENTRY)request->Routine == HalMaximumRoutine) HalInitiateShutdown();
				else HalReturnToFirmware((FIRMWARE_REENTRY)request->Routine);

				break;
			}
	}
}

NTSTATUS ReceiveAndValidateRequest(PSOCKET_THREAD_PARAM ThreadParam, PSOCKADDR_IN FromOut, PINT FromLenOut, PXDON_COMMAND CommandIDOut)
{
	//First, read the frame.
	Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): Awaiting frame...", ThreadParam->ClientSocket);
	XDON_COMMAND_REQUEST_FRAME frame;
	if (!SockReceive(ThreadParam->ClientSocket, &frame, sizeof(frame), FromOut, FromLenOut))
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Request frame not received: %d", ThreadParam->ClientSocket, WSAGetLastError());
		return STATUS_UNSUCCESSFUL;
	}
	else
	{
		if (frame.Identifier != REQUEST_FRAME_IDENTIFIER)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Non-XDON data received; ignoring.", ThreadParam->ClientSocket);
			return STATUS_NOT_SUPPORTED;
		}
		if (frame.Version != PROTOCOL_VERSION)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Protocol version mismatch (received=%d, supported=%d).", ThreadParam->ClientSocket, frame.Version, PROTOCOL_VERSION);
			return STATUS_NOT_SUPPORTED;
		}
		else *CommandIDOut = (XDON_COMMAND)frame.CommandID;
	}
    
    if (FromOut != NULL && frame.CommandID != XDON_COMMAND_IDENTIFY)
    {
        Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Only XDON_COMMAND_IDENTIFY can be handled over UDP.", ThreadParam->ClientSocket);
        return STATUS_NOT_SUPPORTED;   
    }

	INT requestSize;
	switch (frame.CommandID)
	{
		case XDON_COMMAND_IDENTIFY:
			{
				requestSize = 0; //sizeof(XDON_COMMAND_IDENTIFY_REQUEST);
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): XDON_COMMAND_IDENTIFY frame received.", ThreadParam->ClientSocket);
				break;
			}
		case XDON_COMMAND_GET_DEVICES:
			{
				requestSize = 0; //sizeof(XDON_COMMAND_GET_DEVICES_REQUEST);
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): XDON_COMMAND_GET_DEVICES frame received.", ThreadParam->ClientSocket);
				break;
			}
		case XDON_COMMAND_READ:
			{
				requestSize = sizeof(XDON_COMMAND_READ_REQUEST);
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): XDON_COMMAND_READ frame received.", ThreadParam->ClientSocket);
				break;
			}
		case XDON_COMMAND_WRITE:
			{
				requestSize = sizeof(XDON_COMMAND_WRITE_REQUEST);
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): XDON_COMMAND_WRITE frame received.", ThreadParam->ClientSocket);
				break;
			}
		case XDON_COMMAND_WRITE_SAME:
			{
				requestSize = sizeof(XDON_COMMAND_WRITE_SAME_REQUEST);
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): XDON_COMMAND_WRITE_SAME frame received.", ThreadParam->ClientSocket);
				break;
			}
		case XDON_COMMAND_ATA_CUSTOM_COMMAND:
			{
				requestSize = sizeof(XDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST);
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): XDON_COMMAND_ATA_CUSTOM_COMMAND frame received.", ThreadParam->ClientSocket);
				break;
			}
		case XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE:
			{
				requestSize = sizeof(XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST);
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE frame received.", ThreadParam->ClientSocket);
				break;
			}
		default:
			{
				Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Frame received, but command ID %d not recognized.", ThreadParam->ClientSocket, frame.CommandID);
				return STATUS_INVALID_PARAMETER;
			}
	}

	if (requestSize > 0)
	{
		//Second, read the request (the part after the frame).
		if (!SockReceive(ThreadParam->ClientSocket, ThreadParam->Memories.RequestMemory, requestSize, FromOut, FromLenOut))
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Request not received: %d", ThreadParam->ClientSocket, WSAGetLastError());
			return STATUS_UNSUCCESSFUL;
		}
		else Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): Request received.", ThreadParam->ClientSocket);
	}
	else Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): No request to receive.", ThreadParam->ClientSocket);

	//Third, validate the request and read any variable-length request data.
	switch (frame.CommandID)
	{
		case XDON_COMMAND_IDENTIFY:
		case XDON_COMMAND_GET_DEVICES: goto successExit;
		case XDON_COMMAND_READ:
			{
				PXDON_COMMAND_READ_REQUEST readRequest = (PXDON_COMMAND_READ_REQUEST)ThreadParam->Memories.RequestMemory;
				//Let NtReadFile do the offset check.
				if (readRequest->DeviceIndex >= DEVICE_INDEX_EXTERNAL_MAX || readRequest->Length == 0 || readRequest->Length > MAX_IO_LENGTH) goto validationFailExit;
				else goto successExit;
			}
		case XDON_COMMAND_WRITE:
			{
				PXDON_COMMAND_WRITE_REQUEST writeRequest = (PXDON_COMMAND_WRITE_REQUEST)ThreadParam->Memories.RequestMemory;
				//Let NtWriteFile do the offset check.
				if (writeRequest->DeviceIndex >= DEVICE_INDEX_EXTERNAL_MAX || writeRequest->DataLength == 0 || writeRequest->DataLength > (writeRequest->DataIsCompressed ? (UINT)MAX_COMPRESSION_LENGTH : MAX_IO_LENGTH)) goto validationFailExit;
				else
				{
					if (!SockReceive(ThreadParam->ClientSocket, ((PXDON_COMMAND_WRITE_REQUEST)ThreadParam->Memories.RequestMemory)->Data, writeRequest->DataLength, FromOut, FromLenOut))
					{
						Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Request data not received: %d", ThreadParam->ClientSocket, WSAGetLastError());
						return STATUS_UNSUCCESSFUL;
					}
					
					goto successExit;
				}
			}
		case XDON_COMMAND_WRITE_SAME:
			{
				PXDON_COMMAND_WRITE_SAME_REQUEST writeSameRequest = (PXDON_COMMAND_WRITE_SAME_REQUEST)ThreadParam->Memories.RequestMemory;
				//Let NtWriteFile do the offset check.
				if (writeSameRequest->DeviceIndex >= DEVICE_INDEX_EXTERNAL_MAX || writeSameRequest->Length == 0 || writeSameRequest->Length > MAX_IO_LENGTH) goto validationFailExit;
				else goto successExit;
			}
		case XDON_COMMAND_ATA_CUSTOM_COMMAND:
			{
				PXDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST ataCustomCommandRequest = (PXDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST)ThreadParam->Memories.RequestMemory;
				if (ataCustomCommandRequest->DataInOutSize > MAX_IO_LENGTH) goto validationFailExit;
				if (ataCustomCommandRequest->Registers.bHostSendsData && ataCustomCommandRequest->DataInOutSize > 0)
				{
					if (!SockReceive(ThreadParam->ClientSocket, ((PXDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST)ThreadParam->Memories.RequestMemory)->DataOut, ataCustomCommandRequest->DataInOutSize, FromOut, FromLenOut))
					{
						Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Request data not received: %d", ThreadParam->ClientSocket, WSAGetLastError());
						return STATUS_UNSUCCESSFUL;
					}
				}
				goto successExit;
			}
		case XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE: goto successExit; //Let HalReturnToFirmware do the routine check.
		default: goto validationFailExit;
	}

	validationFailExit:
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Request failed validation.", ThreadParam->ClientSocket);
	return STATUS_INVALID_PARAMETER;

	successExit:
	if (requestSize > 0) Print(PRINT_VERBOSITY_FLAG_REQUESTS, "ReceiveAndValidateRequest (%X): Request passed validation.", ThreadParam->ClientSocket);
	return STATUS_SUCCESS;
}

VOID TCPSocketThread(PSOCKET_THREAD_PARAM ThreadParam)
{
    while (TRUE)
    {
        XDON_COMMAND commandID = XDON_COMMAND_UNKNOWN;
        NTSTATUS status = ReceiveAndValidateRequest(ThreadParam, NULL, NULL, &commandID);
        if (!NT_SUCCESS(status))
        {
            //If validation failed due to a network error, do not send anything because it's pointless.
            if (WSAGetLastError() == NO_ERROR)
            {
                XDON_COMMAND_RESPONSE_FRAME frame;
                frame.Identifier = RESPONSE_FRAME_IDENTIFIER;
                frame.Version = PROTOCOL_VERSION;
                frame.ConsoleType = CONSOLE_TYPE;
                frame.StatusCode = status;
                if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), NULL, 0, NULL, 0)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SocketThread (TCP/%X): Failed to send validation failure response frame: %d", ThreadParam->ClientSocket, WSAGetLastError());
            }
        }
        else FulfillRequest(ThreadParam, NULL, 0, commandID);
        if (WSAGetLastError() == WSAECONNRESET)
        {
            Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SocketThread (TCP/%X): Connection reset by peer; exiting.", ThreadParam->ClientSocket);
            SockDestroy(ThreadParam->ClientSocket);
            break;
        }
    }
    
    FreeSocketThreadParam(ThreadParam);
    
    Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "SocketThread (TCP/%X): Thread is exiting.", ThreadParam->ClientSocket);
}

VOID ServerMain()
{
    sockDestroyBuffer = XMemAlloc(SOCKET_BUFFER_SIZE, heapMemoryAttributes);
    if (sockDestroyBuffer == NULL)
    {
        Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: Failed to allocate memory for socket destroy buffer.");
		return;
    }
    
	SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serverSocket == INVALID_SOCKET)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: socket failed: %d", WSAGetLastError());
		return;
	}

	if (!SockSetOptions(serverSocket, TRUE, FALSE, FALSE, -1)) return;

	SOCKADDR_IN serverEndpoint;
	MemZero(&serverEndpoint, sizeof(serverEndpoint));
	serverEndpoint.sin_family = AF_INET;
	serverEndpoint.sin_port = htons(PC_COMMUNICATION_PORT);
	serverEndpoint.sin_addr.s_addr = INADDR_ANY;
	if (bind(serverSocket, (const sockaddr*)&serverEndpoint, sizeof(serverEndpoint)) == SOCKET_ERROR) 
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: bind failed: %d", WSAGetLastError());
		return;
	}

	if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: listen failed: %d", WSAGetLastError());
		return;
	}

	//Also set up identification answering socket.
	SOCKET idServerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (idServerSocket == INVALID_SOCKET)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: socket failed: %d", WSAGetLastError());
		return;
	}

	if (!SockSetOptions(idServerSocket, TRUE, FALSE, TRUE, -1)) return;

	if (bind(idServerSocket, (const sockaddr*)&serverEndpoint, sizeof(serverEndpoint)) == SOCKET_ERROR) 
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: bind failed: %d", WSAGetLastError());
		return;
	}

    HANDLE uiLoopThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)UILoop, NULL, 0, NULL);
	if (uiLoopThread == NULL)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: CreateThread (UILoop) failed: %d", GetLastError());
		return;
	}
    CloseHandle(uiLoopThread);

	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: ***READY***");

	fd_set rfds;
	while (TRUE)
	{
		FD_ZERO(&rfds);
		FD_SET(serverSocket, &rfds);
		FD_SET(idServerSocket, &rfds);

		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: Awaiting a new TCP client or UDP packet...");
		if (select(0, &rfds, NULL, NULL, NULL) == SOCKET_ERROR)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: select failed: %d; starting over.", WSAGetLastError());
			continue;
		}
		else if (FD_ISSET(serverSocket, &rfds))
		{
			SOCKET clientSocket = accept(serverSocket, NULL, NULL);
			if (clientSocket != INVALID_SOCKET)
			{
				Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (TCP): TCP client connected!");
				if (SockSetOptions(clientSocket, FALSE, FALSE, FALSE, SOCKET_TIMEOUT))
				{
                    PSOCKET_THREAD_PARAM socketThreadParam = CreateSocketThreadParam(clientSocket);
                    if (socketThreadParam == NULL)
                    {
                        Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (TCP): Failed to allocate memory; starting over.");
                        SockDestroy(clientSocket);   
                    }
                    else
                    {
                        HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TCPSocketThread, socketThreadParam, 0, NULL);
                        if (thread == NULL)
                        {
                            Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (TCP): CreateThread (SocketThread) failed: %d; starting over.", GetLastError());
                            SockDestroy(clientSocket);
                        }
                        else
                        {
                            Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (TCP): Thread started to handle socket %X; starting over.", clientSocket);
                            //Priority needs to be bumped up or else performance will be really bad!
                            SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
                            CloseHandle(thread);
                        }
                    }
				}
				else
				{
					Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (TCP): Socket options problem with the client; starting over.");
					SockDestroy(clientSocket);
				}
			}
			else Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (TCP): accept failed: %d", WSAGetLastError());
		}
		else if (FD_ISSET(idServerSocket, &rfds))
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (UDP): Handling a UDP packet.");
			//Reset the error for a fresh start in case something failed before.
			WSASetLastError(NO_ERROR);
			SOCKADDR_IN from;
			INT fromLen = sizeof(from);
			XDON_COMMAND commandID = XDON_COMMAND_UNKNOWN;
			SOCKET_THREAD_PARAM socketThreadParam;
            //Don't need to set Memories because this is only used for XDON_COMMAND_IDENTIFY, which does not need those allocations.
			socketThreadParam.ClientSocket = idServerSocket;
			NTSTATUS status = ReceiveAndValidateRequest(&socketThreadParam, &from, &fromLen, &commandID);
			if (!NT_SUCCESS(status))
			{
				XDON_COMMAND_RESPONSE_FRAME frame;
				frame.Identifier = RESPONSE_FRAME_IDENTIFIER;
				frame.Version = PROTOCOL_VERSION;
				frame.ConsoleType = CONSOLE_TYPE;
				frame.StatusCode = status;
				if (!SockSend(idServerSocket, &frame, sizeof(frame), NULL, 0, &from, fromLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (UDP): Failed to send validation failure response frame: %d", WSAGetLastError());
			}
			else FulfillRequest(&socketThreadParam, &from, fromLen, commandID);
			if (WSAGetLastError() != NO_ERROR) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (UDP): Connection problem detected with the UDP send.");		
		}
	}
}

#pragma endregion Network

VOID __cdecl main() 
{
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "XDON - Xbox Disk Over Network");
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Created by Eaton");
	#ifdef _DEBUG
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Version: %d Debug, built: %s %s", XDON_VERSION, __DATE__, __TIME__);
	#else
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Version: %d Release, built: %s %s", XDON_VERSION, __DATE__, __TIME__);
	#endif

	XDEVICE_PREALLOC_TYPE deviceTypes[] =
	{
	   { XDEVICE_TYPE_MEMORY_UNIT, XGetPortCount() * 2 },
       { XDEVICE_TYPE_GAMEPAD, XGetPortCount() }
	};
	XInitDevices(ARRAYSIZE(deviceTypes), deviceTypes);
    
	//Disable the Auto-Off feature to avoid unintended shutdowns. The next title will reinitialize it, so we don't have to change it back on exit.
	XapiAutoPowerDownGlobals.fAutoPowerDown = FALSE;

	if (!InitNetwork())
    {
        HalWriteSMCLEDStates(SMC_LED_STATES_RED_STATE0 | SMC_LED_STATES_RED_STATE2);
        goto failExit;
    }

	DWORD linkStatus = XNetGetEthernetLinkStatus();
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "XNetGetEthernetLinkStatus: %d", linkStatus);
	if (linkStatus == 0)
    {
        HalWriteSMCLEDStates(SMC_LED_STATES_GREEN_STATE0 | SMC_LED_STATES_GREEN_STATE2);
        goto failExit;
    }
    else if ((linkStatus & XNET_ETHERNET_LINK_10MBPS) == XNET_ETHERNET_LINK_10MBPS) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Slow ethernet link detected.");

	XNADDR localAddr;
	DWORD status = XNetGetTitleXnAddr(&localAddr);
	while (status == XNET_GET_XNADDR_PENDING)
	{
		Sleep(100);
		status = XNetGetTitleXnAddr(&localAddr);
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "XNetGetTitleXnAddr: %d", status);
	if ((status & XNET_GET_XNADDR_NONE) == XNET_GET_XNADDR_NONE || (status & XNET_GET_XNADDR_TROUBLESHOOT) == XNET_GET_XNADDR_TROUBLESHOOT)
    {
        HalWriteSMCLEDStates(SMC_LED_STATES_GREEN_STATE0 | SMC_LED_STATES_GREEN_STATE2);
        goto failExit;
    }
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "IP Address: %d.%d.%d.%d", localAddr.ina.S_un.S_un_b.s_b1, localAddr.ina.S_un.S_un_b.s_b2, localAddr.ina.S_un.S_un_b.s_b3, localAddr.ina.S_un.S_un_b.s_b4);

	//Get devices at the start to set everything up for potential cases where the GetDevices command is not sent first by clients.
	XDON_COMMAND_GET_DEVICES_RESPONSE tmp;
	GetDevices(&tmp);

	ServerMain();
    
    //ServerMain loops forever, so if we get here, it has failed and we cannot continue.
    HalWriteSMCLEDStates(SMC_LED_STATES_RED_STATE0 | SMC_LED_STATES_RED_STATE2);
    failExit:
    Sleep(3000);
    //The Original Xbox doesn't like returns from main, so get out this way.
    #ifdef _SKELETONKEY
    HalReturnToFirmware(HalRebootRoutine);
    #else
    LD_LAUNCH_DASHBOARD ld;
    MemZero(&ld, sizeof(ld));
    ld.dwReason = XLD_LAUNCH_DASHBOARD_MAIN_MENU;
    XLaunchNewImage(NULL, (PLAUNCH_DATA)&ld);
    #endif
}