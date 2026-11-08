#include "tetrisphere/text_rewrites.h"

#include <cassert>

int main() {
    using namespace tetrisphere;
    // FNV-1a reference values.
    assert(text_fingerprint("") == 0xcbf29ce484222325ull);
    assert(text_fingerprint("a") == 0xaf63dc4c8601ec8cull);
    // Text that was not reviewed keeps its own wording.
    assert(!text_rewrite("SOMETHING ELSE ENTIRELY").has_value());
    return 0;
}
