#include "csl.h"

int main(int argc, char **argv) {
    csl_easy_init("log.bin", LL_INFO);

    LOG("{}/{}/{}", LL_INFO, 1, "", 1.0f);

    for (int i = 0; i < 10; ++i) {
        LOG("{}", LL_INFO, i);
    }

    LOG("{}", LL_INFO, "end");
    csl_easy_end();
}
