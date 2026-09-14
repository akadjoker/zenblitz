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
#else
    void* pidSlot = nullptr;
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
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    char* mutableCommandLine = new char[commandLine.size() + 1];
    memcpy(mutableCommandLine, commandLine.c_str(), commandLine.size() + 1);

    const BOOL started = CreateProcessA(nullptr, mutableCommandLine, nullptr, nullptr, TRUE, 0,
                                        nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &si, &pi);
    delete[] mutableCommandLine;
    CloseHandle(writeEnd);

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
        SDL_AtomicSetPtr(&handle->processHandle, nullptr);
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

    const pid_t child = fork();
    if (child < 0)
    {
        close(pipeFds[0]);
        close(pipeFds[1]);
        result.output = ig::String("zenblitz-editor: could not launch ") + programPath;
        result.exitCode = -1;
        result.ranAtAll = false;
        return result;
    }

    if (child == 0)
    {
        dup2(pipeFds[1], STDOUT_FILENO);
        dup2(pipeFds[1], STDERR_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
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
        SDL_AtomicSetPtr(&handle->pidSlot, nullptr);
    return result;
}

#endif

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
