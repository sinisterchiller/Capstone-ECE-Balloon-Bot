/*
 * Host tests for CM4/Core/Src/nmea_gga.c.
 *
 * The parser is pure C with no HAL dependency, so it runs unchanged on the
 * development machine. Build and run from the MXproject directory:
 *
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *      -I CM4/Core/Inc CM4/Core/Src/nmea_gga.c CM4/tests/nmea_gga_test.c \
 *      -o /tmp/nmea_gga_test && /tmp/nmea_gga_test
 */

#include "nmea_gga.h"

#include <stdio.h>
#include <string.h>

/* Classic fixed-position GGA: 48 07.038 N, 011 31.000 E, 545.4 m MSL. */
#define GGA_FIX_NORTH_EAST \
  "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"

/* Diagnostics the parser prints; the tests pin the user-visible text. */
#define NO_FIX_ZERO_SATELLITES \
  "GPS STATUS: No position fix yet (0 satellites used). Check antenna and sky view."
#define NO_FIX_UNKNOWN_SATELLITES \
  "GPS STATUS: No position fix yet (satellite count unavailable). Check antenna and sky view."
#define GGA_INVALID_TEXT \
  "GPS ERROR: Unusable GGA (checksum or field format); no position decoded."

static unsigned int failures;

typedef struct
{
  const char *name;
  const char *sentence;
  NmeaGgaStatus status;
  const char *line;
} RenderCase;

static const RenderCase render_cases[] =
{
  {
    "north/east fix",
    GGA_FIX_NORTH_EAST,
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=11.516667 altitude=545.4 m"
  },
  {
    "GN talker fix",
    "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59",
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=11.516667 altitude=545.4 m"
  },
  {
    "south/west fix",
    "$GPGGA,123520,3352.000,S,15112.000,W,1,08,0.9,25.0,M,0.0,M,,*4D",
    NMEA_GGA_FIX,
    "GPS lat=-33.866667 lon=-151.200000 altitude=25.0 m"
  },
  {
    "lower-case checksum digits",
    "$GPGGA,123520,3352.000,S,15112.000,W,1,08,0.9,25.0,M,0.0,M,,*4d",
    NMEA_GGA_FIX,
    "GPS lat=-33.866667 lon=-151.200000 altitude=25.0 m"
  },
  {
    "CRLF terminated sentence",
    GGA_FIX_NORTH_EAST "\r\n",
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=11.516667 altitude=545.4 m"
  },
  {
    "fix quality 9",
    "$GPGGA,123539,4807.038,N,01131.000,E,9,08,0.9,545.4,M,46.9,M,,*4D",
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=11.516667 altitude=545.4 m"
  },
  {
    "extra latitude fraction digits",
    "$GPGGA,123538,4807.0381234,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*40",
    NMEA_GGA_FIX,
    "GPS lat=48.117302 lon=11.516667 altitude=545.4 m"
  },
  {
    "latitude at the north pole limit",
    "$GPGGA,123529,9000.000,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*4D",
    NMEA_GGA_FIX,
    "GPS lat=90.000000 lon=11.516667 altitude=545.4 m"
  },
  {
    "longitude at the antimeridian limit",
    "$GPGGA,123531,4807.038,N,18000.000,E,1,08,0.9,545.4,M,46.9,M,,*46",
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=180.000000 altitude=545.4 m"
  },
  {
    "zero coordinate keeps no negative sign",
    "$GPGGA,123537,0000.000,N,00000.000,E,1,08,0.9,0.0,M,0.0,M,,*72",
    NMEA_GGA_FIX,
    "GPS lat=0.000000 lon=0.000000 altitude=0.0 m"
  },
  {
    "altitude precision preserved (two decimals)",
    "$GPGGA,123523,4807.038,N,01131.000,E,1,08,0.9,545.40,M,46.9,M,,*7E",
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=11.516667 altitude=545.40 m"
  },
  {
    "altitude precision preserved (integer)",
    "$GPGGA,123525,4807.038,N,01131.000,E,1,08,0.9,545,M,46.9,M,,*52",
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=11.516667 altitude=545 m"
  },
  {
    "negative altitude",
    "$GPGGA,123524,4807.038,N,01131.000,E,1,08,0.9,-12.30,M,46.9,M,,*64",
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=11.516667 altitude=-12.30 m"
  },
  {
    "explicitly signed altitude",
    "$GPGGA,123541,4807.038,N,01131.000,E,1,08,0.9,+545.4,M,46.9,M,,*61",
    NMEA_GGA_FIX,
    "GPS lat=48.117300 lon=11.516667 altitude=+545.4 m"
  },
  {
    "user-style no fix, empty position and quality 0",
    "$GPGGA,123521,,,,,0,00,99.99,,,,,,*4E",
    NMEA_GGA_NO_FIX,
    NO_FIX_ZERO_SATELLITES
  },
  {
    /* Captured verbatim from the receiver before its first fix. */
    "real receiver no fix before first fix",
    "$GPGGA,020246.00,,,,,0,00,99.99,,,,,,*64",
    NMEA_GGA_NO_FIX,
    NO_FIX_ZERO_SATELLITES
  },
  {
    "no fix reporting three satellites used",
    "$GPGGA,123521,,,,,0,03,99.99,,,,,,*4D",
    NMEA_GGA_NO_FIX,
    "GPS STATUS: No position fix yet (3 satellites used). Check antenna and sky view."
  },
  {
    "no fix reporting twelve satellites used",
    "$GPGGA,123521,,,,,0,12,99.99,,,,,,*4D",
    NMEA_GGA_NO_FIX,
    "GPS STATUS: No position fix yet (12 satellites used). Check antenna and sky view."
  },
  {
    "no fix with an empty satellite field",
    "$GPGGA,123521,,,,,0,,99.99,,,,,,*4E",
    NMEA_GGA_NO_FIX,
    NO_FIX_UNKNOWN_SATELLITES
  },
  {
    "no fix with a non-numeric satellite field",
    "$GPGGA,123521,,,,,0,x7,99.99,,,,,,*01",
    NMEA_GGA_NO_FIX,
    NO_FIX_UNKNOWN_SATELLITES
  },
  {
    "no fix with an out-of-range satellite field",
    "$GPGGA,123521,,,,,0,123,99.99,,,,,,*7E",
    NMEA_GGA_NO_FIX,
    NO_FIX_UNKNOWN_SATELLITES
  },
  {
    "no fix with truncated trailing fields",
    "$GPGGA,123521,,,,,0*60",
    NMEA_GGA_NO_FIX,
    NO_FIX_UNKNOWN_SATELLITES
  },
  {
    "quality 0 overrides present coordinates",
    "$GPGGA,123522,4807.038,N,01131.000,E,0,00,99.99,545.4,M,46.9,M,,*4F",
    NMEA_GGA_NO_FIX,
    NO_FIX_ZERO_SATELLITES
  },
  {
    "checksum failure",
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "checksum missing",
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "checksum has a non-hex digit",
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*4G",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "text after the checksum",
    GGA_FIX_NORTH_EAST "x",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "checksum field truncated after the star",
    "$GPGGA,123519*",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "checksum field with one digit",
    "$GPGGA,123519*4",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "latitude integer part too short",
    "$GPGGA,123526,480.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*7C",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "latitude hemisphere mismatch",
    "$GPGGA,123527,4807.038,E,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*41",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "latitude minutes out of range",
    "$GPGGA,123528,4860.000,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*4F",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "latitude beyond 90 degrees",
    "$GPGGA,123530,9000.001,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*44",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "longitude beyond 180 degrees",
    "$GPGGA,123532,4807.038,N,18000.001,E,1,08,0.9,545.4,M,46.9,M,,*44",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "altitude unit is not metres",
    "$GPGGA,123533,4807.038,N,01131.000,E,1,08,0.9,545.4,F,46.9,M,,*44",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "lower-case altitude unit",
    "$GPGGA,123543,4807.038,N,01131.000,E,1,08,0.9,545.4,m,46.9,M,,*68",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "altitude empty with a claimed fix",
    "$GPGGA,123534,4807.038,N,01131.000,E,1,08,0.9,,M,46.9,M,,*66",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "altitude is not a number",
    "$GPGGA,123535,4807.038,N,01131.000,E,1,08,0.9,54x5.4,M,46.9,M,,*31",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "altitude has only a decimal point",
    "$GPGGA,123544,4807.038,N,01131.000,E,1,08,0.9,.,M,46.9,M,,*4F",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "latitude empty with a claimed fix",
    "$GPGGA,123536,,,,,1,00,99.99,545.4,M,46.9,M,,*72",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "GGA without position fields",
    "$GPGGA,123540,4807.038,N*2B",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "fix quality is not a number",
    "$GPGGA,123519,4807.038,N,01131.000,E,x,08,0.9,545.4,M,46.9,M,,*0E",
    NMEA_GGA_INVALID,
    GGA_INVALID_TEXT
  },
  {
    "GSV satellite list is not a position",
    "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74",
    NMEA_GGA_NOT_GGA,
    ""
  },
  {
    "GSA sentence is not a GGA",
    "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39",
    NMEA_GGA_NOT_GGA,
    ""
  },
  {
    "RMC sentence is not a GGA",
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
    NMEA_GGA_NOT_GGA,
    ""
  },
  {
    "unrecognised talker",
    "$GLGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*5B",
    NMEA_GGA_NOT_GGA,
    ""
  },
  {
    "garbage line",
    "not an NMEA sentence",
    NMEA_GGA_NOT_GGA,
    ""
  },
};

static void check_render(const RenderCase *test_case)
{
  char destination[NMEA_GGA_LINE_CAPACITY];
  uint16_t length = 0xFFFFu;
  NmeaGgaStatus status;

  memset(destination, 0xA5, sizeof(destination));
  status = NMEA_GGA_RenderLine(test_case->sentence, destination,
                               (uint16_t)sizeof(destination), &length);

  if (status != test_case->status)
  {
    printf("FAIL %s: status %d, expected %d\n", test_case->name, (int)status,
           (int)test_case->status);
    failures++;
  }
  if (strcmp(destination, test_case->line) != 0)
  {
    printf("FAIL %s: line \"%s\", expected \"%s\"\n", test_case->name, destination,
           test_case->line);
    failures++;
  }
  if (length != (uint16_t)strlen(test_case->line))
  {
    printf("FAIL %s: length %u, expected %u\n", test_case->name,
           (unsigned int)length, (unsigned int)strlen(test_case->line));
    failures++;
  }
}

static void check_null_arguments(void)
{
  char destination[NMEA_GGA_LINE_CAPACITY];
  uint16_t length = 0xFFFFu;

  if (NMEA_GGA_RenderLine(NULL, destination, (uint16_t)sizeof(destination), &length) !=
      NMEA_GGA_INVALID)
  {
    printf("FAIL NULL sentence was not rejected\n");
    failures++;
  }
  if (length != 0u)
  {
    printf("FAIL NULL sentence did not clear the length\n");
    failures++;
  }
  if (NMEA_GGA_RenderLine(GGA_FIX_NORTH_EAST, NULL, 1u, &length) != NMEA_GGA_INVALID)
  {
    printf("FAIL NULL destination was not rejected\n");
    failures++;
  }
  if (NMEA_GGA_RenderLine(GGA_FIX_NORTH_EAST, destination, 0u, &length) !=
      NMEA_GGA_INVALID)
  {
    printf("FAIL zero capacity was not rejected\n");
    failures++;
  }
  if (NMEA_GGA_RenderLine(GGA_FIX_NORTH_EAST, destination,
                          (uint16_t)sizeof(destination), NULL) != NMEA_GGA_INVALID)
  {
    printf("FAIL NULL length pointer was not rejected\n");
    failures++;
  }
}

/*
 * The rendered line must fit exactly, including its NUL; one byte less has to
 * be rejected without leaving a partial line behind.
 */
static void check_capacity_bound(void)
{
  const char *expected = "GPS lat=48.117300 lon=11.516667 altitude=545.4 m";
  uint16_t required = (uint16_t)(strlen(expected) + 1u);
  char destination[NMEA_GGA_LINE_CAPACITY];
  uint16_t length = 0xFFFFu;

  memset(destination, 0xA5, sizeof(destination));
  if (NMEA_GGA_RenderLine(GGA_FIX_NORTH_EAST, destination, (uint16_t)(required - 1u),
                          &length) != NMEA_GGA_INVALID)
  {
    printf("FAIL one byte too small was accepted\n");
    failures++;
  }
  if ((destination[0] != '\0') || (length != 0u))
  {
    printf("FAIL rejected capacity left output behind\n");
    failures++;
  }

  if (NMEA_GGA_RenderLine(GGA_FIX_NORTH_EAST, destination, required, &length) !=
      NMEA_GGA_FIX)
  {
    printf("FAIL exact capacity was rejected\n");
    failures++;
  }
  if (strcmp(destination, expected) != 0)
  {
    printf("FAIL exact capacity produced \"%s\"\n", destination);
    failures++;
  }

  {
    const char *no_fix_expected = NO_FIX_UNKNOWN_SATELLITES; /* longest line */
    uint16_t no_fix_required = (uint16_t)(strlen(no_fix_expected) + 1u);

    if (NMEA_GGA_RenderLine("$GPGGA,123521,,,,,0*60", destination,
                            (uint16_t)(no_fix_required - 1u), &length) !=
        NMEA_GGA_INVALID)
    {
      printf("FAIL no-fix text overran a short buffer\n");
      failures++;
    }
    if ((destination[0] != '\0') || (length != 0u))
    {
      printf("FAIL rejected no-fix capacity left output behind\n");
      failures++;
    }
    if (NMEA_GGA_RenderLine("$GPGGA,123521,,,,,0*60", destination, no_fix_required,
                            &length) != NMEA_GGA_NO_FIX)
    {
      printf("FAIL no-fix text did not fit its exact buffer\n");
      failures++;
    }
    if (strcmp(destination, no_fix_expected) != 0)
    {
      printf("FAIL no-fix text was \"%s\"\n", destination);
      failures++;
    }
  }

  {
    const char *invalid_expected = GGA_INVALID_TEXT;
    uint16_t invalid_required = (uint16_t)(strlen(invalid_expected) + 1u);

    if (NMEA_GGA_RenderLine("$GPGGA,123519*", destination,
                            (uint16_t)(invalid_required - 1u), &length) !=
        NMEA_GGA_INVALID)
    {
      printf("FAIL error text overran a short buffer\n");
      failures++;
    }
    if (NMEA_GGA_RenderLine("$GPGGA,123519*", destination, invalid_required, &length) !=
        NMEA_GGA_INVALID)
    {
      printf("FAIL error text did not fit its exact buffer\n");
      failures++;
    }
    if (strcmp(destination, invalid_expected) != 0)
    {
      printf("FAIL error text was \"%s\"\n", destination);
      failures++;
    }
  }
}

typedef struct
{
  unsigned char head[8];
  char line[NMEA_GGA_LINE_CAPACITY];
  unsigned char tail[8];
} GuardedLine;

static void check_no_overflow_around_destination(void)
{
  static const char *const sentences[] =
  {
    GGA_FIX_NORTH_EAST,                        /* shortest line */
    "$GPGGA,123521,,,,,0*60",                 /* longest line */
    "$GPGGA,123519*"                           /* error line */
  };
  GuardedLine guarded;
  uint16_t length = 0u;
  size_t i;
  size_t s;

  for (s = 0u; s < (sizeof(sentences) / sizeof(sentences[0])); s++)
  {
    memset(&guarded, 0xA5, sizeof(guarded));
    (void)NMEA_GGA_RenderLine(sentences[s], guarded.line,
                              (uint16_t)sizeof(guarded.line), &length);
    for (i = 0u; i < sizeof(guarded.head); i++)
    {
      if (guarded.head[i] != 0xA5u)
      {
        printf("FAIL render wrote before the destination\n");
        failures++;
        return;
      }
    }
    for (i = 0u; i < sizeof(guarded.tail); i++)
    {
      if (guarded.tail[i] != 0xA5u)
      {
        printf("FAIL render wrote after the destination\n");
        failures++;
        return;
      }
    }
  }
}

int main(void)
{
  size_t i;

  for (i = 0u; i < (sizeof(render_cases) / sizeof(render_cases[0])); i++)
  {
    check_render(&render_cases[i]);
  }
  check_null_arguments();
  check_capacity_bound();
  check_no_overflow_around_destination();

  if (failures != 0u)
  {
    printf("%u failure(s)\n", failures);
    return 1;
  }

  printf("nmea_gga: %u render cases passed\n",
         (unsigned int)(sizeof(render_cases) / sizeof(render_cases[0])));
  return 0;
}
