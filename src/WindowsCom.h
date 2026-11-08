#ifndef WINDOWSCOM_H
#define WINDOWSCOM_H

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>

// A successful CoInitializeEx call owns one reference, including S_FALSE.
// Keep this guard on its creating thread and destroy COM interfaces before it.
class WindowsComApartment {
	HRESULT result_;
public:
	explicit WindowsComApartment(DWORD flags = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)
		: result_(CoInitializeEx(nullptr, flags))
	{
	}
	~WindowsComApartment()
	{
		if (SUCCEEDED(result_)) CoUninitialize();
	}
	WindowsComApartment(WindowsComApartment const &) = delete;
	WindowsComApartment &operator=(WindowsComApartment const &) = delete;
	bool initialized() const { return SUCCEEDED(result_); }
	HRESULT result() const { return result_; }
	static bool pumpMessages()
	{
		MSG message;
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) {
				PostQuitMessage(static_cast<int>(message.wParam));
				return false;
			}
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		return true;
	}
};
#endif

#endif // WINDOWSCOM_H
