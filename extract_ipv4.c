#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_INPUT 1024

/*
 * getDigitRun: starting at str[pos], counts how many consecutive
 * digit characters follow (the "maximal" run of digits). This is
 * used so we never accept a partial slice of a longer digit run
 * as an octet or port (e.g. "1234" can never yield a valid 3-digit
 * octet "123").
 */
static int getDigitRun(const char* str, int pos) {
    int len = 0;
    while (isdigit((unsigned char)str[pos + len])) {
        len++;
    }
    return len;
}

/*
 * extractIPv4: scans str for the first valid IPv4 address
 * (optionally followed by :port).
 *
 * Rules enforced:
 *   - 4 octets separated by '.', each 1-3 digits, value 0-255
 *   - an octet may only have a leading zero if it IS "0" (single digit);
 *     runs like "00", "012" are invalid regardless of value
 *   - optional ":port" after the 4th octet, 1-5 digits, value 0-65535,
 *     same leading-zero rule as above
 *   - if a colon is found but the port portion is invalid, the WHOLE
 *     address+port candidate is rejected (not just the port)
 *
 * On success, returns 1, and:
 *   *outAddress = 32-bit value of the address (A<<24 | B<<16 | C<<8 | D)
 *   *outPort    = 0-65535 if a port was present, otherwise -1
 *
 * On failure (no valid address anywhere in the string), returns 0 and
 * leaves *outAddress / *outPort untouched.
 */
int extractIPv4(const char* str, unsigned long* outAddress, int* outPort) {
    int len = (int)strlen(str);
    int i = 0;

    while (i < len) {
        /* Only attempt a match at the START of a digit run. If the
         * previous character is also a digit, starting here would mean
         * slicing into the middle of a longer number, which can never
         * be a valid octet. */
        int prevIsDigit = (i > 0) && isdigit((unsigned char)str[i - 1]);
        if (isdigit((unsigned char)str[i]) && !prevIsDigit) {
            int pos = i;
            int octets[4];
            int success = 1;

            for (int o = 0; o < 4 && success; o++) {
                int runLen = getDigitRun(str, pos);

                if (runLen < 1 || runLen > 3) {
                    success = 0;
                    break;
                }
                /* leading zero check: "0" alone is fine, "0x..." with
                 * more digits after is not */
                if (runLen > 1 && str[pos] == '0') {
                    success = 0;
                    break;
                }

                int value = 0;
                for (int k = 0; k < runLen; k++) {
                    value = value * 10 + (str[pos + k] - '0');
                }
                if (value > 255) {
                    success = 0;
                    break;
                }

                octets[o] = value;
                pos += runLen;

                if (o < 3) {
                    /* need a '.' followed immediately by another digit */
                    if (str[pos] != '.') {
                        success = 0;
                        break;
                    }
                    pos += 1;
                    if (!isdigit((unsigned char)str[pos])) {
                        success = 0;
                        break;
                    }
                }
            }

            if (success) {
                int port = -1;

                if (str[pos] == ':') {
                    int afterColon = pos + 1;
                    int runLen = getDigitRun(str, afterColon);

                    if (runLen < 1 || runLen > 5) {
                        success = 0;
                    } else if (runLen > 1 && str[afterColon] == '0') {
                        success = 0;
                    } else {
                        long value = 0;
                        for (int k = 0; k < runLen; k++) {
                            value = value * 10 + (str[afterColon + k] - '0');
                        }
                        if (value > 65535) {
                            success = 0;
                        } else {
                            port = (int)value;
                        }
                    }
                }
                else if (str[pos] == '.') 
                {
                    success = 0;
                }

                if (success) {
                    unsigned long address =
                        ((unsigned long)octets[0] << 24) |
                        ((unsigned long)octets[1] << 16) |
                        ((unsigned long)octets[2] << 8) |
                        ((unsigned long)octets[3]);

                    *outAddress = address;
                    *outPort = port;
                    return 1;
                }
            }
        }

        i++;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*                          TEST HARNESS                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* description;
    const char* input;
    int expectedReturn;        /* 0 or 1 */
    unsigned long expectedAddress; /* only checked if expectedReturn == 1 */
    int expectedPort;              /* only checked if expectedReturn == 1 */
} TestCase;

static unsigned long makeAddress(int a, int b, int c, int d) {
    return ((unsigned long)a << 24) | ((unsigned long)b << 16) |
           ((unsigned long)c << 8)  |  (unsigned long)d;
}

void runTests(void) {
    TestCase tests[] = {
        /* 1. Invalid octet range */
        {"Octet range: 256 (one over max)",           "256.1.2.3",                 0, 0, 0},
        {"Octet range: 999 in middle octet",          "1.2.999.3",                 0, 0, 0},
        {"Octet range: 300 in last octet",            "1.2.3.300",                 0, 0, 0},

        /* 2. Invalid port range */
        {"Port range: 65536 (one over max)",          "1.2.3.4:65536",             0, 0, 0},
        {"Port range: 99999",                         "10.0.0.1:99999",            0, 0, 0},

        /* 3. Trailing '.' or trailing ':' */
        {"Trailing dot after 4th octet",              "1.2.3.4.",                  0, 0, 0},
        {"Trailing colon with no port digits",        "1.2.3.4:",                  0, 0, 0},

        /* 4. Invalid number of octets */
        {"Too few octets (3)",                        "1.2.3",                     0, 0, 0},
        {"Too many octets (5)",                       "1.2.3.4.5",                 0, 0, 0},

        /* 5. Port placed in the middle of the octets */
        {"Port jammed between octets",                "1.2:80.3.4",                0, 0, 0},

        /* 6. First of several valid addresses wins */
        {"First valid address wins over later ones",  "bad 999.1.1.1 then 1.2.3.4 and 5.6.7.8",
                                                                                     1, 0, -1}, /* address filled in below */

        /* 7. Valid address but invalid port -> everything rejected */
        {"Valid address + invalid port rejects both", "10.0.0.1:70000",            0, 0, 0},

        /* 8. Oversized leading digit run swallows the octet */
        {"Oversized first octet (maximal munch)",     "connecting to 1234.12.12.12", 0, 0, 0},

        /* Additional edge cases */
        {"Leading zero in an octet is invalid",       "192.168.01.1",              0, 0, 0},
        {"All-zero address is valid",                 "0.0.0.0",                   1, 0, -1}, /* filled in below */
        {"Port value 0 (single digit) is valid",      "1.2.3.4:0",                 1, 0, 0},   /* addr filled in below */
        {"Port with leading zero + extra digit",      "1.2.3.4:007",               0, 0, 0},
        {"Max valid octets and port",                 "255.255.255.255:65535",     1, 0, 65535}, /* addr filled below */
        {"Address bounded by punctuation",            "[1.2.3.4]",                 1, 0, -1},     /* addr filled below */
        {"Oversized port digit run (6 digits)",       "1.2.3.4:123456",            0, 0, 0},
        {"Oversized last-octet digit run",            "1.2.3.44445",               0, 0, 0},
        {"Trailing text after a valid port is fine",  "1.2.3.4:80.5",              1, 0, 80},    /* addr filled below */
        {"Valid address after an earlier invalid one (trailing dot)",
                                                       "1.2.3.4. 5.6.7.8",          1, 0, -1},    /* addr filled below */
    };

    /* fill in the expected addresses that reference makeAddress() */
    for (size_t t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
        if (strcmp(tests[t].input, "bad 999.1.1.1 then 1.2.3.4 and 5.6.7.8") == 0) tests[t].expectedAddress = makeAddress(1,2,3,4);
        else if (strcmp(tests[t].input, "0.0.0.0") == 0)                          tests[t].expectedAddress = makeAddress(0,0,0,0);
        else if (strcmp(tests[t].input, "1.2.3.4:0") == 0)                        tests[t].expectedAddress = makeAddress(1,2,3,4);
        else if (strcmp(tests[t].input, "255.255.255.255:65535") == 0)            tests[t].expectedAddress = makeAddress(255,255,255,255);
        else if (strcmp(tests[t].input, "[1.2.3.4]") == 0)                        tests[t].expectedAddress = makeAddress(1,2,3,4);
        else if (strcmp(tests[t].input, "1.2.3.4:80.5") == 0)                     tests[t].expectedAddress = makeAddress(1,2,3,4);
        else if (strcmp(tests[t].input, "1.2.3.4. 5.6.7.8") == 0)                 tests[t].expectedAddress = makeAddress(5,6,7,8);
    }

    int numTests = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;

    for (int t = 0; t < numTests; t++) {
        unsigned long address = 0;
        int port = 0;
        int result = extractIPv4(tests[t].input, &address, &port);

        int ok = (result == tests[t].expectedReturn);
        if (ok && result == 1) {
            ok = (address == tests[t].expectedAddress) && (port == tests[t].expectedPort);
        }

        printf("[%s] %s\n", ok ? "PASS" : "FAIL", tests[t].description);
        printf("       input: \"%s\"\n", tests[t].input);
        if (!ok) {
            printf("       expected: return=%d", tests[t].expectedReturn);
            if (tests[t].expectedReturn == 1) {
                printf(", address=%lu, port=%d", tests[t].expectedAddress, tests[t].expectedPort);
            }
            printf("\n       actual:   return=%d", result);
            if (result == 1) {
                printf(", address=%lu, port=%d", address, port);
            }
            printf("\n");
        } else {
            passed++;
        }
    }

    printf("\n%d / %d tests passed.\n", passed, numTests);
}

int main(int argc, char* argv[]) {
    if (argc > 1 && strcmp(argv[1], "--test") == 0) {
        runTests();
        return 0;
    }

    char input[MAX_INPUT];

    while (1) {
        printf("Enter a string (or 'END' to quit): ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            break; /* EOF on stdin */
        }

        /* strip trailing newline / carriage return */
        size_t len = strlen(input);
        while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r')) {
            input[--len] = '\0';
        }

        if (strcmp(input, "END") == 0) {
            printf("Program terminated.\n");
            break;
        }

        unsigned long address;
        int port;

        if (extractIPv4(input, &address, &port)) {
            unsigned int a = (unsigned int)((address >> 24) & 0xFF);
            unsigned int b = (unsigned int)((address >> 16) & 0xFF);
            unsigned int c = (unsigned int)((address >> 8) & 0xFF);
            unsigned int d = (unsigned int)(address & 0xFF);
            
            if (port != -1)
            {
                printf("Extracted IPv4 address: %u.%u.%u.%u (decimal value: %lu, port: %d)\n",
                   a, b, c, d, address, port);
            }
            else {
                printf("Extracted IPv4 address: %u.%u.%u.%u (decimal value: %lu, port: none)\n",
                   a, b, c, d, address);
            }

        } else {
            printf("Invalid input: no valid IPv4 address found.\n");
        }
    }

    return 0;
}