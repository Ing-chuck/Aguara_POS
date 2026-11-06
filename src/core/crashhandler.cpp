#include "crashhandler.h"

#include <QDir>
#include <QUuid>

#ifdef Q_OS_WIN

#include <windows.h>

#include <csignal>
#include <cstring>

namespace {

const int kMostFrames = 32;
const int kScannedBytes = 64 * 1024;

wchar_t g_path[1024];
char g_version[40];
volatile LONG g_written = 0;
bool g_armed = false;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;

char g_text[12288];     // here, not on the stack: it could be the stack that overflowed
int g_used = 0;

struct Module {
    HMODULE handle;
    char name[120];
    unsigned long stamp;
};
Module g_modules[kMostFrames];
int g_moduleCount = 0;

void put(const char *text)
{
    while (*text && g_used < int(sizeof g_text) - 1)
        g_text[g_used++] = *text++;
    g_text[g_used] = 0;
}

bool readable(const void *address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(address, &info, sizeof info) || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    const char *end = static_cast<const char *>(info.BaseAddress) + info.RegionSize;
    return static_cast<const char *>(address) + size <= end;
}

bool executable(const void *address)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(address, &info, sizeof info) || info.State != MEM_COMMIT)
        return false;
    return info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
}

/// A return address comes right after a call instruction.
bool afterCall(const unsigned char *address)
{
    if (!readable(address - 7, 7))
        return false;
    return address[-5] == 0xE8                                  // call rel32
        || (address[-2] == 0xFF && (address[-1] & 0x38) == 0x10)    // call reg, call [reg]
        || (address[-3] == 0xFF && (address[-2] & 0x38) == 0x10)    // call [reg+8]
        || (address[-6] == 0xFF && (address[-5] & 0x38) == 0x10)    // call [reg+32], call [abs]
        || (address[-7] == 0xFF && (address[-6] & 0x38) == 0x10);   // with an index
}

/// Which build a module is: written by the linker, read from the loaded image.
unsigned long stampOf(HMODULE module)
{
    const char *image = reinterpret_cast<const char *>(module);
    if (!readable(image, sizeof(IMAGE_DOS_HEADER)))
        return 0;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || !readable(image + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS)))
        return 0;
    return reinterpret_cast<const IMAGE_NT_HEADERS *>(image + dos->e_lfanew)->FileHeader.TimeDateStamp;
}

void remember(HMODULE module, const char *name)
{
    for (int i = 0; i < g_moduleCount; ++i) {
        if (g_modules[i].handle == module)
            return;
    }
    if (g_moduleCount == kMostFrames)
        return;
    Module &entry = g_modules[g_moduleCount++];
    entry.handle = module;
    entry.stamp = stampOf(module);
    lstrcpynA(entry.name, name, int(sizeof entry.name));
}

/// "aguara.exe+0x1a2b", with what the server takes: the name of a file, no path.
void addFrame(const void *address, bool first)
{
    char line[200];
    HMODULE module = nullptr;
    wchar_t wide[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &module)
        && GetModuleFileNameW(module, wide, MAX_PATH)) {
        const wchar_t *name = wide;
        for (const wchar_t *c = wide; *c; ++c) {
            if (*c == L'\\' || *c == L'/')
                name = c + 1;
        }
        char narrow[120];
        int length = 0;
        for (; name[length] && length < int(sizeof narrow) - 1; ++length) {
            const wchar_t c = name[length];
            const bool plain = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')
                               || c == L'.' || c == L'_' || c == L'-';
            narrow[length] = plain ? char(c) : '_';
        }
        narrow[length] = 0;
        remember(module, narrow);
        wsprintfA(line, "%s\"%s+0x%lx\"", first ? "" : ",", narrow,
                  static_cast<unsigned long>(static_cast<const char *>(address) - reinterpret_cast<const char *>(module)));
    } else {
        wsprintfA(line, "%s\"0x%lx\"", first ? "" : ",", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(address)));
    }
    put(line);
}

void write(const char *code, const CONTEXT *context)
{
    if (!g_armed || InterlockedExchange(&g_written, 1))
        return;                     // one report: a crash inside this mustn't start over

    SYSTEMTIME now;
    GetSystemTime(&now);
    char head[160];
    wsprintfA(head, "{\"when\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\",\"version\":\"%s\",\"code\":\"%s\",\"frames\":[",
              now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, g_version, code);
    put(head);

#if defined(_M_X64) || defined(__x86_64__)
    const ULONG_PTR ip = context->Rip, sp = context->Rsp;
#else
    const ULONG_PTR ip = context->Eip, sp = context->Esp;
#endif
    int frames = 0;
    if (executable(reinterpret_cast<const void *>(ip))) {
        addFrame(reinterpret_cast<const void *>(ip), true);
        ++frames;
    }
    const ULONG_PTR *slot = reinterpret_cast<const ULONG_PTR *>(sp & ~(sizeof(ULONG_PTR) - 1));
    const ULONG_PTR *const end = slot + kScannedBytes / sizeof(ULONG_PTR);
    for (; slot < end && frames < kMostFrames; ++slot) {
        if ((reinterpret_cast<ULONG_PTR>(slot) & 0xFFF) == 0 || slot == reinterpret_cast<const ULONG_PTR *>(sp)) {
            if (!readable(slot, sizeof(ULONG_PTR)))
                break;
        }
        const void *candidate = reinterpret_cast<const void *>(*slot);
        if (executable(candidate) && afterCall(static_cast<const unsigned char *>(candidate))) {
            addFrame(candidate, frames == 0);
            ++frames;
        }
    }
    put("],\"modules\":{");
    for (int i = 0; i < g_moduleCount; ++i) {
        char line[160];
        wsprintfA(line, "%s\"%s\":\"%08lx\"", i ? "," : "", g_modules[i].name, g_modules[i].stamp);
        put(line);
    }
    put("}}");

    HANDLE file = CreateFileW(g_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, g_text, DWORD(g_used), &written, nullptr);
        CloseHandle(file);
    }
}

LONG WINAPI onException(EXCEPTION_POINTERS *exception)
{
    char code[16];
    wsprintfA(code, "0x%08lx", static_cast<unsigned long>(exception->ExceptionRecord->ExceptionCode));
    write(code, exception->ContextRecord);
    return g_previous ? g_previous(exception) : EXCEPTION_CONTINUE_SEARCH;
}

/// abort(): a qFatal, an uncaught exception, a failed check of the runtime.
void onAbort(int)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    write("abort", &context);
    signal(SIGABRT, SIG_DFL);
}

} // namespace

namespace CrashHandler {

void install(const QString &directory, const QString &version)
{
    const QString path = QDir::toNativeSeparators(QDir(directory).filePath(
        QStringLiteral("crash-%1.json").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).toLower())));
    const int length = path.toWCharArray(g_path);
    g_path[length] = 0;
    const QByteArray text = version.toLatin1().left(int(sizeof g_version) - 1);
    memcpy(g_version, text.constData(), size_t(text.size()) + 1);
    if (!g_armed) {
        g_previous = SetUnhandledExceptionFilter(onException);
        signal(SIGABRT, onAbort);
        g_armed = true;
    }
}

void fatalMessage()
{
    CONTEXT context;
    RtlCaptureContext(&context);
    write("qfatal", &context);
}

} // namespace CrashHandler

#else

namespace CrashHandler {

void install(const QString &, const QString &) {}
void fatalMessage() {}

} // namespace CrashHandler

#endif
