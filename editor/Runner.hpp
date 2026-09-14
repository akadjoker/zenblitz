#pragma once

#include <SDL_atomic.h>
#include <SDL_thread.h>

#include <ct/vector.hpp>
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
    RunResult run(const ig::String& programPath, const ct::Vector<ig::String>& arguments,
                  const ig::String& workingDirectory = ig::String()) const;

    struct RunHandle;

    RunHandle* runAsync(const ig::String& scriptPath) const;
    RunHandle* runAsync(const ig::String& programPath, const ct::Vector<ig::String>& arguments,
                        const ig::String& workingDirectory = ig::String()) const;
    // Returns true once the child has exited. On every call, out.output
    // receives only the new stdout/stderr captured since the previous poll,
    // so the UI can append it while the program is still running.
    bool poll(RunHandle* handle, RunResult& out) const;
    void stop(RunHandle* handle) const;

private:
    ig::String runnerPath_;
};

} // namespace zed
