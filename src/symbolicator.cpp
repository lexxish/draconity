//
// Created by lexxish on 7/8/2019.
//

#include "symbolicator.h"

class Symbolicator {
public:
    void loadSymbols(const std::string& libraryName, const std::list<SymbolLoad>& symbols) {
        HMODULE hDLL = LoadLibrary(libraryName.c_str());
        if (hDLL == nullptr) {
            draconity_logf("ERROR: Could not load DLL %s. %s", libraryName.c_str(), getLastErrorAsString().c_str());
        }
        for (auto sym : symbols) {
            FARPROC procAddress = GetProcAddress(hDLL, sym.getName().c_str());
            if (procAddress == nullptr) {
                draconity_logf(
                        "WARN: Could not find method %s in library %s. %s",
                        sym.getName().c_str(), libraryName.c_str(), getLastErrorAsString().c_str()
                );
            }
            sym.setPtr(reinterpret_cast<void **>(&procAddress));
        }
    }

private:
    std::string getLastErrorAsString() {
        DWORD errorMessageID = ::GetLastError();
        if (errorMessageID == 0) {
            // No error message
            return std::string();
        }

        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR) &messageBuffer, 0, NULL
        );

        std::string message(messageBuffer, size);

        LocalFree(messageBuffer);
        return message;
    }
};
