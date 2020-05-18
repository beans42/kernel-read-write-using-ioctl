#include "driver.hpp"
#include <TlHelp32.h>

constexpr uintptr_t dwLocalPlayer = 0xD2FB94;
constexpr uintptr_t dwForceJump = 0x51ED760;
constexpr uintptr_t m_fFlags = 0x104;
constexpr uintptr_t FL_ONGROUND = 1 << 0;

uintptr_t GetModule(const char* modName, DWORD procId) {
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
	if (hSnap != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 modEntry;
		modEntry.dwSize = sizeof(modEntry);
		if (Module32First(hSnap, &modEntry)) {
			do {
				if (!strcmp(modEntry.szModule, modName)) {
					CloseHandle(hSnap);
					return (uintptr_t)modEntry.modBaseAddr;
				}
			} while (Module32Next(hSnap, &modEntry));
		}
	}
}

int main() {
	auto hwnd = FindWindowA(NULL, "Counter-Strike: Global Offensive");
	DWORD process_id; GetWindowThreadProcessId(hwnd, &process_id);
	auto client = GetModule("client_panorama.dll", process_id);

	auto driver = new driver_manager("\\\\.\\cartidriver", process_id);

	while (true) {
		auto local_player = driver->RPM<uintptr_t>(client + dwLocalPlayer);
		if (!local_player) continue;
		auto flags = driver->RPM<DWORD>(local_player + m_fFlags);
		if (flags & FL_ONGROUND)
			if (GetAsyncKeyState(VK_SPACE) >> 15)
				driver->WPM<DWORD>(client + dwForceJump, 6);
		Sleep(5);
	}
}
