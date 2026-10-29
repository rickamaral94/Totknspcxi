#include "test.h"

#include <cstdlib>
#include <cstring>

std::vector<TestCase> &test_registry() {
    static std::vector<TestCase> r;
    return r;
}
int g_failures;

int write_sample(const std::string &dir, int mib);

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--write-sample")) return write_sample(argv[2], argc > 3 ? atoi(argv[3]) : 0);
    int run = 0;
    for (auto &t : test_registry()) {
        if (argc > 1 && !strstr(t.name, argv[1])) continue;
        int before = g_failures;
        fprintf(stderr, "[ RUN  ] %s\n", t.name);
        try {
            t.fn();
        } catch (const RequireFailed &) {
        } catch (const nx::Error &e) {
            fprintf(stderr, "  unexpected nx::Error: %s\n", e.en().c_str());
            g_failures++;
        } catch (const std::exception &e) {
            fprintf(stderr, "  unexpected exception: %s\n", e.what());
            g_failures++;
        }
        fprintf(stderr, "[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
        run++;
    }
    fprintf(stderr, "%d tests, %d failures\n", run, g_failures);
    return g_failures ? 1 : 0;
}
