// Boundary and hostile-input tests for the game's 16-bit wchar_t formatter.
// Builds with -fshort-wchar on POSIX hosts to match the game ABI.
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
int port_swprintf(wchar_t* dst, size_t n, const wchar_t* fmt, ...);
size_t port_wcslen(const wchar_t* s);
}

namespace {

int failures = 0;

void check(const char* name, bool ok) {
    printf("%-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) ++failures;
}

bool same(const wchar_t* a, const wchar_t* b) {
    const size_t na = port_wcslen(a);
    const size_t nb = port_wcslen(b);
    return na == nb && memcmp(a, b, na * sizeof(wchar_t)) == 0;
}

bool guardIntact(const wchar_t* buf, size_t from, size_t total) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(buf);
    for (size_t i = from * sizeof(wchar_t); i < total; ++i) {
        if (bytes[i] != 0xCD) return false;
    }
    return true;
}

void testBasicConversions() {
    wchar_t buf[64];
    int r = port_swprintf(buf, 64, L"abc%%def");
    check("literal text and %%", r == 7 && same(buf, L"abc%def"));

    r = port_swprintf(buf, 64, L"%d/%u/%#x/%+d/% d", -5, 7u, 0x2au, 3, 4);
    check("integer conversions", r == 15 && same(buf, L"-5/7/0x2a/+3/ 4"));

    r = port_swprintf(buf, 64, L"%5.3d", 7);
    check("width and precision combine", r == 5 && same(buf, L"  007"));

    r = port_swprintf(buf, 64, L"%+.2f", -1.5);
    check("negative float", r == 5 && same(buf, L"-1.50"));

    r = port_swprintf(buf, 64, L"%p", reinterpret_cast<void*>(0x2a));
    check("pointer conversion", r == 4 && same(buf, L"0x2a"));

    r = port_swprintf(buf, 64, L"%c", 'Z');
    check("character conversion", r == 1 && same(buf, L"Z"));
}

void testHostileArguments() {
    wchar_t buf[128];
    const wchar_t* hostile = L"%n %1000000000d %s %ls %%";
    int r = port_swprintf(buf, 128, L"%ls", hostile);
    check("hostile text passed via %ls stays literal",
          r == static_cast<int>(port_wcslen(hostile)) && same(buf, hostile));

    r = port_swprintf(buf, 64, L"a%nb");
    check("%n never writes through a pointer", r == 4 && same(buf, L"a%nb"));

    r = port_swprintf(buf, 64, L"%.3s", "abcdef");
    check("narrow string precision", r == 3 && same(buf, L"abc"));

    r = port_swprintf(buf, 64, L"%ls", static_cast<const wchar_t*>(nullptr));
    check("null wide string is printed safely", r == 6 && same(buf, L"(null)"));
}

void testWidthCap() {
    wchar_t buf[16];
    memset(buf, 0xCD, sizeof buf);
    int r = port_swprintf(buf, 8, L"%1000000000d", 5);
    check("enormous width returns the clamped length", r == 4096);
    check("enormous width truncates without overflow",
          buf[7] == 0 && same(buf, L"       ") && guardIntact(buf, 8, 16));

    memset(buf, 0xCD, sizeof buf);
    r = port_swprintf(buf, 12, L"[%*d]", INT_MIN, 42);
    check("INT_MIN star width is clamped", r == 4098);
    check("INT_MIN star width leaves no overflow",
          buf[0] == L'[' && buf[1] == L'4' && buf[2] == L'2' && buf[3] == L' ' &&
              buf[11] == 0 && guardIntact(buf, 12, 16));

    wchar_t left[8];
    r = port_swprintf(left, 8, L"%*d", -6, 42);
    check("negative star width left-aligns", r == 6 && same(left, L"42    "));
}

void testPrecisionCap() {
    wchar_t buf[80];
    memset(buf, 0xCD, sizeof buf);
    int r = port_swprintf(buf, 80, L"%.10000d", 7);
    bool zeros = r == 60;
    for (int i = 0; zeros && i < 59; ++i) zeros = buf[i] == L'0';
    check("enormous precision is capped at 60 digits", zeros);
    check("precision output fits the conversion buffer",
          buf[59] == L'7' && buf[60] == 0 && guardIntact(buf, 61, 80));

    r = port_swprintf(buf, 80, L"%.*d", INT_MAX, 7);
    check("INT_MAX star precision is capped", r == 60 && buf[59] == L'7');

    r = port_swprintf(buf, 80, L"%.*d", -1, 7);
    check("negative star precision means none", r == 1 && same(buf, L"7"));
}

void testIntegerLimits() {
    wchar_t buf[64];
    int r = port_swprintf(buf, 64, L"%lld", LLONG_MIN);
    check("LLONG_MIN negates without overflow",
          r == 20 && same(buf, L"-9223372036854775808"));

    r = port_swprintf(buf, 64, L"%llu", ULLONG_MAX);
    check("ULLONG_MAX prints every digit",
          r == 20 && same(buf, L"18446744073709551615"));
}

void testTruncation() {
    wchar_t buf[8];
    memset(buf, 0xCD, sizeof buf);
    int r = port_swprintf(buf, 4, L"abcdef");
    check("truncation still returns the full length", r == 6);
    check("truncation NUL-terminates inside the buffer",
          buf[3] == 0 && same(buf, L"abc") && guardIntact(buf, 4, 8));

    r = port_swprintf(buf, 1, L"abcdef");
    check("n=1 writes only the terminator", r == 6 && buf[0] == 0);

    r = port_swprintf(nullptr, 0, L"abcdef");
    check("n=0 performs no writes and reports the length", r == 6);
}

}  // namespace

int main() {
    printf("=== Running wide-format tests (wchar_t = %zu bytes) ===\n", sizeof(wchar_t));
    testBasicConversions();
    testHostileArguments();
    testWidthCap();
    testPrecisionCap();
    testIntegerLimits();
    testTruncation();
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
