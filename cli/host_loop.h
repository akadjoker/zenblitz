/*
** host_loop.h — the console host loop.
**
** A Blitz program does not run to completion in one call: Delay, WaitTimer
** (and later Flip) suspend the VM and hand the host a wake-up deadline, so
** that a browser or Android build can return to its event loop instead of
** blocking. A console host has no such constraint and simply waits out the
** deadline before resuming.
*/
#ifndef ZENBLITZ_HOST_LOOP_H
#define ZENBLITZ_HOST_LOOP_H

#include "vm.h"

namespace zen
{
    /* Run func to completion, honouring suspensions. Returns the exit code. */
    inline int run_until_done(VM &vm, ObjFunc *func)
    {
        const Backend &b = vm.backend();
        vm.run(func);
        while (vm.suspended())
        {
            int64_t wake = vm.wake_at();
            if (wake > 0 && b.millisecs && b.delay)
            {
                int64_t left = wake - b.millisecs(b.userdata);
                if (left > 0) b.delay(left, b.userdata);
            }
            if (!vm.resume()) break;
        }
        return vm.had_error() ? 1 : 0;
    }
}

#endif
