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
    // Answers a prompt: writes one line (text followed by a newline) to the
    // running program's stdin. Every child gets a stdin pipe of its own
    // rather than the editor's own input stream (see Runner.cpp), so a
    // program that asks a question - every sample does, through start.bb's
    // SetGfx - is answered from the Output panel's console row instead of
    // blocking on a terminal the editor does not have. False when the run
    // has already exited and there is nothing left to write to.
    bool writeInput(RunHandle* handle, const ig::String& text) const;
    // Returns true once the child has exited. On every call, out.output
    // receives only the new stdout/stderr captured since the previous poll,
    // so the UI can append it while the program is still running.
    bool poll(RunHandle* handle, RunResult& out) const;
    void stop(RunHandle* handle) const;

private:
    ig::String runnerPath_;
};

} // namespace zed
