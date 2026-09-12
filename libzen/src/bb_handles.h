/*
** bb_handles.h — integer handles for backend objects (files, directories,
** timers). Blitz hands the program an int; the slot behind it holds the
** pointer.
**
** The handle must survive what a Blitz program does with it: store it with
** PokeInt/WriteInt and compare it against small numbers. So it is a 31-bit
** value (sign bit clear, never negative, never 0 — which is Blitz's
** failure value) rather than a pointer or a wide key.
**
** A generation counter rides in the high bits so that a handle used after
** being closed is rejected instead of landing on whatever reused its slot:
** closing a file twice, or reading from a closed one, gets a clean "no such
** handle" rather than another program's file. 16 bits of index and 15 of
** generation hold 64K live objects and 32K reuses of each slot, past
** anything a Blitz program opens; the generation wraps rather than
** overflowing into the index.
*/
#ifndef BB_HANDLES_H
#define BB_HANDLES_H

#include "ct/vector.hpp"

namespace bb
{
    template <typename P> /* P is a pointer type; nullptr marks a free slot */
    struct Handles
    {
        enum { INDEX_BITS = 16, MAX_SLOTS = 1 << INDEX_BITS, GEN_MASK = 0x7FFF };

        struct Slot
        {
            P value;
            unsigned generation;
        };

        ct::Vector<Slot> slots;
        ct::Vector<unsigned> free_slots;

        long long add(P p)
        {
            unsigned index;
            if (!free_slots.empty())
            {
                index = free_slots.back();
                free_slots.pop_back();
                slots[index].value = p;
            }
            else
            {
                if (slots.size() >= MAX_SLOTS) return 0; /* out of handle space */
                index = (unsigned)slots.size();
                Slot s;
                s.value = p;
                s.generation = 1;
                slots.push_back(s);
            }
            return pack(index, slots[index].generation);
        }

        P get(long long h) const
        {
            unsigned index;
            return live(h, &index) ? slots[index].value : P();
        }

        void remove(long long h)
        {
            unsigned index;
            if (!live(h, &index)) return;
            slots[index].value = P();
            /* a new generation, so handles to the old occupant stop matching */
            slots[index].generation = (slots[index].generation + 1) & GEN_MASK;
            if (slots[index].generation == 0) slots[index].generation = 1;
            free_slots.push_back(index);
        }

    private:
        static long long pack(unsigned index, unsigned generation)
        {
            return (long long)(((generation & GEN_MASK) << INDEX_BITS) | index);
        }

        /* True when `h` names a slot that is still holding the object it was
           issued for. Writes the slot index through `out`. */
        bool live(long long h, unsigned *out) const
        {
            if (h <= 0) return false;
            unsigned index = (unsigned)h & (MAX_SLOTS - 1);
            unsigned generation = ((unsigned)h >> INDEX_BITS) & GEN_MASK;
            if (index >= slots.size()) return false;
            const Slot &s = slots[index];
            if (!s.value || s.generation != generation) return false;
            *out = index;
            return true;
        }
    };
}

#endif
