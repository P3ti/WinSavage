#include "patchutil.h"
#include <windows.h>

void Nop(uintptr_t address, size_t count)
{
	memset((void*)address, 0x90, count);
}

void Patch(uintptr_t address, std::initializer_list<uint8_t> list)
{
	std::copy(list.begin(), list.end(), stdext::make_checked_array_iterator((uint8_t*)address, list.size()));
}

void PutPushImm(uintptr_t address, uintptr_t value)
{
	*(uint8_t*)address = 0x68;
	*(uintptr_t*)(address + 1) = value;
}

void Hook(uintptr_t installAddress, uint8_t op, uintptr_t hookAddress)
{
	*(uint8_t*)installAddress = op;
	*(uintptr_t*)(installAddress + 1) = hookAddress - (installAddress + 5);
}

void Hook(uintptr_t installAddress, uint8_t op, void* hookAddress)
{
	Hook(installAddress, op, (uintptr_t)hookAddress);
}

uintptr_t MethodHook(uintptr_t installAddress, uintptr_t hookAddress)
{
	uintptr_t orig = *(uintptr_t*)installAddress;
	*(uintptr_t*)installAddress = hookAddress;
	return orig;
}

uintptr_t MethodHook(uintptr_t installAddress, void* hookAddress)
{
	return MethodHook(installAddress, (uintptr_t)hookAddress);
}

uintptr_t WatcomCall(uintptr_t address, uintptr_t a1)
{
	uintptr_t result;
	_asm
	{
		mov eax, a1
		call address
		mov result, eax
	}
	return result;
}

uintptr_t WatcomCall(uintptr_t address, uintptr_t a1, uintptr_t a2)
{
	uintptr_t result;
	_asm
	{
		mov edx, a2
		mov eax, a1
		call address
		mov result, eax
	}
	return result;
}

uintptr_t WatcomCall(uintptr_t address, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	uintptr_t result;
	_asm
	{
		mov ebx, a3
		mov edx, a2
		mov eax, a1
		call address
		mov result, eax
	}
	return result;
}

uintptr_t WatcomCall(uintptr_t address, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	uintptr_t result;
	_asm
	{
		mov ecx, a4
		mov ebx, a3
		mov edx, a2
		mov eax, a1
		call address
		mov result, eax
	}
	return result;
}

static void __declspec(naked) WatcomHookThunk0()
{
	_asm
	{
		push ecx
		push edx
		push ebx
		push ebp
		push esi
		push edi
		call [esp+24]
		pop edi
		pop esi
		pop ebp
		pop ebx
		pop edx
		pop ecx
		add esp, 4
		ret
	}
}

static void __declspec(naked) WatcomStdcallHookThunk1()
{
	_asm
	{
		push ecx
		push edx
		push ebx
		push ebp
		push esi
		push edi
		push eax
		call [esp+28]
		pop edi
		pop esi
		pop ebp
		pop ebx
		pop edx
		pop ecx
		add esp, 4
		ret
	}
}

static void __declspec(naked) WatcomCdeclHookThunk1()
{
	_asm
	{
		push ecx
		push edx
		push ebx
		push ebp
		push esi
		push edi
		push eax
		call [esp+28]
		add esp, 4
		pop edi
		pop esi
		pop ebp
		pop ebx
		pop edx
		pop ecx
		add esp, 4
		ret
	}
}

static void __declspec(naked) WatcomStdcallHookThunk2()
{
	_asm
	{
		push ecx
		push ebx
		push ebp
		push esi
		push edi
		push edx
		push eax
		call [esp+28]
		pop edi
		pop esi
		pop ebp
		pop ebx
		pop ecx
		add esp, 4
		ret
	}
}

static void __declspec(naked) WatcomCdeclHookThunk2()
{
	_asm
	{
		push ecx
		push ebx
		push ebp
		push esi
		push edi
		push edx
		push eax
		call [esp+28]
		add esp, 8
		pop edi
		pop esi
		pop ebp
		pop ebx
		pop ecx
		add esp, 4
		ret
	}
}

static void __declspec(naked) WatcomStdcallHookThunk3()
{
	_asm
	{
		push ecx
		push ebp
		push esi
		push edi
		push ebx
		push edx
		push eax
		call [esp+28]
		pop edi
		pop esi
		pop ebp
		pop ecx
		add esp, 4
		ret
	}
}

static void __declspec(naked) WatcomCdeclHookThunk3()
{
	_asm
	{
		push ecx
		push ebp
		push esi
		push edi
		push ebx
		push edx
		push eax
		call [esp+28]
		add esp, 12
		pop edi
		pop esi
		pop ebp
		pop ecx
		add esp, 4
		ret
	}
}

static void __declspec(naked) WatcomStdcallHookThunk4()
{
	_asm
	{
		push ebp
		push esi
		push edi
		push ecx
		push ebx
		push edx
		push eax
		call [esp+28]
		pop edi
		pop esi
		pop ebp
		add esp, 4
		ret
	}
}

static void __declspec(naked) WatcomCdeclHookThunk4()
{
	_asm
	{
		push ebp
		push esi
		push edi
		push ecx
		push ebx
		push edx
		push eax
		call [esp+28]
		add esp, 16
		pop edi
		pop esi
		pop ebp
		add esp, 4
		ret
	}
}

const uintptr_t WatcomStdcallHookThunks[5] =
{
	(uintptr_t)WatcomHookThunk0,
	(uintptr_t)WatcomStdcallHookThunk1,
	(uintptr_t)WatcomStdcallHookThunk2,
	(uintptr_t)WatcomStdcallHookThunk3,
	(uintptr_t)WatcomStdcallHookThunk4
};

const uintptr_t WatcomCdeclHookThunks[5] =
{
	(uintptr_t)WatcomHookThunk0,
	(uintptr_t)WatcomCdeclHookThunk1,
	(uintptr_t)WatcomCdeclHookThunk2,
	(uintptr_t)WatcomCdeclHookThunk3,
	(uintptr_t)WatcomCdeclHookThunk4
};

void __declspec(naked) VarArgHookThunk()
{
	static struct
	{
		uintptr_t Ret;
		uintptr_t Ecx;
		uintptr_t Edx;
		uintptr_t Ebx;
		uintptr_t Ebp;
		uintptr_t Esi;
		uintptr_t Edi;
	} Context;

	_asm
	{
		mov Context.Ecx, ecx
		mov Context.Edx, edx
		mov Context.Ebx, ebx
		mov Context.Ebp, ebp
		mov Context.Esi, esi
		mov Context.Edi, edi
		pop eax
		pop Context.Ret
		call eax
		mov edi, Context.Edi
		mov esi, Context.Esi
		mov ebp, Context.Ebp
		mov ebx, Context.Ebx
		mov edx, Context.Edx
		mov ecx, Context.Ecx
		jmp Context.Ret
	}
}
