#pragma once

#include <stdint.h>
#include <iterator>
#include <utility>

void Nop(uintptr_t address, size_t count);
void Patch(uintptr_t address, std::initializer_list<uint8_t> list);

void PutPushImm(uintptr_t address, uintptr_t value);

void Hook(uintptr_t installAddress, uint8_t op, uintptr_t hookAddress);
void Hook(uintptr_t installAddress, uint8_t op, void* hookAddress);

uintptr_t MethodHook(uintptr_t installAddress, uintptr_t hookAddress);
uintptr_t MethodHook(uintptr_t installAddress, void* hookAddress);

uintptr_t WatcomCall(uintptr_t address, uintptr_t a1);
uintptr_t WatcomCall(uintptr_t address, uintptr_t a1, uintptr_t a2);
uintptr_t WatcomCall(uintptr_t address, uintptr_t a1, uintptr_t a2, uintptr_t a3);
uintptr_t WatcomCall(uintptr_t address, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

template<typename Ret>
Ret WatcomCall(uintptr_t address)
{
	return (*(Ret (*)())address)();
}

template<typename Ret, typename... Args>
Ret WatcomCall(uintptr_t address, Args&&... args)
{
	return (Ret)WatcomCall(address, (uintptr_t)args...);
}

extern const uintptr_t WatcomStdcallHookThunks[5];
extern const uintptr_t WatcomCdeclHookThunks[5];

template<typename Ret, typename... Args>
void WatcomHook(uintptr_t address, Ret (__stdcall *hook)(Args...))
{
	static_assert(sizeof...(Args) <= sizeof(WatcomStdcallHookThunks) / sizeof(WatcomStdcallHookThunks[0]));

	PutPushImm(address, reinterpret_cast<uintptr_t>(hook));
	Hook(address + 5, 0xE9, WatcomStdcallHookThunks[sizeof...(Args)]);
}

template<typename Ret, typename... Args>
void WatcomHook(uintptr_t address, Ret (__cdecl *hook)(Args...))
{
	static_assert(sizeof...(Args) <= sizeof(WatcomCdeclHookThunks) / sizeof(WatcomCdeclHookThunks[0]));

	PutPushImm(address, reinterpret_cast<uintptr_t>(hook));
	Hook(address + 5, 0xE9, WatcomCdeclHookThunks[sizeof...(Args)]);
}

void VarArgHookThunk();

template<typename Ret, typename... Args>
void WatcomHook(uintptr_t address, Ret (*hook)(Args..., ...))
{
	PutPushImm(address, reinterpret_cast<uintptr_t>(hook));
	Hook(address + 5, 0xE9, reinterpret_cast<uintptr_t>(VarArgHookThunk));
}
