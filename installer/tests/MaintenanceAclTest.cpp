#define NOMINMAX
#include "update_install_recovery.h"
#include <cstdio>

static bool accepts(const wchar_t* text, bool usersReadExecute) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text, SDDL_REVISION_1, &descriptor, nullptr)) {
        std::fprintf(stderr, "invalid SDDL fixture: %lu\n", GetLastError());
        std::exit(2);
    }
    const bool value = ymcc::updaterecovery::protectedDescriptor(descriptor, usersReadExecute);
    LocalFree(descriptor);
    return value;
}

int main() {
    struct Test { const wchar_t* sddl; bool readers, expected; };
    const Test tests[] = {
        {L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", false, true},
        {L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)", true, true},
        {L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)", false, false},
        {L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFWFX;;;BU)", true, false},
        {L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;SD;;;BU)", true, false},
        {L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;WD)", true, false},
        {L"O:BAG:BAD:(A;;FA;;;SY)(A;;FA;;;BA)", true, false},
        {L"O:BUG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)", true, false}
    };
    for (unsigned i = 0; i < _countof(tests); ++i) {
        if (accepts(tests[i].sddl, tests[i].readers) != tests[i].expected) {
            std::fprintf(stderr, "ACL case %u failed\n", i + 1);
            return 1;
        }
    }
    std::puts("PASS: 8 maintenance ACL cases; no file or registry changes");
    return 0;
}
