// Copyright © Eaton Works 2025. All rights reserved.
// License: https://github.com/EatonZ/XDON/blob/main/LICENSE

#pragma once

#pragma region

//3 memory buffers get allocated at startup.
//The 0x1000 is to account for request and response structs before any variable-length data. Frames are not part of this.
#define MAX_IO_LENGTH          1048576 //When doing a single IO operation, 1 MB has proven to be the sweet spot. If you want to go higher, you can change this, but it won't be any faster.
#define MAX_COMPRESSION_LENGTH LZ4_COMPRESSBOUND(MAX_IO_LENGTH)
#define REQUEST_MEMORY_SIZE	   MAX_COMPRESSION_LENGTH + 0x1000 //Used to read the request and any variable-length request data.
#define RESPONSE_MEMORY_SIZE   MAX_COMPRESSION_LENGTH + 0x1000 //Used to prepare responses with variable-length response data.
#define SCRATCH_MEMORY_SIZE	   MAX_IO_LENGTH //Used for anything.

#define CONSOLE_TYPE              1 //1=Xbox 360
#define PC_COMMUNICATION_PORT     1000 //Why port 1000? Look here: https://i.imgur.com/B4PsFu6.png
#define SOCKET_BUFFER_SIZE	      0x4000 //Currently the Xbox default. Not really any benefit to changing based on our testing.
#define SOCKET_TIMEOUT  	      0 //Timeout for TCP SO_SNDTIMEO and SO_RCVTIMEO.
#define REQUEST_FRAME_IDENTIFIER  0x58444F4E52455155 //XDONREQU
#define RESPONSE_FRAME_IDENTIFIER 0x58444F4E52455350 //XDONRESP
#define PROTOCOL_VERSION          1 //Increase when a breaking change is made to the protocol.
#define XDON_VERSION              1 //Keep this in sync with XDON.xml and BASE_VER & UPDATE_VER environment variables.

//Must keep in sync with XDON clients.
#define XDON_COMMAND_REQUEST_FRAME_SIZE                   0xA
#define XDON_COMMAND_RESPONSE_FRAME_SIZE                  0xE
#define XDON_COMMAND_IDENTIFY_RESPONSE_SIZE               0x4D
#define XDON_COMMAND_GET_DEVICES_RESPONSE_SIZE            0x14BE
#define XDON_COMMAND_READ_REQUEST_SIZE                    0xE
#define XDON_COMMAND_READ_RESPONSE_SIZE                   8
#define XDON_COMMAND_WRITE_REQUEST_SIZE                   0xE
#define XDON_COMMAND_WRITE_SAME_REQUEST_SIZE              0xE
#define XDON_COMMAND_ATA_CUSTOM_COMMAND_REQUEST_SIZE      0xC
#define XDON_COMMAND_ATA_CUSTOM_COMMAND_RESPONSE_SIZE     8
#define XDON_COMMAND_REBOOT_SHUTDOWN_CONSOLE_REQUEST_SIZE 1

#pragma endregion Defs

#pragma region

typedef enum _PRINT_VERBOSITY_FLAG : BYTE {
	PRINT_VERBOSITY_FLAG_ESSENTIAL_AND_ERRORS = 1, //Print essential messages about XDON operation the user will want to see.
	PRINT_VERBOSITY_FLAG_REQUESTS = 2, //Print informational messages from FulfillRequest and ReceiveAndValidateRequest. A little noisy.
    PRINT_VERBOSITY_FLAG_SOCK_SEND_RECV = 4 //Enable printing in SockSend and SockReceive. Very noisy!
} PRINT_VERBOSITY_FLAG, *PPRINT_VERBOSITY_FLAG;

typedef enum _XDON_COMMAND : BYTE {
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

//For internal use. Make sure memory unit pairs stick together.
typedef enum _DEVICE_INDEX_INTERNAL : BYTE {
	DEVICE_INDEX_INTERNAL_HARDDISK0_PARTITION0 = 0,
	DEVICE_INDEX_INTERNAL_MU0SYSTEM = 1,
	DEVICE_INDEX_INTERNAL_MU0 = 2,
	DEVICE_INDEX_INTERNAL_MU1SYSTEM = 3,
	DEVICE_INDEX_INTERNAL_MU1 = 4,
	DEVICE_INDEX_INTERNAL_BUILTINMUMMC_RESERVATIONPARTITION = 5,
	DEVICE_INDEX_INTERNAL_BUILTINMUMMC_STORAGE = 6,
	DEVICE_INDEX_INTERNAL_BUILTINMUUSB_RESERVATIONPARTITION = 7,
	DEVICE_INDEX_INTERNAL_BUILTINMUUSB_STORAGE = 8,
	DEVICE_INDEX_INTERNAL_BUILTINMUSFC = 9,
	DEVICE_INDEX_INTERNAL_BUILTINMUSFCSYSTEM = 10,
	DEVICE_INDEX_INTERNAL_MASS0 = 11,
	DEVICE_INDEX_INTERNAL_MASS1 = 12,
	DEVICE_INDEX_INTERNAL_MASS2 = 13,
	DEVICE_INDEX_INTERNAL_FLASH = 14,
	DEVICE_INDEX_INTERNAL_CDROM0 = 15,
	DEVICE_INDEX_INTERNAL_MAX = 16
} DEVICE_INDEX_INTERNAL, *PDEVICE_INDEX_INTERNAL;

//For use in clients.
typedef enum _DEVICE_INDEX_EXTERNAL : BYTE {
	DEVICE_INDEX_EXTERNAL_HARDDISK0 = 0,
	DEVICE_INDEX_EXTERNAL_MU0 = 1,
	DEVICE_INDEX_EXTERNAL_MU1 = 2,
	DEVICE_INDEX_EXTERNAL_BUILTINMUMMC = 3,
	DEVICE_INDEX_EXTERNAL_BUILTINMUUSB = 4,
	DEVICE_INDEX_EXTERNAL_BUILTINMUSFC = 5,
	DEVICE_INDEX_EXTERNAL_MASS0 = 6,
	DEVICE_INDEX_EXTERNAL_MASS1 = 7,
	DEVICE_INDEX_EXTERNAL_MASS2 = 8,
	DEVICE_INDEX_EXTERNAL_FLASH = 9,
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
	CHAR Path[42];
	HANDLE Handle;
    HANDLE Mutex;
	PARTITION_INFORMATION PartitionInfo;
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
	UINT64 Identifier; //REQUEST_FRAME_IDENTIFIER (little endian)
	BYTE Version; //PROTOCOL_VERSION
	XDON_COMMAND CommandID;
} XDON_COMMAND_REQUEST_FRAME, *PXDON_COMMAND_REQUEST_FRAME;

typedef struct _XDON_COMMAND_RESPONSE_FRAME {
	UINT64 Identifier; //RESPONSE_FRAME_IDENTIFIER (little endian)
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
	//+1 for null terminator.
	WCHAR XboxName[23 + 1];
	UINT MaxIOLength;
} XDON_COMMAND_IDENTIFY_RESPONSE, *PXDON_COMMAND_IDENTIFY_RESPONSE;

/*typedef struct _XDON_COMMAND_GET_DEVICES_REQUEST {
} XDON_COMMAND_GET_DEVICES_REQUEST, *PXDON_COMMAND_GET_DEVICES_REQUEST;*/

#pragma bitfield_order(push, lsb_to_msb) //Required on Xbox 360 to ensure correct ordering.
typedef struct _XDON_COMMAND_GET_DEVICES_RESPONSE {
	struct {
		//The internal SATA HDD.
		//Raw access to the entire device can be achieved through: Harddisk0\\partition0
		BOOLEAN Harddisk0 : 1;

		//The memory units are the Xbox-branded (or third-party) ones that plug into phat consoles, and on slim consoles via USB. These are FATX-formatted.
		//Raw access to the entire device must be done through 2 paths: Mu0System, Mu0, or Mu1System, Mu1
		//These are not accessible via the Mass paths.
		BOOLEAN Mu0 : 1;
		BOOLEAN Mu1 : 1;

		//The built-in memory units are on the console motherboard. Mmc is the nand flash and Usb is the USB-connected module.
		//Raw access to the Mmc and Usb variants must be done through 2 paths: BuiltInMu(Mmc/Usb)\\ReservationPartition, BuiltInMu(Mmc/USB)\\Storage
		//ReservationPartition covers all the system partitions except Storage.
		BOOLEAN BuiltInMuMmc : 1;
		BOOLEAN BuiltInMuUsb : 1;

		//The Sfc memory unit is part of the nand flash.
		//Raw access to this part of the nand must be done through 2 paths: BuiltInMuSfcSystem, BuiltInMuSfc
		BOOLEAN BuiltInMuSfc : 1;

		//The Mass devices are connected USB sticks or external HDDs. These are FAT16, FAT32, or HFS+ formatted. FATX-formatted when XL.
		//Raw access to the entire device can be achieved through: Mass0, Mass1, Mass2
		BOOLEAN Mass0 : 1;
		BOOLEAN Mass1 : 1;
		BOOLEAN Mass2 : 1;

		//The raw NAND flash.
		//Raw access to the entire device can be achieved through: Flash
		BOOLEAN Flash : 1;

		//The disc drive that game discs go in. Also supports other CDs.
		//Raw access to the entire disc can be achieved through: CdRom0
		BOOLEAN CdRom0 : 1;
	} AvailableDevices;

	DISK_GEOMETRY Harddisk0Geometry;
	DISK_GEOMETRY Mu0Geometry;
	DISK_GEOMETRY Mu1Geometry;
    DISK_GEOMETRY BuiltInMuMmcGeometry;
	DISK_GEOMETRY BuiltInMuUsbGeometry;
	DISK_GEOMETRY BuiltInMuSfcGeometry;
	DISK_GEOMETRY Mass0Geometry;
	DISK_GEOMETRY Mass1Geometry;
	DISK_GEOMETRY Mass2Geometry;
	FLASH_GEOMETRY FlashGeometry;
	DISK_GEOMETRY CdRom0Geometry;

	BOOLEAN Harddisk0InfoAvailable;
	IDENTIFY_DEVICE_DATA Harddisk0Info;

	//All the below descriptors are guaranteed to have a bDescriptorType of USB_STRING_DESCRIPTOR_TYPE.
	//bLength being 0 means there is no descriptor, or an error occurred while getting it.
	USB_STRING_DESCRIPTOR_MAX Mu0Manufacturer;
	USB_STRING_DESCRIPTOR_MAX Mu0Product;
	USB_STRING_DESCRIPTOR_MAX Mu0SerialNumber;

	USB_STRING_DESCRIPTOR_MAX Mu1Manufacturer;
	USB_STRING_DESCRIPTOR_MAX Mu1Product;
	USB_STRING_DESCRIPTOR_MAX Mu1SerialNumber;

	USB_STRING_DESCRIPTOR_MAX BuiltInMuUsbManufacturer;
	USB_STRING_DESCRIPTOR_MAX BuiltInMuUsbProduct;
	USB_STRING_DESCRIPTOR_MAX BuiltInMuUsbSerialNumber;

	USB_STRING_DESCRIPTOR_MAX Mass0Manufacturer;
	USB_STRING_DESCRIPTOR_MAX Mass0Product;
	USB_STRING_DESCRIPTOR_MAX Mass0SerialNumber;

	USB_STRING_DESCRIPTOR_MAX Mass1Manufacturer;
	USB_STRING_DESCRIPTOR_MAX Mass1Product;
	USB_STRING_DESCRIPTOR_MAX Mass1SerialNumber;

	USB_STRING_DESCRIPTOR_MAX Mass2Manufacturer;
	USB_STRING_DESCRIPTOR_MAX Mass2Product;
	USB_STRING_DESCRIPTOR_MAX Mass2SerialNumber;

	BOOLEAN CdRom0InfoAvailable;
	INQUIRYDATA CdRom0Info;
} XDON_COMMAND_GET_DEVICES_RESPONSE, *PXDON_COMMAND_GET_DEVICES_RESPONSE;
#pragma bitfield_order(pop)

typedef struct _XDON_COMMAND_READ_REQUEST {
	DEVICE_INDEX_EXTERNAL DeviceIndex;
	INT64 Offset;
	UINT Length;
	BOOLEAN CompressResponseData; //Compression is LZ4, a very fast algorithm that brings noticeable benefits to network transfers. LZX, on the other hand, took too much CPU and slowed things down.
} XDON_COMMAND_READ_REQUEST, *PXDON_COMMAND_READ_REQUEST;

typedef struct _XDON_COMMAND_READ_RESPONSE {
	UINT DataLength; //If 0 and NT_SUCCESS(Frame.StatusCode), a performance-saving measure was done because the data is empty (all bytes have a value of 0).
	UINT UncompressedDataLength;
	BYTE Data[0];
} XDON_COMMAND_READ_RESPONSE, *PXDON_COMMAND_READ_RESPONSE;

typedef struct _XDON_COMMAND_WRITE_REQUEST {
	DEVICE_INDEX_EXTERNAL DeviceIndex;
	INT64 Offset;
	UINT DataLength;
	BOOLEAN DataIsCompressed;
	BYTE Data[0];
} XDON_COMMAND_WRITE_REQUEST, *PXDON_COMMAND_WRITE_REQUEST;

/*typedef struct _XDON_COMMAND_WRITE_RESPONSE {
} XDON_COMMAND_WRITE_RESPONSE, *PXDON_COMMAND_WRITE_RESPONSE;*/

//Write Same is a performant function for when you want to write a lot of the same data, like when you want to wipe a drive with a zero-pass. XDON will create a buffer of size Length filled with Value internally so no data needs to be sent.
typedef struct _XDON_COMMAND_WRITE_SAME_REQUEST {
	DEVICE_INDEX_EXTERNAL DeviceIndex;
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
	FIRMWARE_REENTRY Routine;
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
C_ASSERT((offsetof(XDON_COMMAND_GET_DEVICES_RESPONSE, CdRom0Info) & FILE_WORD_ALIGNMENT) == 0);
C_ASSERT((offsetof(XDON_COMMAND_READ_RESPONSE, Data) & FILE_WORD_ALIGNMENT) == 0);
C_ASSERT((offsetof(XDON_COMMAND_WRITE_REQUEST, Data) & FILE_WORD_ALIGNMENT) == 0);

#pragma endregion Structs