#include "parse_csv_int.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

using OHOS::NetManagerStandard::ParseCsvUint32;
using OHOS::NetManagerStandard::ParseCsvInt64;

static void Expect(bool cond, const char *msg)
{
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    uint32_t u = 0;
    int64_t i = 0;

    Expect(ParseCsvUint32("0", u) && u == 0, "u0");
    Expect(ParseCsvUint32("4294967295", u) && u == 4294967295u, "uMax");
    Expect(!ParseCsvUint32("4294967296", u), "uOverflow");
    Expect(!ParseCsvUint32("", u), "uEmpty");
    Expect(!ParseCsvUint32("abc", u), "uJunk");
    Expect(!ParseCsvUint32("12a", u), "uTrailing");
    Expect(!ParseCsvUint32("-1", u), "uNeg");
    Expect(!ParseCsvUint32(" 1", u), "uSpace");

    Expect(ParseCsvInt64("0", i) && i == 0, "i0");
    Expect(ParseCsvInt64("-3", i) && i == -3, "iNeg");
    Expect(ParseCsvInt64("9223372036854775807", i) && i == 9223372036854775807LL, "iMax");
    Expect(!ParseCsvInt64("9223372036854775808", i), "iOverflow");
    Expect(!ParseCsvInt64("99999999999999999999", i), "iHuge");
    Expect(!ParseCsvInt64("", i), "iEmpty");
    Expect(!ParseCsvInt64("1.5", i), "iFloat");
    Expect(!ParseCsvInt64("2147483648x", i), "iTrailing");

    std::puts("All host tests passed");
    return 0;
}
