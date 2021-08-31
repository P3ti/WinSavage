#include <iostream>
#include <windows.h>
#include <varargs.h>
#include "patchutil.h"
#include "memory.h"

uintptr_t SQ;
size_t SQ_SIZE;
uintptr_t LoadSQ(const char* path, size_t& virtualSize, void(*patchFunc)(uintptr_t));

HWND GameWindow = NULL;
uint8_t* KeyMap;

void PatchWatcomCrt(uintptr_t SQ);
bool PatchGlide2x(uintptr_t SQ);
void PatchAudioSystem(uintptr_t SQ);

int Displayf(const char* format, ...);

WatcomFunc<void*(void*, const char*)> HashTable_Access;
WatcomFunc<bool(void*, const char*, void*)> HashTable_Insert;

WatcomFunc<void(void*, int, char**)> datArgParser_Init;
void datArgParser_Init_Hook(void* datArgParser, int argc, char* argv[]);

WatcomFunc<void(void*)> rtRoot_Update;
void rtRoot_Update_Hook(void* rtRoot);

uint32_t rdtsc_Hook();

void PatchSQ(uintptr_t SQ)
{
    PatchWatcomCrt(SQ);

    if(!PatchGlide2x(SQ))
        exit(2);

    PatchAudioSystem(SQ);

    Nop(SQ + 0x12A1D6, 5); // don't install SIGSEGV handler

    WatcomHook(SQ + 0x21090, Displayf);

    Patch(SQ + 0x179CCF, { 0xC3 }); // plug query
    Patch(SQ + 0xF6E80, { 0xCC }); // get immersia product

    Patch(SQ + 0xF692C, { 0xC3 }); // plug error
    Patch(SQ + 0x139300, { 0xC3 }); // PlugUtilities.cpp (450) : 'code <=0' -
    Patch(SQ + 0xF75C0, { 0x31, 0xC0, 0xC3 }); // ProtectionPlug1.cpp (321) : 'code == 0' -
    Patch(SQ + 0xF7F30, { 0xC3 }); // VerifyProductProtectionPlug
    Patch(SQ + 0xF6EF0, { 0xC3 }); // dongle check at ImmCoinOpToolkit init
    Patch(SQ + 0x15D32A, { 0xCC }); // AImmProtocol::GetInstance
    Patch(SQ + 0x101862, { 0xC3 }); // do not connect to immersia peripherals

    Patch(SQ + 0xFE917, { 0xE9, 0x08, 0x01, 0x00, 0x00 }); // skip ImmProtocol init

    Patch(SQ + 0x21F70, { 0xC3 }); // ??

    KeyMap = (uint8_t*)(SQ + 0x503F58);

    HashTable_Access = SQ + 0x45260;
    HashTable_Insert = SQ + 0x44EC0;

    datArgParser_Init = SQ + 0x2EBE0;
    Hook(SQ + 0x12A240, 0xE8, SQ + 0x1793F1);
    WatcomHook(SQ + 0x1793F1, datArgParser_Init_Hook);

    rtRoot_Update = MethodHook(SQ + 0x2AD788, SQ + 0x3314A);
    WatcomHook(SQ + 0x3314A, rtRoot_Update_Hook);
    WatcomHook(SQ + 0x33140, rdtsc_Hook);
}

LONG WINAPI ExceptionHandler(_EXCEPTION_POINTERS* Info);

int main(int argc, char* argv[], char* envp[])
{
    timeBeginPeriod(1);

    if(GetFileAttributesA("PSAVAGE.EXE") == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
    {
        fprintf(stderr, "PSAVAGE.EXE not found\n");
        return 1;
    }

    SQ = LoadSQ("PSAVAGE.EXE", SQ_SIZE, PatchSQ);
    if(!SQ)
    {
        fprintf(stderr, "Unable to load game\n");
        return 1;
    }

    if(SQ_SIZE != 0x764AB0 || memcmp((const char*)(SQ + 0x225B4B), "SQ v0.92 May 13 1999", 21))
    {
        fprintf(stderr, "Unsupported game version\n");
        return 1;
    }

    Displayf("SQ %08X", SQ);
    SetUnhandledExceptionFilter(ExceptionHandler);

    WatcomCall(SQ + 0x977DC, 0xFF); // InitRtns
    return WatcomCall<int>(SQ + 0x12A1C0, argc, argv, envp); // main
}

int Displayf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    putchar('\n');
    va_end(args);
    return ret;
}

void FatalError(const char* format, ...)
{
    static char buffer[256];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);

    fprintf(stderr, "Fatal Error: %s\n", buffer);
    MessageBoxA(NULL, buffer, "Fatal Error", MB_ICONERROR | MB_OK);
    exit(0);
}

void datArgParser_Init_Hook(void* datArgParser, int argc, char* argv[])
{
    datArgParser_Init(datArgParser, argc, argv);

    struct Entry
    {
        size_t num;
        const char** args;
    };
    static_assert(sizeof(Entry) == 8, "Wrong size: datArgParser_Init_Hook::Entry");

#define InsertDefaultPath(name, defaultPath) \
    if(!HashTable_Access(datArgParser, name)) \
    { \
        static char absolutePath[MAX_PATH]; \
        if(_fullpath(absolutePath, defaultPath, MAX_PATH)) \
            HashTable_Insert(datArgParser, name, new Entry { 1, new const char* { absolutePath } }); \
    }

    InsertDefaultPath("path", "..\\wav");
    InsertDefaultPath("hdbpath", "..\\iff\\database");
    InsertDefaultPath("archive", "..\\arc\\savage");
}

#include <Xinput.h>
void UpdateGamepadState()
{
    static DWORD (__stdcall *GetState)(DWORD, XINPUT_STATE*) = []()
    {
        HMODULE XInputLibrary = LoadLibraryA("xinput1_3.dll");
        if(!XInputLibrary)
            return (DWORD (__stdcall*)(DWORD, XINPUT_STATE*))0;

        return (DWORD (__stdcall*)(DWORD, XINPUT_STATE*))GetProcAddress(XInputLibrary, "XInputGetState");
    }();

    if(!GetState)
        return;

    DWORD controllerIndex = 0xFFFFFFFF;

    XINPUT_STATE controllerState;
    memset(&controllerState, 0, sizeof(XINPUT_STATE));

    if(controllerIndex == 0xFFFFFFFF)
    {
        DWORD i = 0;
        do
        {
            if(GetState(i, &controllerState) == ERROR_SUCCESS)
                controllerIndex = i;

            ++i;
        }
        while(i < XUSER_MAX_COUNT && controllerIndex == 0xFFFFFFFF);
    }
    else
    {
        if(GetState(controllerIndex, &controllerState) != ERROR_SUCCESS)
            controllerIndex = 0xFFFFFFFF;
    }

    if(controllerIndex != 0xFFFFFFFF)
    {
        uintptr_t inputMgr = *(uintptr_t*)(SQ + 0x4FE104);
        uintptr_t joystick = inputMgr ? *(uintptr_t*)(inputMgr + 60) : 0;
        if(joystick)
        {
            *(float*)(joystick + 1948) = controllerState.Gamepad.sThumbLY / (float)0x7FFF;
            *(float*)(joystick + 1956) = controllerState.Gamepad.sThumbLX / (float)0x7FFF;

            if((controllerState.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0)
                *(uint8_t*)(joystick + 1964) |= 1; // bite
            else
                *(uint8_t*)(joystick + 1964) &= ~1;

            if((controllerState.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0)
                *(uint8_t*)(joystick + 1964) |= 2; // charge
            else
                *(uint8_t*)(joystick + 1964) &= ~2;

            if((controllerState.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0)
                *(uint8_t*)(joystick + 1964) |= 4; // roar
            else
                *(uint8_t*)(joystick + 1964) &= ~4;

            if((controllerState.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0)
                *(uint8_t*)(joystick + 1964) |= 8; // start
            else
                *(uint8_t*)(joystick + 1964) &= ~8;
        }
    }
}

static uint8_t VirtKeyToDosScanCode(uint32_t virtKey);
LRESULT CALLBACK GameWindowProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_DESTROY:
        case WM_QUIT:
        {
            exit(0);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        {
            uint8_t keyCode = VirtKeyToDosScanCode(wParam);
            if(keyCode != 0xFF)
                KeyMap[keyCode] = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);

            return 0;
        }

        default:
            return DefWindowProcW(wnd, msg, wParam, lParam);
    }
}

void rtRoot_Update_Hook(void* rtRoot)
{
    UpdateGamepadState();

    MSG msg;
    while(PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    rtRoot_Update(rtRoot);
}

#pragma comment(lib, "Winmm.lib")
uint32_t rdtsc_Hook()
{
    static uint32_t count = 0;

    static double counterFreqScale = []()
    {
        LARGE_INTEGER counterFreq;
        QueryPerformanceFrequency(&counterFreq);
        return 448053660.0 / counterFreq.QuadPart;
    }();

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    static LARGE_INTEGER prev = now;
    count += static_cast<uint32_t>((now.QuadPart - prev.QuadPart) * counterFreqScale);
    prev = now;
    return count;
}

const char* GetExceptionName(DWORD ExceptionCode)
{
    switch(ExceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
        case DBG_CONTROL_C: return "DBG_CONTROL_C";
        default: return nullptr;
    }
}

LONG WINAPI ExceptionHandler(_EXCEPTION_POINTERS* Info)
{
    const char* exceptionName = GetExceptionName(Info->ExceptionRecord->ExceptionCode);
    if(exceptionName)
        fprintf(stderr, "%s\n", exceptionName);
    else
        fprintf(stderr, "Exception code: %08x\n", Info->ExceptionRecord->ExceptionCode);

    uintptr_t exceptionAddress = (uintptr_t)Info->ExceptionRecord->ExceptionAddress;
    fprintf(stderr, "Exception address: 0x%08X", exceptionAddress);
    if(exceptionAddress >= SQ && exceptionAddress < SQ + SQ_SIZE)
        fprintf(stderr, " (0x%08X)\n", exceptionAddress - SQ);
    else
        fprintf(stderr, "\n");

    switch(Info->ExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION:
        {
            switch(Info->ExceptionRecord->ExceptionInformation[0])
            {
                case 0:
                    fprintf(stderr, "Attempted to read from: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[1]);
                    break;

                case 1:
                    fprintf(stderr, "Attempted to write to: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[1]);
                    break;

                case 8:
                    fprintf(stderr, "Data Execution Prevention (DEP) at: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[1]);
                    break;

                default:
                    fprintf(stderr, "Unknown access violation at: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[1]);
                    break;
            }

            break;
        }

        case EXCEPTION_IN_PAGE_ERROR:
        {
            switch(Info->ExceptionRecord->ExceptionInformation[0])
            {
                case 0:
                    fprintf(stderr, "Attempted to read from: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[1]);
                    break;

                case 1:
                    fprintf(stderr, "Attempted to write to: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[1]);
                    break;

                case 8:
                    fprintf(stderr, "Data Execution Prevention (DEP) at: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[1]);
                    break;

                default:
                    fprintf(stderr, "Unknown access violation at: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[1]);
                    break;
            }

            fprintf(stderr, "NTSTATUS: 0x%08X\n", Info->ExceptionRecord->ExceptionInformation[2]);
            break;
        }

        default:
            break;
    }

    if(Info->ContextRecord->ContextFlags & CONTEXT_SEGMENTS)
    {
        fprintf(stderr, "GS = 0x%x, FS = 0x%x, ES = 0x%x, DS = 0x%x\n", Info->ContextRecord->SegGs, Info->ContextRecord->SegFs, Info->ContextRecord->SegEs, Info->ContextRecord->SegDs);
    }

    if(Info->ContextRecord->ContextFlags & CONTEXT_INTEGER)
    {
        fprintf(stderr, "EDI = 0x%x, ESI = 0x%x, EBX = 0x%x\n", Info->ContextRecord->Edi, Info->ContextRecord->Esi, Info->ContextRecord->Ebx);
        fprintf(stderr, "EDX = 0x%x, ECX = 0x%x, EAX = 0x%x\n", Info->ContextRecord->Edx, Info->ContextRecord->Ecx, Info->ContextRecord->Eax);
    }

    if(Info->ContextRecord->ContextFlags & CONTEXT_CONTROL)
    {
        if(Info->ContextRecord->Eip >= SQ && Info->ContextRecord->Eip < SQ + SQ_SIZE)
            fprintf(stderr, "EBP = 0x%08X, EIP = 0x%x (0x%08X), CS = 0x%08X\n", Info->ContextRecord->Ebp, Info->ContextRecord->Eip, Info->ContextRecord->Eip - SQ, Info->ContextRecord->SegCs);
        else
            fprintf(stderr, "EBP = 0x%08X, EIP = 0x%08X, CS = 0x%08X\n", Info->ContextRecord->Ebp, Info->ContextRecord->Eip, Info->ContextRecord->SegCs);

        fprintf(stderr, "EFLAGS = 0x%08X, ESP = 0x%08X, SS = 0x%08X\n", Info->ContextRecord->EFlags, Info->ContextRecord->Esp, Info->ContextRecord->SegSs);
    }

    uintptr_t Esp = Info->ContextRecord->Esp;
    for(size_t i = 0; i < 480; i += 16)
    {
        uintptr_t stackValues[4] = { *(uintptr_t*)(Esp + i), *(uintptr_t*)(Esp + i + 4), *(uintptr_t*)(Esp + i + 8), *(uintptr_t*)(Esp + i + 12) };
        fprintf(stderr, "+%04X: 0x%08X 0x%08X 0x%08X 0x%08X", i, stackValues[0], stackValues[1], stackValues[2], stackValues[3]);

        bool printOffsets = false;
        for(size_t j = 0; j < 4; ++j)
        {
            if(stackValues[j] >= SQ && stackValues[j] < SQ + SQ_SIZE)
            {
                stackValues[j] -= SQ;
                printOffsets |= true;
            }
            else
            {
                stackValues[j] = 0;
            }
        }

        if(printOffsets)
            fprintf(stderr, " <0x%08X, 0x%08X, 0x%08X, 0x%08X>", stackValues[0], stackValues[1], stackValues[2], stackValues[3]);

        fprintf(stderr, "\n");
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

static uint8_t VirtKeyToDosScanCode(uint32_t virtKey)
{
    switch(virtKey)
    {
        case VK_ESCAPE: return 1;
        case 0x31: return 2; // 1
        case 0x32: return 3; // 2
        case 0x33: return 4; // 3
        case 0x34: return 5; // 4
        case 0x35: return 6; // 5
        case 0x36: return 7; // 6
        case 0x37: return 8; // 7
        case 0x38: return 9; // 8
        case 0x39: return 10; // 9
        case 0x30: return 11; // 0
        case VK_OEM_MINUS: return 12;
        case VK_OEM_NEC_EQUAL: return 13;
        case VK_BACK: return 14;
        case VK_TAB: return 15;
        case 0x51: return 16; // q
        case 0x57: return 17; // w
        case 0x45: return 18; // e
        case 0x52: return 19; // r
        case 0x54: return 20; // t
        case 0x59: return 21; // y
        case 0x55: return 22; // u
        case 0x49: return 23; // i
        case 0x4F: return 24; // o
        case 0x50: return 25; // p
        case VK_RETURN: return 28;
        case VK_CONTROL: return 29;
        case 0x41: return 30; // a
        case 0x53: return 31; // s
        case 0x44: return 32; // d
        case 0x46: return 33; // f
        case 0x47: return 34; // g
        case 0x48: return 35; // h
        case 0x4A: return 36; // j
        case 0x4B: return 37; // k
        case 0x4C: return 38; // l
        case VK_SHIFT: return 42;
        case 0x5A: return 44; // z
        case 0x58: return 45; // x
        case 0x43: return 46; // c
        case 0x56: return 47; // v
        case 0x42: return 48; // b
        case 0x4E: return 49; // n
        case 0x4D: return 50; // m
        case VK_OEM_COMMA: return 51;
        case VK_OEM_PERIOD: return 52;
        case VK_MULTIPLY: return 55;
        case VK_MENU: return 56;
        case VK_SPACE: return 57;
        case VK_CAPITAL: return 58;
        case VK_F1: return 59;
        case VK_F2: return 60;
        case VK_F3: return 61;
        case VK_F4: return 62;
        case VK_F5: return 63;
        case VK_F6: return 64;
        case VK_F7: return 65;
        case VK_F8: return 66;
        case VK_F9: return 67;
        case VK_F10: return 68;
        case VK_NUMLOCK: return 69;
        case VK_SCROLL: return 70;
        case VK_NUMPAD7: return 71;
        case VK_NUMPAD8: return 72;
        case VK_NUMPAD9: return 73;
        case VK_NUMPAD4: return 75;
        case VK_NUMPAD5: return 76;
        case VK_NUMPAD6: return 77;
        case VK_OEM_PLUS: return 78;
        case VK_NUMPAD1: return 79;
        case VK_NUMPAD2: return 80;
        case VK_NUMPAD3: return 81;
        case VK_NUMPAD0: return 82;
        case VK_F11: return 87;
        case VK_F12: return 88;
        case VK_DIVIDE: return 53;
        case VK_HOME: return 71;
        case VK_UP: return 72;
        case VK_PRIOR: return 73;
        case VK_LEFT: return 75;
        case VK_RIGHT: return 77;
        case VK_END: return 79;
        case VK_DOWN: return 80;
        case VK_NEXT: return 81;
        case VK_INSERT: return 82;
        case VK_DELETE: return 83;
        default: return 0xFF;
    }
}
