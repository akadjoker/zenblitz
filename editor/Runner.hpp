#pragma once

#include <SDL_atomic.h>
#include <SDL_thread.h>

#include <igui/Types.hpp>

namespace zed
{

struct RunResult
{
    ig::String output;
    int exitCode = 0;
    bool ranAtAll = false;
};

class Runner
{
public:
#if defined(_WIN32)
    explicit Runner(ig::String runnerPath = ".\\zenblitz3d.exe");
#else
    explicit Runner(ig::String runnerPath = "./zenblitz3d");
#endif

    RunResult run(const ig::String& scriptPath) const;

    struct RunHandle;

    RunHandle* runAsync(const ig::String& scriptPath) const;
    // Returns true once the child has exited. On every call, out.output
    // receives only the new stdout/stderr captured since the previous poll,
    // so the UI can append it while the program is still running.
    bool poll(RunHandle* handle, RunResult& out) const;
    void stop(RunHandle* handle) const;
    // Writes one line (a newline is appended) to the running program's
    // stdin, so Input$ can be answered from the Output panel. Returns
    // false if the program has already exited or never started.
    bool sendInput(RunHandle* handle, const ig::String& line) const;

private:
    ig::String runnerPath_;
};

} // namespace zed
