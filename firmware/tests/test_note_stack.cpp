// Host test: last-note-priority mono note stack.
#include "engine/note_stack.h"
#include <cassert>
#include <cstdio>

int main()
{
    NoteStack<8> s;
    assert(s.Empty());

    // Last-note priority: newest held note is on top.
    s.Push(60, 100);
    s.Push(64, 110);
    s.Push(67, 120);
    assert(s.Count() == 3);
    assert(s.TopNote() == 67 && s.TopVel() == 120);

    // Release the newest -> fall back to the previous held note.
    assert(s.Remove(67));
    assert(s.TopNote() == 64);

    // Releasing a non-top held note leaves the top unchanged.
    assert(s.Remove(60));
    assert(s.TopNote() == 64);
    assert(s.Count() == 1);

    // Duplicate push moves the note to the top rather than duplicating.
    s.Push(72, 80);
    s.Push(64, 90); // 64 already held
    assert(s.Count() == 2);
    assert(s.TopNote() == 64 && s.TopVel() == 90);

    // Removing an absent note is a no-op.
    assert(!s.Remove(1));

    // Emptying releases everything.
    s.Remove(64);
    s.Remove(72);
    assert(s.Empty());

    std::printf("note_stack OK\n");
    return 0;
}
