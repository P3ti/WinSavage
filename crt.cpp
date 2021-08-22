#include <stdio.h>
#include <stdarg.h>
#include <windows.h>
#include <time.h>
#include "patchutil.h"

uintptr_t sq_std_streams = 0;

FILE* get_sq_std_stream(FILE* stream)
{
    switch((uintptr_t)stream - sq_std_streams)
    {
        case 0 * 26: return stdin;
        case 1 * 26: return stdout;
        case 2 * 26: return stderr;
        case 3 * 26: return stdin;
        case 4 * 26: return stdout;
        default: return stream;
    }
}

uintptr_t stackavail()
{
    static uintptr_t StackPtr;
    _asm mov StackPtr, esp

    static MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery((PVOID)StackPtr, &mbi, sizeof(mbi));
    return StackPtr - (uintptr_t)mbi.AllocationBase;
}

void exit_hook(int exitCode)
{
    printf("Game exiting: %d\n", exitCode);
    DebugBreak();

    exit(exitCode);
}

int fgetc_hook(FILE* stream)
{
    return fgetc(get_sq_std_stream(stream));
}

int fscanf_hook(FILE* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vfscanf(get_sq_std_stream(stream), format, args);
    va_end(args);
    return ret;
}

int fputc_hook(int character, FILE* stream)
{
    return fputc(character, get_sq_std_stream(stream));
}

int fputs_hook(const char* str, FILE* stream)
{
    return fputs(str, get_sq_std_stream(stream));
}

int fprintf_hook(FILE* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vfprintf(get_sq_std_stream(stream), format, args);
    va_end(args);
    return ret;
}

int fflush_hook(FILE* stream)
{
    return fflush(get_sq_std_stream(stream));
}

void PatchWatcomCrt(uintptr_t SQ)
{
    sq_std_streams = SQ + 0x2B9F18;

    Patch(SQ + 0x21F0C, { 0xC3 }); // return sys_init_387_emulator
    Patch(SQ + 0x1793F0, { 0xC3 }); // return Init_Argv
    Patch(SQ + 0x1D0930, { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 }); // return nonIBM (1)
    Patch(SQ + 0xEAAB0, { 0xC3 }); // return InitFiles
    Patch(SQ + 0xEC8E8, { 0xC3 }); // return setenvp

    Patch(SQ + 0xB8ED0, { 0xC3 }); // return _dos_getvect_
    Patch(SQ + 0xB8EA0, { 0xC3 }); // return _dos_setvect_

    Patch(SQ + 0xF1C0B, { 0xC3 }); // return _STK (stack overflow check)
    WatcomHook(SQ + 0x11E840, stackavail);

    WatcomHook(SQ + 0x22CB0, malloc);
    WatcomHook(SQ + 0x22DA0, free);

    WatcomHook(SQ + 0x420E4, exit_hook);
    WatcomHook(SQ + 0x420B0, printf);
    WatcomHook(SQ + 0xACDE0, time);

    Patch(SQ + 0x12A320, { 0x31, 0xC0, 0xC3 }); // make isatty always return 0
    WatcomHook(SQ + 0x9887C, fopen);
    WatcomHook(SQ + 0x98590, ftell);
    WatcomHook(SQ + 0x980C8, fseek);
    WatcomHook(SQ + 0x982C0, fgetc_hook);
    WatcomHook(SQ + 0x98460, ungetc);
    WatcomHook(SQ + 0x97E50, fread);
    WatcomHook(SQ + 0x9829C, fscanf_hook);
    WatcomHook(SQ + 0x64230, fwrite);
    WatcomHook(SQ + 0x9B9A0, fputc_hook);
    WatcomHook(SQ + 0x1890F0, fputs_hook);
    WatcomHook(SQ + 0x4FE10, fprintf_hook);
    WatcomHook(SQ + 0xEAC70, fflush_hook);
    WatcomHook(SQ + 0x448D0, fclose);
}
