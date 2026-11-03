#include "Slate/Core/CrashHandler.h"
#include "Slate/Core/Log.h"

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

namespace Slate {

namespace {

const char* ExceptionName(DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
        default: return "UNKNOWN";
    }
}

LONG WINAPI UnhandledExceptionFilterImpl(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const CONTEXT* context = info->ContextRecord;

    const HMODULE module = GetModuleHandleW(nullptr);
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const uintptr_t address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);

    Log::Critical("Unhandled exception 0x{:08X} ({}) at 0x{:016X} (module+0x{:X})",
        record->ExceptionCode, ExceptionName(record->ExceptionCode), address, address - base);

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
    {
        Log::Critical("Access violation: {} address 0x{:016X}",
            record->ExceptionInformation[0] == 0 ? "read from" : "write to",
            record->ExceptionInformation[1]);
    }

    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (SymInitialize(process, nullptr, TRUE))
    {
        STACKFRAME64 frame {};
        frame.AddrPC.Offset = context->Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context->Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context->Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        Log::Critical("Stack trace:");
        for (int i = 0; i < 40; ++i)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, const_cast<CONTEXT*>(context),
                    nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;

            if (frame.AddrPC.Offset == 0)
                break;

            DWORD64 displacement = 0;
            const char* name = "<unknown>";
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
                name = symbol->Name;

            IMAGEHLP_LINE64 line {};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
                Log::Critical("  #{:02d} 0x{:016X} {} ({}:{})", i, frame.AddrPC.Offset, name, line.FileName, line.LineNumber);
            else
                Log::Critical("  #{:02d} 0x{:016X} {} + 0x{:X}", i, frame.AddrPC.Offset, name, displacement);
        }

        SymCleanup(process);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void InstallCrashHandler()
{
    SetUnhandledExceptionFilter(UnhandledExceptionFilterImpl);
}

} // namespace Slate

#else

namespace Slate {
void InstallCrashHandler() {}
} // namespace Slate

#endif
