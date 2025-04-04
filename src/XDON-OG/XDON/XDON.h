// Copyright © Eaton Works 2025. All rights reserved.
// License: https://github.com/EatonZ/XDON/blob/main/LICENSE

#pragma once

#pragma region

//3 memory buffers get allocated at startup.
//The 0x1000 is to account for request and response structs before any variable-length data. Frames are not part of this.
#define MAX_IO_LENGTH          1048576 //When doing a single IO operation, 1 MB has proven to be the sweet spot. If you want to go higher, you can change this, but it won't be any faster.
#define MAX_COMPRESSION_LENGTH MAX_IO_LENGTH //The LZ4 code won't compile on OG right now.
#define REQUEST_MEMORY_SIZE	   MAX_COMPRESSION_LENGTH + 0x1000 //Used to read the request and any variable-length request data.
#define RESPONSE_MEMORY_SIZE   MAX_COMPRESSION_LENGTH + 0x1000 //Used to prepare responses with variable-length response data.
#define SCRATCH_MEMORY_SIZE	   MAX_IO_LENGTH //Used for anything.

#define CONSOLE_TYPE              0 //0=OG Xbox
#define PC_COMMUNICATION_PORT     1000 //Why port 1000? Look here: https://i.imgur.com/ElBjYm7.png
#define SOCKET_BUFFER_SIZE	      0x4000 //Currently the Xbox default. Not really any benefit to changing based on our testing.
#define SOCKET_TIMEOUT  	      0 //Timeout for TCP SO_SNDTIMEO and SO_RCVTIMEO.
#define REQUEST_FRAME_IDENTIFIER  0x58444F4E52455155 //XDONREQU
#define RESPONSE_FRAME_IDENTIFIER 0x58444F4E52455350 //XDONRESP
#define PROTOCOL_VERSION          1 //Increase when a breaking change is made to the protocol.
#define XDON_VERSION              1 //Keep this in sync with XBE Version.

//Must keep in sync with XDON clients.
#define XDON_COMMAND_REQUEST_FRAME_SIZE                   0xA
#define XDON_COMMAND_RESPONSE_FRAME_SIZE                  0xE
#define XDON_COMMAND_IDENTIFY_RESPONSE_SIZE               0x15
#define XDON_COMMAND_GET_DEVICES_RESPONSE_SIZE            0x50C
#define XDON_COMMAND_READ_REQUEST_SIZE                    0xE
#define XDON_COMMAND_READ_RESPONSE_SIZE                   8
#define XDON_COMMAND_WRITE_REQUEST_SIZE                   0xE
#define XDON_COMMAND_WRITE_SAME_REQUEST_SIZE              0xE
#define XDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST_SIZE      0xC
#define XDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE_SIZE     8
#define XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST_SIZE 1

#pragma endregion Defs

#pragma region

typedef enum _PRINT_VERBOSITY_FLAG {
	PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS = 1, //Print essential messages about XDON operation the user will want to see.
	PRINT_VERBOSITY_FLAG_REQUESTS = 2, //Print informational messages from FulfillRequest and ReceiveAndValidateRequest. A little noisy.
    PRINT_VERBOSITY_FLAG_SOCK_SEND_RECV = 4 //Enable printing in SockSend and SockReceive. Very noisy!
} PRINT_VERBOSITY_FLAG, *PPRINT_VERBOSITY_FLAG;

typedef enum _XDON_COMMAND {
	XDON_COMMAND_UNKNOWN = 0,
	XDON_COMMAND_IDENTIFY = 1,
	XDON_COMMAND_GET_DEVICES = 2,
	XDON_COMMAND_READ = 3,
	XDON_COMMAND_WRITE = 4,
	XDON_COMMAND_WRITE_SAME = 5,
	XDON_COMMAND_ATA_CUSTOM_COMMAND = 6,
	XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE = 7,
	XDON_COMMAND_MAX = 8
} XDON_COMMAND, *PXDON_COMMAND;

//For internal use.
typedef enum _DEVICE_INDEX_INTERNAL {
	DEVICE_INDEX_INTERNAL_HARDDISK0_PARTITION0 = 0,
	DEVICE_INDEX_INTERNAL_HARDDISK1_PARTITION0 = 1,
	DEVICE_INDEX_INTERNAL_MU0 = 2,
	DEVICE_INDEX_INTERNAL_MU1 = 3,
	DEVICE_INDEX_INTERNAL_MU2 = 4,
	DEVICE_INDEX_INTERNAL_MU3 = 5,
	DEVICE_INDEX_INTERNAL_MU4 = 6,
	DEVICE_INDEX_INTERNAL_MU5 = 7,
	DEVICE_INDEX_INTERNAL_MU6 = 8,
	DEVICE_INDEX_INTERNAL_MU7 = 9,
	DEVICE_INDEX_INTERNAL_CDROM0 = 10,
	DEVICE_INDEX_INTERNAL_MAX = 11
} DEVICE_INDEX_INTERNAL, *PDEVICE_INDEX_INTERNAL;

//For use in clients.
typedef enum _DEVICE_INDEX_EXTERNAL {
	DEVICE_INDEX_EXTERNAL_HARDDISK0 = 0,
	DEVICE_INDEX_EXTERNAL_HARDDISK1 = 1,
	DEVICE_INDEX_EXTERNAL_MU0 = 2,
	DEVICE_INDEX_EXTERNAL_MU1 = 3,
	DEVICE_INDEX_EXTERNAL_MU2 = 4,
	DEVICE_INDEX_EXTERNAL_MU3 = 5,
	DEVICE_INDEX_EXTERNAL_MU4 = 6,
	DEVICE_INDEX_EXTERNAL_MU5 = 7,
	DEVICE_INDEX_EXTERNAL_MU6 = 8,
	DEVICE_INDEX_EXTERNAL_MU7 = 9,
	DEVICE_INDEX_EXTERNAL_CDROM0 = 10,
	DEVICE_INDEX_EXTERNAL_MAX = 11
} DEVICE_INDEX_EXTERNAL, *PDEVICE_INDEX_EXTERNAL;

#pragma endregion Enums

#pragma region

typedef struct _VERTEX {
    FLOAT x, y, z;
    FLOAT uvx, uvy;
} VERTEX, *PVERTEX;

typedef struct _DEVICE_INFO {
	CHAR Path[29];
	HANDLE Handle;
    HANDLE Mutex;
} DEVICE_INFO, *PDEVICE_INFO;

typedef struct _MEMORY_INFO {
    LPVOID RequestMemory;
    LPVOID ResponseMemory;
    LPVOID ScratchMemory;
} MEMORY_INFO, *PMEMORY_INFO;

typedef struct _SOCKET_THREAD_PARAM {
    MEMORY_INFO Memories;
    SOCKET ClientSocket;
} SOCKET_THREAD_PARAM, *PSOCKET_THREAD_PARAM;

//Pack to make the smallest network packets possible, and easy parsing for clients.
#pragma pack(push, 1)

//Disable warning about 0-length arrays.
#pragma warning(push)
#pragma warning(disable:4200)

typedef struct _XDON_COMMAND_REQUEST_FRAME {
	UINT64 Identifier; //REQUEST_FRAME_IDENTIFIER
	BYTE Version; //PROTOCOL_VERSION
	BYTE CommandID;
} XDON_COMMAND_REQUEST_FRAME, *PXDON_COMMAND_REQUEST_FRAME;

typedef struct _XDON_COMMAND_RESPONSE_FRAME {
	UINT64 Identifier; //RESPONSE_FRAME_IDENTIFIER
	BYTE Version; //PROTOCOL_VERSION
	BYTE ConsoleType; //CONSOLE_TYPE
	NTSTATUS StatusCode; //https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55
} XDON_COMMAND_RESPONSE_FRAME, *PXDON_COMMAND_RESPONSE_FRAME;

/*typedef struct _XDON_COMMAND_IDENTIFY_REQUEST {
} XDON_COMMAND_IDENTIFY_REQUEST, *PXDON_COMMAND_IDENTIFY_REQUEST;*/

typedef struct _XDON_COMMAND_IDENTIFY_RESPONSE {
    BYTE XDONVersion;
	XBOX_KRNL_VERSION XboxVersion;
	XBOX_HARDWARE_INFO XboxHardwareInfo;
	UINT MaxIOLength;
} XDON_COMMAND_IDENTIFY_RESPONSE, *PXDON_COMMAND_IDENTIFY_RESPONSE;

/*typedef struct _XDON_COMMAND_GET_DEVICES_REQUEST {
} XDON_COMMAND_GET_DEVICES_REQUEST, *PXDON_COMMAND_GET_DEVICES_REQUEST;*/

typedef struct _XDON_COMMAND_GET_DEVICES_RESPONSE {
	struct {
		//The internal IDE HDD.
		//Raw access to the entire device can be achieved through: Harddisk0\\partition0
		BOOLEAN Harddisk0 : 1;

		//The internal IDE HDD (Cerbios secondary HDD) that takes the place of the disc drive.
		//Raw access to the entire device can be achieved through: Harddisk1\\partition0
		BOOLEAN Harddisk1 : 1;

		//The memory units are the Xbox-branded (or third-party) ones that plug into controllers.
		BOOLEAN Mu0 : 1;
		BOOLEAN Mu1 : 1;
		BOOLEAN Mu2 : 1;
		BOOLEAN Mu3 : 1;
		BOOLEAN Mu4 : 1;
		BOOLEAN Mu5 : 1;
		BOOLEAN Mu6 : 1;
		BOOLEAN Mu7 : 1;

		//The disc drive that game discs go in. Also supports other CDs.
		//Raw access to the entire disc can be achieved through: CdRom0
		BOOLEAN CdRom0 : 1;
	} AvailableDevices;

	DISK_GEOMETRY Harddisk0Geometry;
	DISK_GEOMETRY Harddisk1Geometry;
	DISK_GEOMETRY Mu0Geometry;
	DISK_GEOMETRY Mu1Geometry;
    DISK_GEOMETRY Mu2Geometry;
	DISK_GEOMETRY Mu3Geometry;
	DISK_GEOMETRY Mu4Geometry;
	DISK_GEOMETRY Mu5Geometry;
	DISK_GEOMETRY Mu6Geometry;
	DISK_GEOMETRY Mu7Geometry;
	DISK_GEOMETRY CdRom0Geometry;

	BOOLEAN Harddisk0InfoAvailable;
	IDENTIFY_DEVICE_DATA Harddisk0Info;

	BOOLEAN Harddisk1InfoAvailable;
	IDENTIFY_DEVICE_DATA Harddisk1Info;
} XDON_COMMAND_GET_DEVICES_RESPONSE, *PXDON_COMMAND_GET_DEVICES_RESPONSE;

typedef struct _XDON_COMMAND_READ_REQUEST {
	BYTE DeviceIndex;
	INT64 Offset;
	UINT Length;
	BOOLEAN CompressResponseData; //Compression is LZ4, a very fast algorithm that brings noticeable benefits to network transfers.
} XDON_COMMAND_READ_REQUEST, *PXDON_COMMAND_READ_REQUEST;

typedef struct _XDON_COMMAND_READ_RESPONSE {
	UINT DataLength; //If 0 and NT_SUCCESS(Frame.StatusCode), a performance-saving measure was done because the data is empty (all bytes have a value of 0).
	UINT UncompressedDataLength;
	BYTE Data[0];
} XDON_COMMAND_READ_RESPONSE, *PXDON_COMMAND_READ_RESPONSE;

typedef struct _XDON_COMMAND_WRITE_REQUEST {
	BYTE DeviceIndex;
	INT64 Offset;
	UINT DataLength;
	BOOLEAN DataIsCompressed;
	BYTE Data[0];
} XDON_COMMAND_WRITE_REQUEST, *PXDON_COMMAND_WRITE_REQUEST;

/*typedef struct _XDON_COMMAND_WRITE_RESPONSE {
} XDON_COMMAND_WRITE_RESPONSE, *PXDON_COMMAND_WRITE_RESPONSE;*/

//Write Same is a performant function for when you want to write a lot of the same data, like when you want to wipe a drive with a zero-pass. XDON will create a buffer of size Length filled with Value internally so no data needs to be sent.
typedef struct _XDON_COMMAND_WRITE_SAME_REQUEST {
	BYTE DeviceIndex;
	INT64 Offset;
	UINT Length;
	BYTE Value;
} XDON_COMMAND_WRITE_SAME_REQUEST, *PXDON_COMMAND_WRITE_SAME_REQUEST;

/*typedef struct _XDON_COMMAND_WRITE_SAME_RESPONSE {
} XDON_COMMAND_WRITE_SAME_RESPONSE, *PXDON_COMMAND_WRITE_SAME_RESPONSE;*/

//Only for the internal SATA HDD.
typedef struct _XDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST {
	IDEREGS Registers;
	UINT DataInOutSize;
	BYTE DataOut[0];
} XDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST, *PXDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST;

typedef struct _XDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE {
	IDEREGS Registers;
	BYTE DataIn[0];
} XDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE, *PXDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE;

typedef struct _XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST {
	BYTE Routine;
} XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST, *PXDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST;

/*typedef struct _XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_RESPONSE {
} XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_RESPONSE, *PXDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_RESPONSE;*/

#pragma warning(pop)

#pragma pack(pop)

//Fix for "the size of an array must be greater than zero" IntelliSense error.
#ifdef __INTELLISENSE__
#define C_ASSERT(e)
#endif

//These ensure the structs are the correct sizes that XDON clients are coded to accept.
//"error C2118: negative subscript" means there is a mismatch that needs fixing!
C_ASSERT(sizeof(XDON_COMMAND_REQUEST_FRAME) == XDON_COMMAND_REQUEST_FRAME_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_RESPONSE_FRAME) == XDON_COMMAND_RESPONSE_FRAME_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_IDENTIFY_RESPONSE) == XDON_COMMAND_IDENTIFY_RESPONSE_SIZE);
//Anything larger than 1472 won't work over UDP.
C_ASSERT(sizeof(XDON_COMMAND_IDENTIFY_RESPONSE) < 1472);
C_ASSERT(sizeof(XDON_COMMAND_GET_DEVICES_RESPONSE) == XDON_COMMAND_GET_DEVICES_RESPONSE_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_READ_REQUEST) == XDON_COMMAND_READ_REQUEST_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_READ_RESPONSE) == XDON_COMMAND_READ_RESPONSE_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_WRITE_REQUEST) == XDON_COMMAND_WRITE_REQUEST_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_WRITE_SAME_REQUEST) == XDON_COMMAND_WRITE_SAME_REQUEST_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST) == XDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE) == XDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE_SIZE);
C_ASSERT(sizeof(XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST) == XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST_SIZE);

//These must be aligned for the kernel to accept them.
C_ASSERT((offsetof(XDON_COMMAND_READ_RESPONSE, Data) & FILE_WORD_ALIGNMENT) == 0);
C_ASSERT((offsetof(XDON_COMMAND_WRITE_REQUEST, Data) & FILE_WORD_ALIGNMENT) == 0);

#pragma endregion Structs