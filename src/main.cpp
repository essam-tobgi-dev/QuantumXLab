// The `quantumxlab` executable (spec 02 §2, 03 §1). Everything it does lives in `qlab::app`, so
// that the GUI, the headless modes and the tests enter through the same door.
#include "App/App.hpp"

int main(int argc, char** argv) {
    return qlab::app::main(argc, argv);
}
