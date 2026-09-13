#include "Runner.hpp"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <cerrno>
#include <SDL_mutex.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace zed
{

struct Runner::RunHandle
{
    SDL_Thread* thread = nullptr;
    SDL_mutex* outputMutex = nullptr;
    SDL_atomic_t done;
    SDL_atomic_t stopRequested;
    RunResult result;
    ig::String pendingOutput;
    ig::String runnerPathCopy;
    ig::String scriptPathCopy;
#if defined(_WIN32)
    void* processHandle = nullptr;
    /* Write end of the child's stdin, null until the worker publishes it
       and again once the child exits. Written by the worker thread, read
       by the UI thread in sendInput, so it goes through the same atomic
       pointer slot processHandle already uses. */
    void* stdinHandle = nullptr;
#else
    void* pidSlot = nullptr;
    /* Write end of the child's stdin, +1 so that 0 means "not open yet"
       (fd 0 is a legitimate descriptor). The worker thread sets it after
       fork; the UI thread reads it in sendInput, so it goes through the
       same atomic slot pidSlot already uses. */
    SDL_atomic_t stdinFdPlusOne;
#endif

    RunHandle(const Runner& r, const ig::String& scriptPath)
        : runnerPathCopy(r.runnerPath_), scriptPathCopy(scriptPath)
    {
        outputMutex = SDL_CreateMutex();
        SDL_AtomicSet(&done, 0);
        SDL_AtomicSet(&stopRequested, 0);
#if !defined(_WIN32)
        SDL_AtomicSet(&stdinFdPlusOne, 0);
#endif
    }

    ~RunHandle()
    {
        if (outputMutex)
            SDL_DestroyMutex(outputMutex);
    }
};

Runner::Runner(ig::String runnerPath) : runnerPath_(runnerPath) {}

static void captureOutput(RunResult& result, Runner::RunHandle* handle,
                          const char* bytes, size_t size)
{
    if (!handle)
    {
        result.output.append(bytes, size);
        return;
    }

    SDL_LockMutex(handle->outputMutex);
    handle->pendingOutput.append(bytes, size);
    SDL_UnlockMutex(handle->outputMutex);
}

#if defined(_WIN32)

static RunResult runWindows(const ig::String& runnerPath, const ig::String& scriptPath, Runner::RunHandle* handle)
{
    RunResult result;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
    {
        result.output = ig::String("zenblitz-editor: could not create a pipe to ") + runnerPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }
    // The child must not inherit our read end, or the pipe never reports
    // EOF: a copy of the handle would stay open in the child and ReadFile
    // here would block forever after the program exited.
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    /* Second pipe, the other way, so Input$ can be answered from the
       Output panel - same reason as the POSIX path's stdinFds. Without it
       the child inherits the editor's own stdin, which is attached to
       nothing the user can type into, and a script that asks a question
       hangs. Our write end stays non-inheritable for the mirror-image
       reason: the child holding a copy would keep its own stdin open and
       it would never see EOF. */
    HANDLE stdinRead = nullptr, stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0))
    {
        CloseHandle(readEnd);
        CloseHandle(writeEnd);
        result.output = ig::String("zenblitz-editor: could not create a pipe to ") + runnerPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    ig::String commandLine = "\"";
    commandLine += runnerPath;
    commandLine += "\" \"";
    commandLine += scriptPath;
    commandLine += "\"";

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = stdinRead;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    char* mutableCommandLine = new char[commandLine.size() + 1];
    memcpy(mutableCommandLine, commandLine.c_str(), commandLine.size() + 1);

    const BOOL started = CreateProcessA(nullptr, mutableCommandLine, nullptr, nullptr, TRUE, 0,
                                        nullptr, nullptr, &si, &pi);
    delete[] mutableCommandLine;
    // Both child-side ends are closed here: the child has its own copies
    // now, and holding ours open would stop either pipe from ever
    // reaching EOF.
    CloseHandle(writeEnd);
    CloseHandle(stdinRead);

    if (!started)
    {
        CloseHandle(readEnd);
        CloseHandle(stdinWrite);
        result.output = ig::String("zenblitz-editor: could not launch ") + runnerPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }

    if (handle)
    {
        SDL_AtomicSetPtr(&handle->processHandle, pi.hProcess);
        SDL_AtomicSetPtr(&handle->stdinHandle, stdinWrite);
        if (SDL_AtomicGet(&handle->stopRequested))
            TerminateProcess(pi.hProcess, 1);
    }
    else
    {
        CloseHandle(stdinWrite);
    }

    char buf[4096];
    DWORD n = 0;
    while (ReadFile(readEnd, buf, sizeof(buf), &n, nullptr) && n > 0)
        captureOutput(result, handle, buf, n);
    CloseHandle(readEnd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = (int)exitCode;
    result.ranAtAll = true;

    if (handle)
    {
        SDL_AtomicSetPtr(&handle->processHandle, nullptr);
        // Take the handle before closing it, so a sendInput racing with
        // exit sees null and writes nothing rather than writing to a
        // closed (or reused) handle - same swap the POSIX path makes.
        HANDLE stdinOut = (HANDLE)SDL_AtomicSetPtr(&handle->stdinHandle, nullptr);
        if (stdinOut) CloseHandle(stdinOut);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

#else

static RunResult runPosix(const ig::String& runnerPath, const ig::String& scriptPath, Runner::RunHandle* handle)
{
    RunResult result;

    int pipeFds[2];
    if (pipe(pipeFds) != 0)
    {
        result.output = ig::String("zenblitz-editor: could not create a pipe to ") + runnerPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }

    /* Second pipe, the other way: the child reads its stdin from this so
       Input$ can be answered from the Output panel. Without it the child
       inherits the editor's own stdin, which is attached to nothing the
       user can type into - Input$ then blocks forever on a script that
       asks a question (every sample using start.bb's mode menu). */
    int stdinFds[2];
    if (pipe(stdinFds) != 0)
    {
        close(pipeFds[0]);
        close(pipeFds[1]);
        result.output = ig::String("zenblitz-editor: could not create a pipe to ") + runnerPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }

    const pid_t child = fork();
    if (child < 0)
    {
        close(pipeFds[0]);
        close(pipeFds[1]);
        close(stdinFds[0]);
        close(stdinFds[1]);
        result.output = ig::String("zenblitz-editor: could not launch ") + runnerPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }

    if (child == 0)
    {
        dup2(pipeFds[1], STDOUT_FILENO);
        dup2(pipeFds[1], STDERR_FILENO);
        dup2(stdinFds[0], STDIN_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        close(stdinFds[0]);
        close(stdinFds[1]);
        execlp(runnerPath.c_str(), runnerPath.c_str(), scriptPath.c_str(), (char*)nullptr);
        _exit(127);
    }

    close(pipeFds[1]);
    close(stdinFds[0]);
    if (handle)
    {
        SDL_AtomicSetPtr(&handle->pidSlot, (void*)(intptr_t)child);
        SDL_AtomicSet(&handle->stdinFdPlusOne, stdinFds[1] + 1);
        if (SDL_AtomicGet(&handle->stopRequested))
            kill(child, SIGKILL);
    }
    else
    {
        close(stdinFds[1]);
    }

    char buf[4096];
    ssize_t n;
    while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0)
        captureOutput(result, handle, buf, (size_t)n);
    close(pipeFds[0]);

    int status = 0;
    waitpid(child, &status, 0);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.ranAtAll = true;

    if (handle)
    {
        SDL_AtomicSetPtr(&handle->pidSlot, nullptr);
        // Take the fd before closing so a sendInput racing with exit sees
        // 0 and writes nothing, rather than writing to a closed (or
        // worse, reused) descriptor.
        const int fd = SDL_AtomicSet(&handle->stdinFdPlusOne, 0);
        if (fd > 0) close(fd - 1);
    }
    return result;
}

#endif

RunResult Runner::run(const ig::String& scriptPath) const
{
#if defined(_WIN32)
    return runWindows(runnerPath_, scriptPath, nullptr);
#else
    return runPosix(runnerPath_, scriptPath, nullptr);
#endif
}

namespace
{
int runThreadEntry(void* data)
{
    Runner::RunHandle* handle = static_cast<Runner::RunHandle*>(data);
#if defined(_WIN32)
    handle->result = runWindows(handle->runnerPathCopy, handle->scriptPathCopy, handle);
#else
    handle->result = runPosix(handle->runnerPathCopy, handle->scriptPathCopy, handle);
#endif
    SDL_AtomicSet(&handle->done, 1);
    return 0;
}
} // namespace

Runner::RunHandle* Runner::runAsync(const ig::String& scriptPath) const
{
    RunHandle* handle = new RunHandle(*this, scriptPath);
    if (!handle->outputMutex)
    {
        handle->result.output = ig::String("zenblitz-editor: could not create output lock: ") +
                                ig::String(SDL_GetError());
        handle->result.exitCode = -1;
        handle->result.ranAtAll = false;
        SDL_AtomicSet(&handle->done, 1);
        return handle;
    }
    handle->thread = SDL_CreateThread(runThreadEntry, "zenblitz-play", handle);
    if (!handle->thread)
    {
        handle->result.output = ig::String("zenblitz-editor: could not start run thread: ") +
                                 ig::String(SDL_GetError());
        handle->result.exitCode = -1;
        handle->result.ranAtAll = false;
        SDL_AtomicSet(&handle->done, 1);
    }
    return handle;
}

bool Runner::poll(RunHandle* handle, RunResult& out) const
{
    if (!handle) return false;

    out = RunResult();
    SDL_LockMutex(handle->outputMutex);
    out.output.swap(handle->pendingOutput);
    SDL_UnlockMutex(handle->outputMutex);

    if (SDL_AtomicGet(&handle->done) == 0)
        return false;

    if (handle->thread)
        SDL_WaitThread(handle->thread, nullptr);

    // The worker sets done only after its final pipe read, but that final
    // chunk may have arrived between the drain above and the done check.
    SDL_LockMutex(handle->outputMutex);
    out.output.append(handle->pendingOutput);
    handle->pendingOutput.clear();
    SDL_UnlockMutex(handle->outputMutex);
    out.output.append(handle->result.output);
    out.exitCode = handle->result.exitCode;
    out.ranAtAll = handle->result.ranAtAll;
    delete handle;
    return true;
}

void Runner::stop(RunHandle* handle) const
{
    if (!handle) return;
    // Remember an early Stop pressed before the worker has published the
    // process id/handle. The worker checks this immediately after launch.
    SDL_AtomicSet(&handle->stopRequested, 1);
#if defined(_WIN32)
    HANDLE process = (HANDLE)SDL_AtomicGetPtr(&handle->processHandle);
    if (process)
        TerminateProcess(process, 1);
#else
    pid_t pid = (pid_t)(intptr_t)SDL_AtomicGetPtr(&handle->pidSlot);
    if (pid > 0)
        kill(pid, SIGKILL);
#endif
}

bool Runner::sendInput(RunHandle* handle, const ig::String& line) const
{
    if (!handle) return false;
#if defined(_WIN32)
    HANDLE pipe = (HANDLE)SDL_AtomicGetPtr(&handle->stdinHandle);
    if (!pipe) return false;

    // CRT stdio on Windows expects CRLF from a console; a bare "\n" is
    // what fgets strips anyway, and bb's own sb_input drops a trailing
    // '\r' explicitly, so either ending works. "\r\n" is sent because
    // that is what a program reading this pipe as a text stream expects.
    ig::String payload = line;
    payload += "\r\n";
    const char* data = payload.c_str();
    DWORD remaining = (DWORD)payload.size();
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(pipe, data, remaining, &written, nullptr) || written == 0)
            return false;
        data += written;
        remaining -= written;
    }
    return true;
#else
    const int fd = SDL_AtomicGet(&handle->stdinFdPlusOne) - 1;
    if (fd < 0) return false;

    ig::String payload = line;
    payload += "\n";
    const char* data = payload.c_str();
    size_t remaining = payload.size();
    while (remaining > 0)
    {
        // A partial write is normal on a pipe once the buffer fills, and
        // EINTR just means a signal landed mid-call - neither is failure.
        const ssize_t written = write(fd, data, remaining);
        if (written > 0)
        {
            data += written;
            remaining -= (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
#endif
}

} // namespace zed
