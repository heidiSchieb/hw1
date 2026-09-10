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

int main(void) {
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
            printf("Program terminated\n");
            break;
        }

        unsigned long address;
        int port;

        if (extractIPv4(input, &address, &port)) {
            unsigned int a = (unsigned int)((address >> 24) & 0xFF);
            unsigned int b = (unsigned int)((address >> 16) & 0xFF);
            unsigned int c = (unsigned int)((address >> 8) & 0xFF);
            unsigned int d = (unsigned int)(address & 0xFF);

            printf("Extracted IPv4 address: %u.%u.%u.%u (decimal value: %lu, port: %d)\n",
                   a, b, c, d, address, port);
        } else {
            printf("No valid IPv4 address found.\n");
        }
    }

    return 0;
}
