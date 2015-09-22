#include "os.hpp"
#include "genesis_editor.hpp"
#include "error.h"

int main(int argc, char *argv[]) {
    int err;
    if ((err = os_init()))
        panic("unable to initialize: %s", genesis_strerror(err));

    GenesisEditor genesis_editor;
    genesis_editor.exec();

    return 0;
}
