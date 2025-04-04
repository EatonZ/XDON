// Copyright © Eaton Works 2025. All rights reserved.
// License: https://github.com/EatonZ/XDON/blob/main/LICENSE

#include "stdafx.h"
#include "XDON.h"

#pragma region

const PCHAR strVertexShaderProgram = 
" float4x4 matWVP;														"
"																		"  
" struct VS_IN															"  
" {																		" 
"     float4 position : POSITION;										" 
"     float2 texcoord : TEXCOORD0;										"                
" };																	" 
"																		" 
" struct VS_OUT															" 
" {																		" 
"     float4 position : POSITION;										" 
"     float2 texcoord : TEXCOORD1;										"    
" };																	"  
"																		"  
" VS_OUT main(VS_IN input)									     		"  
" {																		"  
"     VS_OUT output;													"  
"     output.position = mul(input.position, matWVP);					" 
"     output.texcoord = input.texcoord;									"
"     return output;													"
" }																		";

const PCHAR strPixelShaderProgram = 
" Texture2D textureDiffuse;												"
" SamplerState samplerDiffuse											"
" {																		"
"	   Texture = <texture>;												"
"      MagFilter = Linear;												"
"      MinFilter = Anisotropic;											"
"      MipFilter = Linear;												"
"	   MaxAnisotropy = 16;												"
" };																	"
"																		"
" struct PS_IN															"
" {																		"
"     float4 position : POSITION;										" 
"     float2 texcoord : TEXCOORD1;										"                  
" };																	"
"																		"  
" float4 main(PS_IN input) : COLOR									    "  
" {																		"  
"     return textureDiffuse.Sample(samplerDiffuse, input.texcoord);		"
" }																		"; 

D3DDevice* pd3dDevice;
D3DVertexBuffer* pVB; 
D3DVertexDeclaration* pVertexDecl; 
D3DVertexShader* pVertexShader;
D3DPixelShader* pPixelShader; 
D3DTexture* pTexture;
HANDLE liveNotifications = NULL;
//Order must match DEVICE_INDEX_INTERNAL.
DEVICE_INFO devices[] =
{
	{ "\\Device\\Harddisk0\\partition0", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\Mu0System", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\Mu0", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\Mu1System", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\Mu1", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\BuiltInMuMmc\\ReservationPartition", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\BuiltInMuMmc\\Storage", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\BuiltInMuUsb\\ReservationPartition", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\BuiltInMuUsb\\Storage", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\BuiltInMuSfcSystem", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\BuiltInMuSfc", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\Mass0", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\Mass1", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\Mass2", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\Flash", INVALID_HANDLE_VALUE, NULL, {} },
	{ "\\Device\\CdRom0", INVALID_HANDLE_VALUE, NULL, {} }
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
DWORD nextHardwareThread = 0;
#ifdef _DEBUG
BYTE verbositySetting = PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS | PRINT_VERBOSITY_FLAG_REQUESTS;
#else
BYTE verbositySetting = PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS;
#endif
pfnXamGetConsoleFriendlyName XamGetConsoleFriendlyName;
//A reboot is mandatory if any write operations take place on any device. While it's not impossible to add exceptions, it would be a lot of work (see XContent::DeviceFormatAndMount).
BOOLEAN writeTookPlace = FALSE;

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

DWORD GetNextHardwareThread()
{
	if (nextHardwareThread == 6) nextHardwareThread = 0;
	return nextHardwareThread++;
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
	if (((SIZE_T)Dest & 127) == 0 && (Count & 127) == 0) XMemSet128(Dest, c, Count);
	else XMemSet(Dest, c, Count);
}

VOID MemZero(PVOID Dest, SIZE_T Count)
{
	MemSet(Dest, 0, Count);
}

VOID MemCpy(PVOID Dest, const PVOID Src, SIZE_T Count)
{
	if (((SIZE_T)Dest & 127) == 0 && ((SIZE_T)Src & 15) == 0 && (Count & 127) == 0) XMemCpy128(Dest, Src, Count);
	else XMemCpy(Dest, Src, Count);
}

BOOLEAN MemIsEmpty(const PVOID Memory, SIZE_T Size)
{
	PBYTE ptr = (PBYTE)Memory;
	XMVECTOR zero = XMVectorZero();
	while (Size > 0)
	{
		if (((SIZE_T)ptr & 15) == 0 && Size >= 16)
		{
			if (!XMVector4EqualInt(XMLoadInt4A((PUINT)ptr), zero)) return FALSE;
			ptr += 16;
			Size -= 16;
		}
		else
		{
			while (Size > 0)
			{
				if (*ptr != 0) return FALSE;
				ptr++;
				Size--;
				//If Memory is unaligned to start, this helps get back on the fast track.
				if (((SIZE_T)ptr & 15) == 0) break;
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
		//MassGetDriveGeometry has a quirk where it will add a 1 to the sector mask value even if it's 0.
		if (Geometry->BytesPerSector == 1) Geometry->BytesPerSector = 0;
		return FALSE;
	}
	else return TRUE;
}

BOOLEAN GetFlashGeometry(HANDLE Handle, PFLASH_GEOMETRY Geometry)
{
	IO_STATUS_BLOCK iosb;
	NTSTATUS status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &iosb, IOCTL_FLASH_GET_DRIVE_GEOMETRY, NULL, 0, Geometry, sizeof(FLASH_GEOMETRY));
	if (!NT_SUCCESS(status))
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "GetFlashGeometry failed: 0x%08X", status);
		return FALSE;
	}
	else return TRUE;
}

DEVICE_INDEX_INTERNAL ExternalDeviceIndexToInternal(DEVICE_INDEX_EXTERNAL ExternalDeviceIndex)
{
	switch (ExternalDeviceIndex)
	{
		case DEVICE_INDEX_EXTERNAL_HARDDISK0: return DEVICE_INDEX_INTERNAL_HARDDISK0_PARTITION0;
		case DEVICE_INDEX_EXTERNAL_MU0: return DEVICE_INDEX_INTERNAL_MU0SYSTEM;
		case DEVICE_INDEX_EXTERNAL_MU1: return DEVICE_INDEX_INTERNAL_MU1SYSTEM;
		case DEVICE_INDEX_EXTERNAL_BUILTINMUMMC: return DEVICE_INDEX_INTERNAL_BUILTINMUMMC_RESERVATIONPARTITION;
		case DEVICE_INDEX_EXTERNAL_BUILTINMUUSB: return DEVICE_INDEX_INTERNAL_BUILTINMUUSB_RESERVATIONPARTITION;
		case DEVICE_INDEX_EXTERNAL_BUILTINMUSFC: return DEVICE_INDEX_INTERNAL_BUILTINMUSFC;
	    case DEVICE_INDEX_EXTERNAL_MASS0: return DEVICE_INDEX_INTERNAL_MASS0;
	    case DEVICE_INDEX_EXTERNAL_MASS1: return DEVICE_INDEX_INTERNAL_MASS1;
	    case DEVICE_INDEX_EXTERNAL_MASS2: return DEVICE_INDEX_INTERNAL_MASS2;
	    case DEVICE_INDEX_EXTERNAL_FLASH: return DEVICE_INDEX_INTERNAL_FLASH;
	    case DEVICE_INDEX_EXTERNAL_CDROM0: return DEVICE_INDEX_INTERNAL_CDROM0;
	}
	return DEVICE_INDEX_INTERNAL_MAX;
}

NTSTATUS ReadWriteDisk(DEVICE_INDEX_EXTERNAL ExternalDeviceIndex, LONGLONG Offset, ULONG Length, PVOID Buffer, BOOLEAN Write)
{
	//No need to validate the handle here; NtRead/WriteFile will return STATUS_INVALID_HANDLE (0xC0000008) if the client has specified a device index that is not connected.

	DEVICE_INDEX_INTERNAL internalDeviceIndex = ExternalDeviceIndexToInternal(ExternalDeviceIndex);
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER offset;
	offset.QuadPart = Offset;
	NTSTATUS status = STATUS_SUCCESS;
    
    DWORD waitResult = WaitForSingleObject(devices[internalDeviceIndex].Mutex, INFINITE);
    assert(waitResult == WAIT_OBJECT_0);
    if (waitResult != WAIT_OBJECT_0) return devices[internalDeviceIndex].Mutex == NULL ? STATUS_INVALID_HANDLE : STATUS_UNSUCCESSFUL;

	if (ExternalDeviceIndex == DEVICE_INDEX_EXTERNAL_HARDDISK0 ||
		ExternalDeviceIndex == DEVICE_INDEX_EXTERNAL_MASS0 ||
		ExternalDeviceIndex == DEVICE_INDEX_EXTERNAL_MASS1 ||
		ExternalDeviceIndex == DEVICE_INDEX_EXTERNAL_MASS2 ||
		ExternalDeviceIndex == DEVICE_INDEX_EXTERNAL_FLASH ||
		ExternalDeviceIndex == DEVICE_INDEX_EXTERNAL_CDROM0)
	{
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
	}
	else
	{
		//Memory units

		PPARTITION_INFORMATION p1 = &devices[internalDeviceIndex].PartitionInfo;
		PPARTITION_INFORMATION p2 = &devices[internalDeviceIndex + 1].PartitionInfo;
		HANDLE h1 = devices[internalDeviceIndex].Handle;
		HANDLE h2 = devices[internalDeviceIndex + 1].Handle;
		if (p1->PartitionLength.QuadPart == 0 || p2->PartitionLength.QuadPart == 0)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReadDisk failed because partition info is missing when it's required.");
			//If the client specified a device index that is not connected, return STATUS_INVALID_HANDLE. Else, return STATUS_INFO_LENGTH_MISMATCH which means IOCTL_DISK_GET_PARTITION_INFO failed earlier.
			status = h1 == INVALID_HANDLE_VALUE || h2 == INVALID_HANDLE_VALUE ? STATUS_INVALID_HANDLE : STATUS_INFO_LENGTH_MISMATCH;
            goto exit;
		}

		if (offset.QuadPart >= p1->PartitionLength.QuadPart)
		{
			//The IO is all in the second partition.
			offset.QuadPart -= p1->PartitionLength.QuadPart;
			if (Write)
			{
				status = NtWriteFile(h2, NULL, NULL, NULL, &iosb, Buffer, Length, &offset);
				if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "WriteDisk (Offset=0x%016I64X, Length=0x%08X) failed: 0x%08X", Offset, Length, status);
			}
			else
			{
				status = NtReadFile(h2, NULL, NULL, NULL, &iosb, Buffer, Length, &offset);
				if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReadDisk (Offset=0x%016I64X, Length=0x%08X) failed: 0x%08X", Offset, Length, status);
			}
			goto exit;
		}
		else
		{
			//Some (or all) of the IO is in the first partition.
			//The prints using the original Offset and Length are intentional.
			BOOLEAN secondIOInP2 = FALSE;
			ULONG p1IOLength = 0;
			if (offset.QuadPart + Length > p1->PartitionLength.QuadPart)
			{
				//Partial IO in the first partition.
				p1IOLength = (ULONG)(p1->PartitionLength.QuadPart - offset.QuadPart);
				if (Write)
				{
					status = NtWriteFile(h1, NULL, NULL, NULL, &iosb, Buffer, p1IOLength, &offset);
					if (!NT_SUCCESS(status))
					{
						Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "WriteDisk (Offset=0x%016I64X, Length=0x%08X) failed: 0x%08X", Offset, Length, status);
						goto exit;
					}
				}
				else
				{
					status = NtReadFile(h1, NULL, NULL, NULL, &iosb, Buffer, p1IOLength, &offset);
					if (!NT_SUCCESS(status))
					{
						Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReadDisk (Offset=0x%016I64X, Length=0x%08X) failed: 0x%08X", Offset, Length, status);
						goto exit;
					}
				}
				secondIOInP2 = TRUE;
			}
			
			if (secondIOInP2) offset.QuadPart = 0;
			if (Write)
			{
				status = NtWriteFile(secondIOInP2 ? h2 : h1, NULL, NULL, NULL, &iosb, (PBYTE)Buffer + (secondIOInP2 ? p1IOLength : 0), secondIOInP2 ? Length - p1IOLength : Length, &offset);
				if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "WriteDisk (Offset=0x%016I64X, Length=0x%08X) failed: 0x%08X", Offset, Length, status);
			}
			else
			{
				status = NtReadFile(secondIOInP2 ? h2 : h1, NULL, NULL, NULL, &iosb, (PBYTE)Buffer + (secondIOInP2 ? p1IOLength : 0), secondIOInP2 ? Length - p1IOLength : Length, &offset);
				if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReadDisk (Offset=0x%016I64X, Length=0x%08X) failed: 0x%08X", Offset, Length, status);
			}
		}
	}
    
    exit:
    BOOL result = ReleaseMutex(devices[internalDeviceIndex].Mutex);
    assert(result);
    return status;
}

VOID CheckMU(PHANDLE Handle1, PHANDLE Handle2, PBOOLEAN Availability, PDISK_GEOMETRY Geometry)
{
	*Availability = FALSE;

	if (*Handle1 == INVALID_HANDLE_VALUE)
	{
		if (*Handle2 != INVALID_HANDLE_VALUE)
		{
			NtClose(*Handle2);
			*Handle2 = INVALID_HANDLE_VALUE;
		}
		return;
	}
	if (*Handle2 == INVALID_HANDLE_VALUE)
	{
		if (*Handle1 != INVALID_HANDLE_VALUE)
		{
			NtClose(*Handle1);
			*Handle1 = INVALID_HANDLE_VALUE;
		}
		return;
	}

	//Getting disk geometry is enough to determine whether a valid device is connected.
	*Availability = GetDiskGeometry(*Handle1, Geometry, FALSE) && GetDiskGeometry(*Handle2, Geometry, FALSE);
	if (!*Availability)
	{
		NtClose(*Handle1);
		*Handle1 = INVALID_HANDLE_VALUE;
		NtClose(*Handle2);
		*Handle2 = INVALID_HANDLE_VALUE;
	}
}

//Recreation of the kernel function since it's not exported.
NTSTATUS MassReadStringDescriptor(HANDLE Handle, USHORT Index, BYTE Value, PUSB_STRING_DESCRIPTOR DescriptorOut)
{
	USB_CONTROL_SETUP_PACKET setupPacket;
	setupPacket.bmRequestType.B = 0;
	setupPacket.bmRequestType.s.Dir = BMREQUEST_DEVICE_TO_HOST;
	setupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;
	setupPacket.wValue = _byteswap_ushort(USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(USB_STRING_DESCRIPTOR_TYPE, Value));
	setupPacket.wIndex = _byteswap_ushort(Index);
	setupPacket.wLength = _byteswap_ushort(MAXIMUM_USB_STRING_LENGTH);

	MASS_CONTROL_TRANSFER mct;
	mct.SetupPacket = &setupPacket;
	mct.TransferBuffer = DescriptorOut;
	mct.TransferLength = MAXIMUM_USB_STRING_LENGTH;
	mct.AllowShortTransfer = TRUE;

	IO_STATUS_BLOCK iosb;
	NTSTATUS status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &iosb, IOCTL_MASS_CONTROL_TRANSFER, &mct, sizeof(mct), &mct, sizeof(mct));
	if (NT_SUCCESS(status))
	{
		if (DescriptorOut->bLength > mct.TransferLengthCompleted || DescriptorOut->bLength < 2 || (DescriptorOut->bLength % 2) != 0 || DescriptorOut->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE) status = STATUS_IO_DEVICE_ERROR;
	}
	return status;
}

VOID GetUSBStringDescriptors(HANDLE Handle, PUSB_STRING_DESCRIPTOR_MAX ManufacturerDescriptorOut, PUSB_STRING_DESCRIPTOR_MAX ProductDescriptorOut, PUSB_STRING_DESCRIPTOR_MAX SerialNumberDescriptorOut)
{
	USB_CONTROL_SETUP_PACKET setupPacket;
	setupPacket.bmRequestType.B = 0;
	setupPacket.bmRequestType.s.Dir = BMREQUEST_DEVICE_TO_HOST;
	setupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;
	setupPacket.wValue = _byteswap_ushort(USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(USB_DEVICE_DESCRIPTOR_TYPE, 0));
	setupPacket.wIndex = _byteswap_ushort(0);
	setupPacket.wLength = _byteswap_ushort(sizeof(USB_DEVICE_DESCRIPTOR));

	USB_DEVICE_DESCRIPTOR deviceDesc;
	MASS_CONTROL_TRANSFER mct;
	mct.SetupPacket = &setupPacket;
	mct.TransferBuffer = &deviceDesc;
	mct.TransferLength = sizeof(deviceDesc);
	mct.AllowShortTransfer = FALSE;

	IO_STATUS_BLOCK iosb;
	NTSTATUS status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &iosb, IOCTL_MASS_CONTROL_TRANSFER, &mct, sizeof(mct), &mct, sizeof(mct));
	if (NT_SUCCESS(status))
	{
		//By default, US English is used, but if the USB descriptor indicates a different primary language, use that.
		USHORT langID = 0x409;
		USB_STRING_DESCRIPTOR0_MAX desc0;
		if (NT_SUCCESS(MassReadStringDescriptor(Handle, 0, 0, (PUSB_STRING_DESCRIPTOR)&desc0)) && desc0.bLength >= 4) langID = _byteswap_ushort(desc0.wLANGID[0]);

		if (deviceDesc.iManufacturer != 0)
		{
			status = MassReadStringDescriptor(Handle, langID, deviceDesc.iManufacturer, (PUSB_STRING_DESCRIPTOR)ManufacturerDescriptorOut);
			if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to read manufacturer string: 0x%08X", status);
		}
		if (deviceDesc.iProduct != 0)
		{
			status = MassReadStringDescriptor(Handle, langID, deviceDesc.iProduct, (PUSB_STRING_DESCRIPTOR)ProductDescriptorOut);
			if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to read product string: 0x%08X", status);
		}
		if (deviceDesc.iSerialNumber != 0)
		{
			status = MassReadStringDescriptor(Handle, langID, deviceDesc.iSerialNumber, (PUSB_STRING_DESCRIPTOR)SerialNumberDescriptorOut);
			if (!NT_SUCCESS(status)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to read serial number string: 0x%08X", status);
		}
	}
	else Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to read device descriptor: 0x%08X", status);
}

VOID Identify(PXDON_COMMAND_IDENTIFY_RESPONSE Response)
{
	MemZero(Response, sizeof(XDON_COMMAND_IDENTIFY_RESPONSE));

    Response->XDONVersion = XDON_VERSION;
	MemCpy(&Response->XboxVersion, XboxKrnlVersion, sizeof(XBOX_KRNL_VERSION));
	MemCpy(&Response->XboxHardwareInfo, XboxHardwareInfo, sizeof(XBOX_HARDWARE_INFO));

	if (XamGetConsoleFriendlyName == NULL) wcsncpy(Response->XboxName, L"Xbox 360", sizeof(Response->XboxName));
	else XamGetConsoleFriendlyName(Response->XboxName);

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

		MemZero(&devices[i].PartitionInfo, sizeof(devices[i].PartitionInfo));
		if (devices[i].Handle != INVALID_HANDLE_VALUE)
		{
			IO_STATUS_BLOCK iosb;
			NTSTATUS status = NtDeviceIoControlFile(devices[i].Handle, NULL, NULL, NULL, &iosb, IOCTL_DISK_GET_PARTITION_INFO, NULL, 0, &devices[i].PartitionInfo, sizeof(devices[i].PartitionInfo));
			//Fail will be checked in ReadWriteDisk.
		}
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

	//The memory units have 2 device paths each. In order to get the full picture of the device, we need both handles.

	BOOLEAN availability;
	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU0SYSTEM].Handle, &devices[DEVICE_INDEX_INTERNAL_MU0].Handle, &availability, &Response->Mu0Geometry);
	Response->AvailableDevices.Mu0 = availability;
	if (Response->AvailableDevices.Mu0) GetUSBStringDescriptors(devices[DEVICE_INDEX_INTERNAL_MU0SYSTEM].Handle, &Response->Mu0Manufacturer, &Response->Mu0Product, &Response->Mu0SerialNumber);
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu0 available: %s", Response->AvailableDevices.Mu0 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_MU1SYSTEM].Handle, &devices[DEVICE_INDEX_INTERNAL_MU1].Handle, &availability, &Response->Mu1Geometry);
	Response->AvailableDevices.Mu1 = availability;
	if (Response->AvailableDevices.Mu1) GetUSBStringDescriptors(devices[DEVICE_INDEX_INTERNAL_MU1SYSTEM].Handle, &Response->Mu1Manufacturer, &Response->Mu1Product, &Response->Mu1SerialNumber);
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mu1 available: %s", Response->AvailableDevices.Mu1 ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_BUILTINMUMMC_RESERVATIONPARTITION].Handle, &devices[DEVICE_INDEX_INTERNAL_BUILTINMUMMC_STORAGE].Handle, &availability, &Response->BuiltInMuMmcGeometry);
	Response->AvailableDevices.BuiltInMuMmc = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "BuiltInMuMmc available: %s", Response->AvailableDevices.BuiltInMuMmc ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_BUILTINMUUSB_RESERVATIONPARTITION].Handle, &devices[DEVICE_INDEX_INTERNAL_BUILTINMUUSB_STORAGE].Handle, &availability, &Response->BuiltInMuUsbGeometry);
	Response->AvailableDevices.BuiltInMuUsb = availability;
	if (Response->AvailableDevices.BuiltInMuUsb) GetUSBStringDescriptors(devices[DEVICE_INDEX_INTERNAL_BUILTINMUUSB_RESERVATIONPARTITION].Handle, &Response->BuiltInMuUsbManufacturer, &Response->BuiltInMuUsbProduct, &Response->BuiltInMuUsbSerialNumber);
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "BuiltInMuUsb available: %s", Response->AvailableDevices.BuiltInMuUsb ? "YES" : "NO");

	CheckMU(&devices[DEVICE_INDEX_INTERNAL_BUILTINMUSFC].Handle, &devices[DEVICE_INDEX_INTERNAL_BUILTINMUSFCSYSTEM].Handle, &availability, &Response->BuiltInMuSfcGeometry);
	Response->AvailableDevices.BuiltInMuSfc = availability;
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "BuiltInMuSfc available: %s", Response->AvailableDevices.BuiltInMuSfc ? "YES" : "NO");

	handle = devices[DEVICE_INDEX_INTERNAL_MASS0].Handle;
	if (handle != INVALID_HANDLE_VALUE)
	{
		Response->AvailableDevices.Mass0 = GetDiskGeometry(handle, &Response->Mass0Geometry, FALSE);
		if (!Response->AvailableDevices.Mass0)
		{
			NtClose(handle);
			devices[DEVICE_INDEX_INTERNAL_MASS0].Handle = INVALID_HANDLE_VALUE;
		}
		else GetUSBStringDescriptors(handle, &Response->Mass0Manufacturer, &Response->Mass0Product, &Response->Mass0SerialNumber);
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mass0 available: %s", Response->AvailableDevices.Mass0 ? "YES" : "NO");

	handle = devices[DEVICE_INDEX_INTERNAL_MASS1].Handle;
	if (handle != INVALID_HANDLE_VALUE)
	{
		Response->AvailableDevices.Mass1 = GetDiskGeometry(handle, &Response->Mass1Geometry, FALSE);
		if (!Response->AvailableDevices.Mass1)
		{
			NtClose(handle);
			devices[DEVICE_INDEX_INTERNAL_MASS1].Handle = INVALID_HANDLE_VALUE;
		}
		else GetUSBStringDescriptors(handle, &Response->Mass1Manufacturer, &Response->Mass1Product, &Response->Mass1SerialNumber);
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mass1 available: %s", Response->AvailableDevices.Mass1 ? "YES" : "NO");

	handle = devices[DEVICE_INDEX_INTERNAL_MASS2].Handle;
	if (handle != INVALID_HANDLE_VALUE)
	{
		Response->AvailableDevices.Mass2 = GetDiskGeometry(handle, &Response->Mass2Geometry, FALSE);
		if (!Response->AvailableDevices.Mass2)
		{
			NtClose(handle);
			devices[DEVICE_INDEX_INTERNAL_MASS2].Handle = INVALID_HANDLE_VALUE;
		}
		else GetUSBStringDescriptors(handle, &Response->Mass2Manufacturer, &Response->Mass2Product, &Response->Mass2SerialNumber);
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Mass2 available: %s", Response->AvailableDevices.Mass2 ? "YES" : "NO");

	handle = devices[DEVICE_INDEX_INTERNAL_FLASH].Handle;
	if (handle != INVALID_HANDLE_VALUE)
	{
		Response->AvailableDevices.Flash = GetFlashGeometry(handle, &Response->FlashGeometry);
		if (!Response->AvailableDevices.Flash)
		{
			NtClose(handle);
			devices[DEVICE_INDEX_INTERNAL_FLASH].Handle = INVALID_HANDLE_VALUE;
		}
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Flash available: %s", Response->AvailableDevices.Flash ? "YES" : "NO");

	handle = devices[DEVICE_INDEX_INTERNAL_CDROM0].Handle;
	if (handle != INVALID_HANDLE_VALUE)
	{
		Response->AvailableDevices.CdRom0 = GetDiskGeometry(handle, &Response->CdRom0Geometry, TRUE);
		if (Response->AvailableDevices.CdRom0)
		{
			SCSI_PASS_THROUGH_DIRECT scsiPT;
			MemZero(&scsiPT, sizeof(scsiPT));
			scsiPT.Length = sizeof(scsiPT);
			scsiPT.DataIn = SCSI_IOCTL_DATA_IN;
			scsiPT.DataTransferLength = sizeof(Response->CdRom0Info);
			scsiPT.DataBuffer = &Response->CdRom0Info;
			PCDB6INQUIRY cdb = (PCDB6INQUIRY)&scsiPT.Cdb;
			cdb->OperationCode = SCSIOP_INQUIRY;
			cdb->AllocationLength = sizeof(Response->CdRom0Info);
			cdb->Control = 0xC0; //Vendor Specific
			
			IO_STATUS_BLOCK iosb;
			NTSTATUS status = NtDeviceIoControlFile(handle, NULL, NULL, NULL, &iosb, IOCTL_SCSI_PASS_THROUGH_DIRECT, &scsiPT, sizeof(scsiPT), NULL, 0);
			if (NT_SUCCESS(status)) Response->CdRom0InfoAvailable = TRUE;
			else Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Failed to get CdRom0 info: 0x%08X", status);
		}
		else
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

#pragma endregion Utilities

#pragma region

//The mere existence of these DirectX functions causes a "The game couldn't start" error on retail when using the debug DirectX libraries. We can't reference the production DirectX libraries in debug mode either, so no display when debugging on retail!
#ifndef _NO_DX
VOID InitDisplay()
{
	//Big thanks to EqUiNoX for helping with this display code.

	PVOID img;
	ULONG imgSize;
    if (!XGetModuleSection(GetModuleHandle(NULL), "BKND.png", &img, &imgSize))
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "InitDisplay: No BKND.png section. Did you rename it?");
		return;
	}

	XVIDEO_MODE vm;
	XGetVideoMode(&vm);
	D3DPRESENT_PARAMETERS d3dpp;
	MemZero(&d3dpp, sizeof(d3dpp));
	d3dpp.BackBufferWidth = vm.dwDisplayWidth;
	d3dpp.BackBufferHeight = vm.dwDisplayHeight;
	d3dpp.BackBufferFormat = D3DFMT_LIN_X8R8G8B8;
	d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_IMMEDIATE;

	HRESULT hr = Direct3DCreate9(D3D_SDK_VERSION)->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, 0, &d3dpp, &pd3dDevice);
	assert(!FAILED(hr));

	hr = D3DXCreateTextureFromFileInMemory(pd3dDevice, img, imgSize, &pTexture);
    assert(!FAILED(hr));

    ID3DXBuffer* pVertexShaderCode;
    ID3DXBuffer* pVertexErrorMsg;
    hr = D3DXCompileShader(strVertexShaderProgram, (UINT)strlen(strVertexShaderProgram), NULL, NULL, "main", "vs_2_0", 0, &pVertexShaderCode, &pVertexErrorMsg, NULL);
    assert(!FAILED(hr));
    hr = pd3dDevice->CreateVertexShader((PDWORD)pVertexShaderCode->GetBufferPointer(), &pVertexShader);
	assert(!FAILED(hr));

    ID3DXBuffer* pPixelShaderCode;
    ID3DXBuffer* pPixelErrorMsg;
    hr = D3DXCompileShader(strPixelShaderProgram, (UINT)strlen(strPixelShaderProgram), NULL, NULL, "main", "ps_2_0", 0, &pPixelShaderCode, &pPixelErrorMsg, NULL);
    assert(!FAILED(hr));
    hr = pd3dDevice->CreatePixelShader((PDWORD)pPixelShaderCode->GetBufferPointer(), &pPixelShader);
	assert(!FAILED(hr));
    
    D3DVERTEXELEMENT9 VertexElements[] =
    {
        { 0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    hr = pd3dDevice->CreateVertexDeclaration(VertexElements, &pVertexDecl);
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
    hr = pd3dDevice->CreateVertexBuffer(sizeof(vertices), 0, 0, 0, &pVB, NULL);
	assert(!FAILED(hr));

    PVERTEX pVertices;
    hr = pVB->Lock(0, 0, (PVOID*)&pVertices, 0);
	assert(!FAILED(hr));
	//Do not use MemCpy here (Datatype misalignment).
    memcpy(pVertices, vertices, sizeof(vertices));

    hr = pVB->Unlock();
	assert(!FAILED(hr));
}
#endif

VOID UILoop()
{
	#ifndef _NO_DX
	InitDisplay();
	#endif

	while (TRUE)
	{
		XINPUT_STATE state;
		DWORD rebootTime = 0;
		for (BYTE i = 0; i < XUSER_MAX_COUNT; i++)
		{
			MemZero(&state, sizeof(state));
			if (XInputGetState(i, &state) == ERROR_SUCCESS)
			{
				if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_B) == XINPUT_GAMEPAD_B)
				{
                    if (writeTookPlace)
                    {
                        XNotifyQueueUI(XNOTIFYUI_TYPE_CONSOLEMESSAGE, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"Cold rebooting since a disk write happened.", 0);
						Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Cold rebooting since a disk write happened.");
						//When we are rendering, the DX loop needs to continue for the XNotify to show.
						#ifdef _NO_DX
						Sleep(7000);
						HalReturnToFirmware(HalRebootQuiesceRoutine);
						return;
						#else
						rebootTime = GetTickCount() + 7000;
						#endif
                    }
                    else
                    {
                        Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Rebooting to default app.");
						XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
						return;
                    }
				}
			}
		}

		if (liveNotifications != NULL && liveNotifications != INVALID_HANDLE_VALUE)
		{
			DWORD notificationID;
			ULONG_PTR param;
			if (XNotifyGetNext(liveNotifications, XN_LIVE_LINK_STATE_CHANGED, &notificationID, &param))
			{
				XNotifyQueueUI((BOOL)param ? XNOTIFYUI_TYPE_PREFERRED_REVIEW : XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, (BOOL)param ? L"Reconnected" : L"Disconnected", 0);
				Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "%s detected.", (BOOL)param ? "Reconnect" : "Disconnect");
			}
		}

		#ifndef _NO_DX
		dxLoop:
		if (rebootTime > 0 && rebootTime < GetTickCount())
		{
			HalReturnToFirmware(HalRebootQuiesceRoutine);
			return;
		}
		HRESULT hr = pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
		assert(!FAILED(hr));
		hr = pd3dDevice->SetVertexDeclaration(pVertexDecl);
		assert(!FAILED(hr));
		hr = pd3dDevice->SetStreamSource(0, pVB, 0, sizeof(VERTEX));
		assert(!FAILED(hr));
		hr = pd3dDevice->SetVertexShader(pVertexShader);
		assert(!FAILED(hr));
		hr = pd3dDevice->SetPixelShader(pPixelShader);   
		assert(!FAILED(hr));
		XMMATRIX matWVP = XMMatrixIdentity();
		hr = pd3dDevice->SetVertexShaderConstantF(0, (PFLOAT)&matWVP, 4);
		assert(!FAILED(hr));
		hr = pd3dDevice->SetTexture(0, pTexture);
		assert(!FAILED(hr));
		hr = pd3dDevice->SetSamplerFilterStates(0, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, 1);
		assert(!FAILED(hr));
		hr = pd3dDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
		assert(!FAILED(hr));
		hr = pd3dDevice->Present(NULL, NULL, NULL, NULL);
		assert(!FAILED(hr));
		if (rebootTime > 0) goto dxLoop;
		#endif
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
	xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY | XNET_STARTUP_DISABLE_PEER_ENCRYPTION;
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
	frame.Identifier = _byteswap_uint64(RESPONSE_FRAME_IDENTIFIER);
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

				frame.StatusCode = ReadWriteDisk(request->DeviceIndex, request->Offset, request->Length, request->CompressResponseData ? ThreadParam->Memories.ScratchMemory : response->Data, FALSE);

				INT responseDataLength = 0;
				if (NT_SUCCESS(frame.StatusCode))
				{
					response->DataLength = MemIsEmpty(request->CompressResponseData ? ThreadParam->Memories.ScratchMemory : response->Data, request->Length) ? 0 : request->Length;
					response->UncompressedDataLength = response->DataLength;
					if (response->DataLength == 0) Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): MemIsEmpty=TRUE", ThreadParam->ClientSocket);
					else if (request->CompressResponseData)
					{
						DWORD tc = GetTickCount();
						response->DataLength = LZ4_compress_default((const PCHAR)ThreadParam->Memories.ScratchMemory, (PCHAR)response->Data, request->Length, MAX_COMPRESSION_LENGTH);
						Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Compressed %d bytes to %d bytes (ratio: %d) in %dms.", ThreadParam->ClientSocket, request->Length, response->DataLength, request->Length / response->DataLength, GetTickCount() - tc);
						if (response->DataLength > request->Length) Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Compression overage detected.", ThreadParam->ClientSocket);
					}
					responseDataLength = sizeof(XDON_COMMAND_READ_RESPONSE) + response->DataLength;
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
					DWORD tc = GetTickCount();
					INT decompressedDataLength = LZ4_decompress_safe((const PCHAR)request->Data, (PCHAR)ThreadParam->Memories.ScratchMemory, request->DataLength, SCRATCH_MEMORY_SIZE);
					if (decompressedDataLength > 0)
					{
						Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Decompressed %d bytes to %d bytes (ratio: %d) in %dms.", ThreadParam->ClientSocket, request->DataLength, decompressedDataLength, decompressedDataLength / request->DataLength, GetTickCount() - tc);
						if (request->DataLength > (UINT)decompressedDataLength) Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Compression overage detected.", ThreadParam->ClientSocket);
						frame.StatusCode = ReadWriteDisk(request->DeviceIndex, request->Offset, decompressedDataLength, ThreadParam->Memories.ScratchMemory, TRUE);
					}
					else
					{
						Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Decompression failed.", ThreadParam->ClientSocket);
						frame.StatusCode = STATUS_UNSUCCESSFUL;
					}
				}
				else frame.StatusCode = ReadWriteDisk(request->DeviceIndex, request->Offset, request->DataLength, request->Data, TRUE);

				writeTookPlace = NT_SUCCESS(frame.StatusCode);

				if (!SockSend(ThreadParam->ClientSocket, &frame, sizeof(frame), NULL, 0, To, ToLen)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "FulfillRequest (%X): Failed to send response frame: %d", ThreadParam->ClientSocket, WSAGetLastError());

				break;
			}
		case XDON_COMMAND_WRITE_SAME:
			{
				PXDON_COMMAND_WRITE_SAME_REQUEST request = (PXDON_COMMAND_WRITE_SAME_REQUEST)ThreadParam->Memories.RequestMemory;
				Print(PRINT_VERBOSITY_FLAG_REQUESTS, "FulfillRequest (%X): Fulfilling XDON_COMMAND_WRITE_SAME (DeviceIndex=%d, Offset=0x%016I64X, Length=0x%08X, Value=0x%02X).", ThreadParam->ClientSocket, request->DeviceIndex, request->Offset, request->Length, request->Value);
				
				MemSet(ThreadParam->Memories.ScratchMemory, request->Value, request->Length);
				frame.StatusCode = ReadWriteDisk(request->DeviceIndex, request->Offset, request->Length, ThreadParam->Memories.ScratchMemory, TRUE);

				writeTookPlace = NT_SUCCESS(frame.StatusCode);

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
				HalReturnToFirmware(request->Routine);

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
		if (_byteswap_uint64(frame.Identifier) != REQUEST_FRAME_IDENTIFIER)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Non-XDON data received; ignoring.", ThreadParam->ClientSocket);
			return STATUS_NOT_SUPPORTED;
		}
		if (frame.Version != PROTOCOL_VERSION)
		{
			Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ReceiveAndValidateRequest (%X): Protocol version mismatch (received=%d, supported=%d).", ThreadParam->ClientSocket, frame.Version, PROTOCOL_VERSION);
			return STATUS_NOT_SUPPORTED;
		}
		else *CommandIDOut = frame.CommandID;
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
                frame.Identifier = _byteswap_uint64(RESPONSE_FRAME_IDENTIFIER);
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

	if (!SockSetOptions(serverSocket, TRUE, TRUE, FALSE, -1)) return;

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

	if (!SockSetOptions(idServerSocket, TRUE, TRUE, TRUE, -1)) return;

	if (bind(idServerSocket, (const sockaddr*)&serverEndpoint, sizeof(serverEndpoint)) == SOCKET_ERROR) 
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: bind failed: %d", WSAGetLastError());
		return;
	}

	HANDLE uiLoopThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)UILoop, NULL, CREATE_SUSPENDED, NULL);
	if (uiLoopThread == NULL)
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain: CreateThread (UILoop) failed: %d", GetLastError());
		return;
	}
	XSetThreadProcessor(uiLoopThread, GetNextHardwareThread());
	ResumeThread(uiLoopThread);
	CloseHandle(uiLoopThread);
    
	if (GetModuleHandle("xbdm.xex") != NULL)
    {
        Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "WARNING: XBDM detected. If you're seeing this, please avoid using Xbox 360 Neighborhood while XDON is running.");
        XNotifyQueueUI(XNOTIFYUI_TYPE_CONSOLEMESSAGE, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"Avoid using Xbox 360 Neighborhood while XDON is running.", 0);
    }

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
                        HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TCPSocketThread, socketThreadParam, CREATE_SUSPENDED, NULL);
                        if (thread == NULL)
                        {
                            Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (TCP): CreateThread (SocketThread) failed: %d; starting over.", GetLastError());
                            SockDestroy(clientSocket);
                        }
                        else
                        {
							Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "ServerMain (TCP): Thread started to handle socket %X; starting over.", clientSocket);
							XSetThreadProcessor(thread, GetNextHardwareThread());
						    //Priority needs to be bumped up or else performance will be really bad!
                            SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
							ResumeThread(thread);
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
				frame.Identifier = _byteswap_uint64(RESPONSE_FRAME_IDENTIFIER);
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

	//Prevent any actions via the Xbox Guide. This prevents signin, profile creation, etc. All for safety.
	//This is better than setting XEX_PRIVILEGE_RESTRICT_HUD_FEATURES because that has a message about original Xbox games and it lets you go home.
	XamSetDashContext(XDASHCONTEXT_TROUBLESHOOTER);

	//Sign out all users to flush the profiles.
	XUID users[XUSER_MAX_COUNT];
	for (BYTE i = 0; i < XUSER_MAX_COUNT; i++)
	{
		XUSER_SIGNIN_INFO signinInfo;
		if (XUserGetSigninInfo(i, XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY, &signinInfo) == ERROR_SUCCESS && signinInfo.UserSigninState != eXUserSigninState_NotSignedIn) users[i] = signinInfo.xuid;
		else users[i] = INVALID_XUID;
	}
	if (!MemIsEmpty(&users, sizeof(users)))
	{
		Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "Profiles are being signed out for safety.");
		HRESULT hr = XamUserLogon(users, XAMUSERLOGON_REMOVEUSERS, NULL);
		if (FAILED(hr)) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "XamUserLogon failed: 0x%08X", hr);
	}

	//Disable the Auto-Off feature to avoid unintended shutdowns. It'll get set back to TRUE on exit (XAM does it - we don't have to do it).
	XamEnableInactivityProcessing(XAMINACTIVITY_AUTOSHUTOFF, FALSE);

	//Resolving like this so XDON can work on pre-16197 consoles.
	XamGetConsoleFriendlyName = (pfnXamGetConsoleFriendlyName)GetProcAddress(GetModuleHandle("xam.xex"), (LPCSTR)1291);

	if (!InitNetwork()) goto failExit;

	DWORD linkStatus = XNetGetEthernetLinkStatus();
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "XNetGetEthernetLinkStatus: %d", linkStatus);
	if (linkStatus == 0)
	{
		XNotifyQueueUI(XNOTIFYUI_TYPE_CONSOLEMESSAGE, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"Connect to a network before launching XDON.", 0);
		Sleep(7000);
		return;
	}
	else if ((linkStatus & XNET_ETHERNET_LINK_10MBPS) == XNET_ETHERNET_LINK_10MBPS) XNotifyQueueUI(XNOTIFYUI_TYPE_CONSOLEMESSAGE, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"Use a 100mbps ethernet link for the best performance.", 0);
	else if ((linkStatus & XNET_ETHERNET_LINK_WIRELESS) == XNET_ETHERNET_LINK_WIRELESS) XNotifyQueueUI(XNOTIFYUI_TYPE_CONSOLEMESSAGE, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"Use a wired ethernet link for the best performance.", 0);

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
		XNotifyQueueUI(XNOTIFYUI_TYPE_CONSOLEMESSAGE, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"There is a problem with the network connection.", 0);
		Sleep(7000);
		return;
	}
	Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "IP Address: %d.%d.%d.%d", localAddr.ina.S_un.S_un_b.s_b1, localAddr.ina.S_un.S_un_b.s_b2, localAddr.ina.S_un.S_un_b.s_b3, localAddr.ina.S_un.S_un_b.s_b4);

	liveNotifications = XNotifyCreateListener(XNOTIFY_LIVE);
	if (liveNotifications == NULL || liveNotifications == INVALID_HANDLE_VALUE) Print(PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS, "XNotifyCreateListener failed.");

	//Get devices at the start to set everything up for potential cases where the GetDevices command is not sent first by clients.
	XDON_COMMAND_GET_DEVICES_RESPONSE tmp;
	GetDevices(&tmp);

	ServerMain();

	//ServerMain loops forever, so if we get here, something essential failed and we cannot continue.
	failExit:
	XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"XDON failed to start. Use Xbox Watson to diagnose.", 0);
	Sleep(7000);
}