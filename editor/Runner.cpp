#include "Runner.hpp"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <SDL_mutex.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
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
    ig::String programPathCopy;
    ct::Vector<ig::String> argumentsCopy;
    ig::String workingDirectoryCopy;
#if defined(_WIN32)
    void* processHandle = nullptr;
    void* stdinSlot = nullptr;
    void* stdinReadSlot = nullptr;
#else
    void* pidSlot = nullptr;
    // stdinSlot is the write end Runner::writeInput sends lines to,
    // stdinReadSlot the end the child reads; both are made by runAsync before
    // the worker starts (see createStdinPipe), so a caller that answers a
    // prompt right after runAsync returns never races the worker.
    void* stdinSlot = nullptr;
    void* stdinReadSlot = nullptr;
#endif

    RunHandle(const ig::String& programPath, const ct::Vector<ig::String>& arguments,
              const ig::String& workingDirectory)
        : programPathCopy(programPath), argumentsCopy(arguments), workingDirectoryCopy(workingDirectory)
    {
        outputMutex = SDL_CreateMutex();
        SDL_AtomicSet(&done, 0);
        SDL_AtomicSet(&stopRequested, 0);
    }

    ~RunHandle()
    {
        if (outputMutex)
            SDL_DestroyMutex(outputMutex);
    }
};

Runner::Runner(ig::String runnerPath) : runnerPath_(runnerPath) {}

// The child's stdin pipe, created up front by runAsync so that
// Runner::writeInput has somewhere to write the moment runAsync returns.
// A caller answering a prompt - the Output panel's console row, or the test
// that writes a line straight after starting a run - must not have to wait
// for the worker thread to get around to creating it.
static bool createStdinPipe(Runner::RunHandle* handle)
{
    if (!handle) return false;
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return false;
    SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);
    SDL_AtomicSetPtr(&handle->stdinReadSlot, readEnd);
    SDL_AtomicSetPtr(&handle->stdinSlot, writeEnd);
    return true;
#else
    int fds[2];
    if (pipe(fds) != 0) return false;
    SDL_AtomicSetPtr(&handle->stdinReadSlot, (void*)(intptr_t)fds[0]);
    SDL_AtomicSetPtr(&handle->stdinSlot, (void*)(intptr_t)fds[1]);
    return true;
#endif
}

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

static ig::String quoteWindowsArgument(const ig::String& argument)
{
    ig::String quoted("\"");
    for (char c : argument)
    {
        if (c == '\"') quoted += '\\';
        quoted += c;
    }
    quoted += '\"';
    return quoted;
}

static RunResult runWindows(const ig::String& programPath, const ct::Vector<ig::String>& arguments,
                            const ig::String& workingDirectory, Runner::RunHandle* handle)
{
    RunResult result;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
    {
        result.output = ig::String("zenblitz-editor: could not create a pipe to ") + programPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    // Same as the POSIX path: the child's stdin is the pipe runAsync made,
    // not the editor's own input stream (see Runner.cpp's runPosix comment).
    void* stdinRead = handle ? SDL_AtomicGetPtr(&handle->stdinReadSlot) : nullptr;
    if (handle)
        SDL_AtomicSetPtr(&handle->stdinReadSlot, nullptr);

    ig::String commandLine = quoteWindowsArgument(programPath);
    for (const ig::String& argument : arguments)
    {
        commandLine += " ";
        commandLine += quoteWindowsArgument(argument);
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = stdinRead ? (HANDLE)stdinRead : GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    char* mutableCommandLine = new char[commandLine.size() + 1];
    memcpy(mutableCommandLine, commandLine.c_str(), commandLine.size() + 1);

    const BOOL started = CreateProcessA(nullptr, mutableCommandLine, nullptr, nullptr, TRUE, 0,
                                        nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &si, &pi);
    delete[] mutableCommandLine;
    CloseHandle(writeEnd);
    if (stdinRead)
        CloseHandle((HANDLE)stdinRead);

    if (!started)
    {
        CloseHandle(readEnd);
        result.output = ig::String("zenblitz-editor: could not launch ") + programPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }

    if (handle)
    {
        SDL_AtomicSetPtr(&handle->processHandle, pi.hProcess);
        if (SDL_AtomicGet(&handle->stopRequested))
            TerminateProcess(pi.hProcess, 1);
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
        void* slot = SDL_AtomicGetPtr(&handle->stdinSlot);
        SDL_AtomicSetPtr(&handle->stdinSlot, nullptr);
        if (slot)
            CloseHandle((HANDLE)slot);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}
#else

static RunResult runPosix(const ig::String& programPath, const ct::Vector<ig::String>& arguments,
                          const ig::String& workingDirectory, Runner::RunHandle* handle)
{
    RunResult result;

    int pipeFds[2];
    if (pipe(pipeFds) != 0)
    {
        result.output = ig::String("zenblitz-editor: could not create a pipe to ") + programPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }

    // The child's stdin comes from the pipe runAsync made - the editor is a
    // GUI with no terminal to hand over, and a Blitz3D program that asks a
    // question (the samples all do it through start.bb's SetGfx) would
    // otherwise either block forever on a stream nobody can type into or, at
    // end of file, spin on Input returning "" and flood its own output (a
    // retry loop like "Repeat: Input ... : Until mode>=1" never satisfies
    // itself). Runner::writeInput answers it from the Output panel's console
    // row. A synchronous run() has no handle and so no pipe: it keeps the
    // inherited stdin, which is what it always did.
    void* stdinRead = handle ? SDL_AtomicGetPtr(&handle->stdinReadSlot) : nullptr;
    if (handle)
        SDL_AtomicSetPtr(&handle->stdinReadSlot, nullptr);

    const pid_t child = fork();
    if (child < 0)
    {
        close(pipeFds[0]);
        close(pipeFds[1]);
        if (stdinRead)
            close((int)(intptr_t)stdinRead);
        result.output = ig::String("zenblitz-editor: could not launch ") + programPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }

    if (child == 0)
    {
        if (stdinRead)
            dup2((int)(intptr_t)stdinRead, STDIN_FILENO);
        dup2(pipeFds[1], STDOUT_FILENO);
        dup2(pipeFds[1], STDERR_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        if (stdinRead)
            close((int)(intptr_t)stdinRead);
        if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
            _exit(126);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(const_cast<char*>(programPath.c_str()));
        for (const ig::String& argument : arguments)
            argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execvp(programPath.c_str(), argv.data());
        _exit(127);
    }

    close(pipeFds[1]);
    if (stdinRead)
        close((int)(intptr_t)stdinRead);
    if (handle)
    {
        SDL_AtomicSetPtr(&handle->pidSlot, (void*)(intptr_t)child);
        if (SDL_AtomicGet(&handle->stopRequested))
            kill(child, SIGKILL);
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
        // Nothing can read the child's stdin any more: give the descriptor
        // back, so a later writeInput finds no slot instead of a stale fd.
        void* slot = SDL_AtomicGetPtr(&handle->stdinSlot);
        SDL_AtomicSetPtr(&handle->stdinSlot, nullptr);
        if (slot)
            close((int)(intptr_t)slot);
    }
    return result;
}

#endif

bool Runner::writeInput(RunHandle* handle, const ig::String& text) const
{
    if (!handle) return false;
    // A child that has already exited has nobody left to read this.
    if (SDL_AtomicGet(&handle->done) != 0) return false;
#if defined(_WIN32)
    void* slot = SDL_AtomicGetPtr(&handle->stdinSlot);
    if (!slot) return false;
    ig::String line = text;
    line += "\n";
    DWORD written = 0;
    return WriteFile((HANDLE)slot, line.data(), (DWORD)line.size(), &written, nullptr) != 0;
#else
    // Writing to a pipe whose reader is gone raises SIGPIPE, which by
    // default kills the process - that is the editor.
    static const bool sigpipeIgnored = [] { signal(SIGPIPE, SIG_IGN); return true; }();
    (void)sigpipeIgnored;
    void* slot = SDL_AtomicGetPtr(&handle->stdinSlot);
    if (!slot) return false;
    const int fd = (int)(intptr_t)slot;
    ig::String line = text;
    line += "\n";
    size_t sent = 0;
    while (sent < line.size())
    {
        const ssize_t n = write(fd, line.data() + sent, line.size() - sent);
        if (n > 0)
        {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false; // EPIPE: the program exited before reading it
    }
    return true;
#endif
}

RunResult Runner::run(const ig::String& scriptPath) const
{
    ct::Vector<ig::String> arguments;
    arguments.push_back(scriptPath);
    return run(runnerPath_, arguments);
}

RunResult Runner::run(const ig::String& programPath, const ct::Vector<ig::String>& arguments,
                      const ig::String& workingDirectory) const
{
#if defined(_WIN32)
    return runWindows(programPath, arguments, workingDirectory, nullptr);
#else
    return runPosix(programPath, arguments, workingDirectory, nullptr);
#endif
}

namespace
{
int runThreadEntry(void* data)
{
    Runner::RunHandle* handle = static_cast<Runner::RunHandle*>(data);
#if defined(_WIN32)
    handle->result = runWindows(handle->programPathCopy, handle->argumentsCopy, handle->workingDirectoryCopy, handle);
#else
    handle->result = runPosix(handle->programPathCopy, handle->argumentsCopy, handle->workingDirectoryCopy, handle);
#endif
    SDL_AtomicSet(&handle->done, 1);
    return 0;
}
} // namespace

Runner::RunHandle* Runner::runAsync(const ig::String& scriptPath) const
{
    ct::Vector<ig::String> arguments;
    arguments.push_back(scriptPath);
    return runAsync(runnerPath_, arguments);
}

Runner::RunHandle* Runner::runAsync(const ig::String& programPath, const ct::Vector<ig::String>& arguments,
                                    const ig::String& workingDirectory) const
{
    RunHandle* handle = new RunHandle(programPath, arguments, workingDirectory);
    if (!handle->outputMutex)
    {
        handle->result.output = ig::String("zenblitz-editor: could not create output lock: ") +
                                ig::String(SDL_GetError());
        handle->result.exitCode = -1;
        handle->result.ranAtAll = false;
        SDL_AtomicSet(&handle->done, 1);
        return handle;
    }
    createStdinPipe(handle);
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

} // namespace zed
