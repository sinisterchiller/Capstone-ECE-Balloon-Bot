/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    nmea_gga.c
  * @brief   Turns one NMEA GGA sentence into a human-readable GPS trace line.
  *
  * Everything here runs on CPU1 in the main loop, on a line already copied out
  * of the receiver state: no interrupt and no USART2 access. The sentence is
  * decoded with integer arithmetic only, so the line can be rendered without
  * floating-point printf support and within fixed buffers.
  *
  * Three outcomes share this renderer: a fix is printed as decimal degrees and
  * MSL altitude, a checksum-valid GGA that reports fix quality 0 is summarised
  * as a status line carrying the GGA satellite count, and an unusable sentence
  * gets its own error line, so a missing position is never silent.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "nmea_gga.h"

#include <string.h>

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* GGA field indices, counting the talker+address field ("GPGGA") as field 0. */
#define NMEA_GGA_FIELD_LATITUDE            2u
#define NMEA_GGA_FIELD_LATITUDE_HEMISPHERE 3u
#define NMEA_GGA_FIELD_LONGITUDE           4u
#define NMEA_GGA_FIELD_LONGITUDE_HEMISPHERE 5u
#define NMEA_GGA_FIELD_FIX_QUALITY         6u
#define NMEA_GGA_FIELD_SATELLITES_USED     7u
#define NMEA_GGA_FIELD_ALTITUDE            9u
#define NMEA_GGA_FIELD_ALTITUDE_UNIT       10u

/* Status line for a checksum-valid GGA without a position fix. The satellite
   count is GGA field 7, the satellites used in the solution - not the GSV
   satellites in view, which a receiver reports separately. */
#define NMEA_GGA_NO_FIX_PREFIX        "GPS STATUS: No position fix yet ("
#define NMEA_GGA_NO_FIX_COUNT_SUFFIX  " satellites used)"
#define NMEA_GGA_NO_FIX_NO_COUNT      "satellite count unavailable)"
#define NMEA_GGA_NO_FIX_SUFFIX        ". Check antenna and sky view."

/* Error line for a GGA whose checksum or fields cannot be trusted. It names no
   other line: gnss_vcp.c prints the faulty sentence ahead of it only when its
   raw trace switch is on. */
#define NMEA_GGA_INVALID_TEXT \
  "GPS ERROR: Unusable GGA (checksum or field format); no position decoded."

/* Size of the copied altitude text, NUL included. NMEA fields cannot exceed
   the 100-byte sentence buffer, so any realistic altitude fits. */
#define NMEA_GGA_ALTITUDE_CAPACITY 16u
/* USER CODE END PD */

/* Private types -------------------------------------------------------------*/
/* USER CODE BEGIN PT */
/**
  * @brief One comma-separated field of a sentence.
  */
typedef struct
{
  const char *text;
  uint16_t length;
} NmeaGgaField;

/**
  * @brief Decoded position, kept in fixed-point so no float is involved.
  */
typedef struct
{
  uint32_t latitude_micro_degrees;
  uint32_t longitude_micro_degrees;
  uint8_t latitude_negative;
  uint8_t longitude_negative;
  char altitude[NMEA_GGA_ALTITUDE_CAPACITY];
} NmeaGgaFix;
/* USER CODE END PT */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static uint8_t NMEA_GGA_IsGga(const char *sentence);
static const char *NMEA_GGA_ChecksumEnd(const char *sentence);
static uint8_t NMEA_GGA_HexDigit(char character);
static uint8_t NMEA_GGA_GetField(const char *fields, const char *fields_end, uint8_t index,
                                 NmeaGgaField *field);
static uint8_t NMEA_GGA_ParseUnsigned(NmeaGgaField field, uint32_t limit, uint32_t *value);
static uint8_t NMEA_GGA_ParseCoordinate(NmeaGgaField coordinate, NmeaGgaField hemisphere,
                                        uint8_t degree_digits, char positive, char negative,
                                        uint32_t *micro_degrees, uint8_t *is_negative);
static uint8_t NMEA_GGA_ParseAltitude(NmeaGgaField altitude, NmeaGgaField unit,
                                      char *destination, uint16_t capacity);
static NmeaGgaStatus NMEA_GGA_ParseFix(const char *fields, const char *fields_end,
                                       NmeaGgaFix *fix);
static uint8_t NMEA_GGA_Append(char *destination, uint16_t capacity, uint16_t *length,
                               const char *text, uint16_t text_length);
static uint8_t NMEA_GGA_AppendLiteral(char *destination, uint16_t capacity, uint16_t *length,
                                      const char *literal);
static uint8_t NMEA_GGA_AppendDegrees(char *destination, uint16_t capacity, uint16_t *length,
                                      uint32_t micro_degrees, uint8_t is_negative);
static uint8_t NMEA_GGA_AppendNumber(char *destination, uint16_t capacity, uint16_t *length,
                                     uint32_t value);
static uint8_t NMEA_GGA_AppendNoFixLine(const char *fields, const char *fields_end,
                                        char *destination, uint16_t capacity, uint16_t *length);
static NmeaGgaStatus NMEA_GGA_RenderInvalidLine(char *destination, uint16_t capacity,
                                                uint16_t *length);
/* USER CODE END PFP */

/* Exported functions --------------------------------------------------------*/
NmeaGgaStatus NMEA_GGA_RenderLine(const char *sentence, char *destination, uint16_t capacity,
                                  uint16_t *length)
{
  NmeaGgaFix fix;
  const char *fields_end;
  NmeaGgaStatus status;
  uint16_t written = 0u;

  if (length != NULL)
  {
    *length = 0u;
  }

  if ((sentence == NULL) || (destination == NULL) || (capacity == 0u) || (length == NULL))
  {
    return NMEA_GGA_INVALID;
  }

  destination[0] = '\0';

  if (NMEA_GGA_IsGga(sentence) == 0u)
  {
    return NMEA_GGA_NOT_GGA;
  }

  fields_end = NMEA_GGA_ChecksumEnd(sentence);
  if (fields_end == NULL)
  {
    /* Corrupt framing: no field can be trusted, and no position may be shown. */
    return NMEA_GGA_RenderInvalidLine(destination, capacity, length);
  }

  status = NMEA_GGA_ParseFix(sentence + 1u, fields_end, &fix);
  if (status == NMEA_GGA_INVALID)
  {
    return NMEA_GGA_RenderInvalidLine(destination, capacity, length);
  }

  if (status == NMEA_GGA_NO_FIX)
  {
    if (NMEA_GGA_AppendNoFixLine(sentence + 1u, fields_end, destination, capacity,
                                 &written) == 0u)
    {
      destination[0] = '\0';
      return NMEA_GGA_INVALID;
    }
    *length = written;
    return NMEA_GGA_NO_FIX;
  }
  if (status != NMEA_GGA_FIX)
  {
    return status;
  }

  if ((NMEA_GGA_AppendLiteral(destination, capacity, &written, "GPS lat=") == 0u) ||
      (NMEA_GGA_AppendDegrees(destination, capacity, &written, fix.latitude_micro_degrees,
                              fix.latitude_negative) == 0u) ||
      (NMEA_GGA_AppendLiteral(destination, capacity, &written, " lon=") == 0u) ||
      (NMEA_GGA_AppendDegrees(destination, capacity, &written, fix.longitude_micro_degrees,
                              fix.longitude_negative) == 0u) ||
      (NMEA_GGA_AppendLiteral(destination, capacity, &written, " altitude=") == 0u) ||
      (NMEA_GGA_Append(destination, capacity, &written, fix.altitude,
                       (uint16_t)strlen(fix.altitude)) == 0u) ||
      (NMEA_GGA_AppendLiteral(destination, capacity, &written, " m") == 0u))
  {
    destination[0] = '\0';
    return NMEA_GGA_INVALID;
  }

  *length = written;
  return NMEA_GGA_FIX;
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Tells whether a sentence is a GGA from the GP or GN talker.
  */
static uint8_t NMEA_GGA_IsGga(const char *sentence)
{
  return ((strncmp(sentence, "$GPGGA,", 7u) == 0) ||
          (strncmp(sentence, "$GNGGA,", 7u) == 0)) ? 1u : 0u;
}

/**
  * @brief  Verifies the trailing checksum and locates the field area.
  * @param  sentence NUL-terminated sentence
  * @return pointer to the '*', so fields are [sentence + 1, result), or NULL
  *         when the checksum is missing, malformed or wrong, or when anything
  *         other than CR/LF follows it.
  */
static const char *NMEA_GGA_ChecksumEnd(const char *sentence)
{
  const char *star = strchr(sentence, '*');
  const char *cursor;
  uint8_t checksum = 0u;
  uint8_t high;
  uint8_t low;

  if (star == NULL)
  {
    return NULL;
  }

  for (cursor = sentence + 1u; cursor < star; cursor++)
  {
    checksum ^= (uint8_t)*cursor;
  }

  if ((star[1] == '\0') || (star[2] == '\0'))
  {
    return NULL; /* the two checksum digits must exist */
  }

  high = NMEA_GGA_HexDigit(star[1]);
  low = NMEA_GGA_HexDigit(star[2]);
  if ((high > 0x0fu) || (low > 0x0fu))
  {
    return NULL;
  }
  if (checksum != (uint8_t)((high << 4u) | low))
  {
    return NULL;
  }

  cursor = star + 3u;
  if (*cursor == '\r')
  {
    cursor++;
  }
  if (*cursor == '\n')
  {
    cursor++;
  }
  if (*cursor != '\0')
  {
    return NULL;
  }

  return star;
}

/**
  * @brief  Hexadecimal value of one checksum digit.
  * @return 0..15, or 0xFF when the character is not a hex digit.
  */
static uint8_t NMEA_GGA_HexDigit(char character)
{
  if ((character >= '0') && (character <= '9'))
  {
    return (uint8_t)(character - '0');
  }
  if ((character >= 'A') && (character <= 'F'))
  {
    return (uint8_t)((character - 'A') + 10);
  }
  if ((character >= 'a') && (character <= 'f'))
  {
    return (uint8_t)((character - 'a') + 10);
  }
  return 0xffu;
}

/**
  * @brief  Extracts one comma-separated field.
  * @param  fields first character of field 0 (the talker+address field)
  * @param  fields_end the '*' ending the field area
  * @param  index field to extract, 0 based
  * @param  field receives text and length; fields may be empty
  * @return 1 when the field exists, 0 when the sentence ends before it
  */
static uint8_t NMEA_GGA_GetField(const char *fields, const char *fields_end, uint8_t index,
                                 NmeaGgaField *field)
{
  const char *cursor = fields;
  uint8_t current = 0u;

  while (current < index)
  {
    while ((cursor < fields_end) && (*cursor != ','))
    {
      cursor++;
    }
    if (cursor >= fields_end)
    {
      return 0u;
    }
    cursor++;
    current++;
  }

  field->text = cursor;
  while ((cursor < fields_end) && (*cursor != ','))
  {
    cursor++;
  }
  field->length = (uint16_t)(cursor - field->text);
  return 1u;
}

/**
  * @brief  Parses a decimal field of up to three digits.
  * @param  field text to parse, which must not be empty
  * @param  limit largest value accepted, e.g. 99 for a two-digit NMEA count
  * @param  value receives the number on success
  * @return 1 when the field is a decimal number no greater than limit
  */
static uint8_t NMEA_GGA_ParseUnsigned(NmeaGgaField field, uint32_t limit, uint32_t *value)
{
  uint32_t parsed = 0u;
  uint16_t i;

  if (field.length == 0u)
  {
    return 0u;
  }

  for (i = 0u; i < field.length; i++)
  {
    char character = field.text[i];

    if ((character < '0') || (character > '9'))
    {
      return 0u;
    }
    parsed = (parsed * 10u) + (uint32_t)(character - '0');
    if (parsed > limit)
    {
      return 0u;
    }
  }

  *value = parsed;
  return 1u;
}

/**
  * @brief  Validates a latitude or longitude and converts it to micro-degrees.
  * @param  coordinate ddmm.mmmm (latitude) or dddmm.mmmm (longitude)
  * @param  hemisphere single N/S or E/W character
  * @param  degree_digits 2 for latitude, 3 for longitude
  * @param  positive hemisphere character for a positive value
  * @param  negative hemisphere character for a negative value
  * @param  micro_degrees receives the absolute value in 1e-6 degrees
  * @param  is_negative receives 1 for the negative hemisphere, else 0
  * @return 1 when the field pair is a valid coordinate
  */
static uint8_t NMEA_GGA_ParseCoordinate(NmeaGgaField coordinate, NmeaGgaField hemisphere,
                                        uint8_t degree_digits, char positive, char negative,
                                        uint32_t *micro_degrees, uint8_t *is_negative)
{
  uint32_t degrees = 0u;
  uint32_t minutes = 0u;
  uint32_t fraction = 0u;
  uint8_t fraction_digits = 0u;
  uint16_t integer_length = 0u;
  uint16_t i;
  uint32_t degree_limit = (degree_digits == 2u) ? 90u : 180u;

  if ((hemisphere.length != 1u) ||
      ((hemisphere.text[0] != positive) && (hemisphere.text[0] != negative)))
  {
    return 0u;
  }

  while ((integer_length < coordinate.length) && (coordinate.text[integer_length] != '.'))
  {
    integer_length++;
  }
  if (integer_length != (uint16_t)(degree_digits + 2u))
  {
    return 0u; /* not ddmm.mmmm / dddmm.mmmm */
  }
  if (integer_length >= coordinate.length)
  {
    return 0u; /* fraction digits are required */
  }

  for (i = 0u; i < degree_digits; i++)
  {
    char character = coordinate.text[i];

    if ((character < '0') || (character > '9'))
    {
      return 0u;
    }
    degrees = (degrees * 10u) + (uint32_t)(character - '0');
  }

  for (i = 0u; i < 2u; i++)
  {
    char character = coordinate.text[degree_digits + i];

    if ((character < '0') || (character > '9'))
    {
      return 0u;
    }
    minutes = (minutes * 10u) + (uint32_t)(character - '0');
  }

  for (i = (uint16_t)(integer_length + 1u); i < coordinate.length; i++)
  {
    char character = coordinate.text[i];

    if ((character < '0') || (character > '9'))
    {
      return 0u;
    }
    if (fraction_digits < 6u)
    {
      fraction = (fraction * 10u) + (uint32_t)(character - '0');
      fraction_digits++;
    }
  }
  if (fraction_digits == 0u)
  {
    return 0u;
  }
  while (fraction_digits < 6u)
  {
    fraction *= 10u;
    fraction_digits++;
  }

  if (minutes >= 60u)
  {
    return 0u;
  }
  if (degrees > degree_limit)
  {
    return 0u;
  }
  if ((degrees == degree_limit) && ((minutes != 0u) || (fraction != 0u)))
  {
    return 0u;
  }

  /* 1 minute is 1/60 degree; +30 rounds the division to the nearest 1e-6. */
  *micro_degrees = (degrees * 1000000u) +
                   (((minutes * 1000000u) + fraction + 30u) / 60u);
  *is_negative = (hemisphere.text[0] == negative) ? 1u : 0u;
  return 1u;
}

/**
  * @brief  Validates the altitude field and copies its text verbatim.
  * @param  altitude GGA altitude above mean sea level, e.g. "545.4"
  * @param  unit altitude unit field, which must be "M"
  * @param  destination receives the text, NUL-terminated
  * @param  capacity size of destination in bytes
  * @return 1 when the field is a decimal number with an optional sign and unit
  */
static uint8_t NMEA_GGA_ParseAltitude(NmeaGgaField altitude, NmeaGgaField unit,
                                      char *destination, uint16_t capacity)
{
  uint8_t integer_digits = 0u;
  uint8_t fraction_digits = 0u;
  uint8_t seen_point = 0u;
  uint16_t i;

  if ((unit.length != 1u) || (unit.text[0] != 'M'))
  {
    return 0u;
  }
  if ((altitude.length == 0u) || (altitude.length >= capacity))
  {
    return 0u;
  }

  for (i = 0u; i < altitude.length; i++)
  {
    char character = altitude.text[i];

    if ((character >= '0') && (character <= '9'))
    {
      if (seen_point != 0u)
      {
        fraction_digits++;
      }
      else
      {
        integer_digits++;
      }
      continue;
    }
    if (((character == '-') || (character == '+')) && (i == 0u))
    {
      continue;
    }
    if ((character == '.') && (seen_point == 0u))
    {
      seen_point = 1u;
      continue;
    }
    return 0u;
  }

  if ((integer_digits == 0u) || ((seen_point != 0u) && (fraction_digits == 0u)))
  {
    return 0u;
  }

  memcpy(destination, altitude.text, altitude.length);
  destination[altitude.length] = '\0';
  return 1u;
}

/**
  * @brief  Validates the position fields of a GGA sentence.
  * @param  fields first character of field 0
  * @param  fields_end the '*' ending the field area
  * @param  fix receives the decoded position on NMEA_GGA_FIX
  * @return NMEA_GGA_FIX, NMEA_GGA_NO_FIX, or NMEA_GGA_INVALID
  */
static NmeaGgaStatus NMEA_GGA_ParseFix(const char *fields, const char *fields_end,
                                       NmeaGgaFix *fix)
{
  NmeaGgaField quality_text;
  NmeaGgaField latitude;
  NmeaGgaField latitude_hemisphere;
  NmeaGgaField longitude;
  NmeaGgaField longitude_hemisphere;
  NmeaGgaField altitude;
  NmeaGgaField altitude_unit;
  uint32_t quality;
  uint32_t latitude_micro_degrees;
  uint32_t longitude_micro_degrees;
  uint8_t latitude_negative;
  uint8_t longitude_negative;

  if (NMEA_GGA_GetField(fields, fields_end, NMEA_GGA_FIELD_FIX_QUALITY, &quality_text) == 0u)
  {
    return NMEA_GGA_INVALID;
  }
  if (NMEA_GGA_ParseUnsigned(quality_text, 999u, &quality) == 0u)
  {
    return NMEA_GGA_INVALID;
  }
  if (quality == 0u)
  {
    /* No fix: the position and altitude fields are legitimately empty, so they
       are not required before printing the no-fix status line. */
    return NMEA_GGA_NO_FIX;
  }

  if ((NMEA_GGA_GetField(fields, fields_end, NMEA_GGA_FIELD_LATITUDE, &latitude) == 0u) ||
      (NMEA_GGA_GetField(fields, fields_end, NMEA_GGA_FIELD_LATITUDE_HEMISPHERE,
                         &latitude_hemisphere) == 0u) ||
      (NMEA_GGA_GetField(fields, fields_end, NMEA_GGA_FIELD_LONGITUDE, &longitude) == 0u) ||
      (NMEA_GGA_GetField(fields, fields_end, NMEA_GGA_FIELD_LONGITUDE_HEMISPHERE,
                         &longitude_hemisphere) == 0u) ||
      (NMEA_GGA_GetField(fields, fields_end, NMEA_GGA_FIELD_ALTITUDE, &altitude) == 0u) ||
      (NMEA_GGA_GetField(fields, fields_end, NMEA_GGA_FIELD_ALTITUDE_UNIT, &altitude_unit) == 0u))
  {
    return NMEA_GGA_INVALID;
  }

  if (NMEA_GGA_ParseCoordinate(latitude, latitude_hemisphere, 2u, 'N', 'S',
                               &latitude_micro_degrees, &latitude_negative) == 0u)
  {
    return NMEA_GGA_INVALID;
  }
  if (NMEA_GGA_ParseCoordinate(longitude, longitude_hemisphere, 3u, 'E', 'W',
                               &longitude_micro_degrees, &longitude_negative) == 0u)
  {
    return NMEA_GGA_INVALID;
  }
  if (NMEA_GGA_ParseAltitude(altitude, altitude_unit, fix->altitude,
                             (uint16_t)sizeof(fix->altitude)) == 0u)
  {
    return NMEA_GGA_INVALID;
  }

  fix->latitude_micro_degrees = latitude_micro_degrees;
  fix->longitude_micro_degrees = longitude_micro_degrees;
  fix->latitude_negative = latitude_negative;
  fix->longitude_negative = longitude_negative;
  return NMEA_GGA_FIX;
}

/**
  * @brief  Appends bounded text to the output line.
  * @return 1 on success, 0 when the text would not fit with its NUL
  */
static uint8_t NMEA_GGA_Append(char *destination, uint16_t capacity, uint16_t *length,
                               const char *text, uint16_t text_length)
{
  if (((uint32_t)*length + (uint32_t)text_length) >= (uint32_t)capacity)
  {
    return 0u;
  }

  memcpy(&destination[*length], text, text_length);
  *length = (uint16_t)(*length + text_length);
  destination[*length] = '\0';
  return 1u;
}

/**
  * @brief  Appends a NUL-terminated literal to the output line.
  */
static uint8_t NMEA_GGA_AppendLiteral(char *destination, uint16_t capacity, uint16_t *length,
                                      const char *literal)
{
  return NMEA_GGA_Append(destination, capacity, length, literal,
                         (uint16_t)strlen(literal));
}

/**
  * @brief  Renders the status line for a GGA that has no position fix.
  * @param  fields first character of field 0
  * @param  fields_end the '*' ending the field area
  * @note   Reports the GGA satellite count when field 7 is a decimal number no
  *         greater than 99, and says the count is unavailable otherwise, so a
  *         receiver that omits the field never looks like one reporting zero
  *         satellites.
  * @return 1 on success, 0 when the line would not fit with its NUL
  */
static uint8_t NMEA_GGA_AppendNoFixLine(const char *fields, const char *fields_end,
                                        char *destination, uint16_t capacity, uint16_t *length)
{
  NmeaGgaField satellites_used;
  uint32_t count = 0u;
  uint8_t have_count = 0u;

  if ((NMEA_GGA_GetField(fields, fields_end, NMEA_GGA_FIELD_SATELLITES_USED,
                         &satellites_used) != 0u) &&
      (NMEA_GGA_ParseUnsigned(satellites_used, 99u, &count) != 0u))
  {
    have_count = 1u;
  }

  if (NMEA_GGA_AppendLiteral(destination, capacity, length, NMEA_GGA_NO_FIX_PREFIX) == 0u)
  {
    return 0u;
  }
  if (have_count != 0u)
  {
    if ((NMEA_GGA_AppendNumber(destination, capacity, length, count) == 0u) ||
        (NMEA_GGA_AppendLiteral(destination, capacity, length,
                                NMEA_GGA_NO_FIX_COUNT_SUFFIX) == 0u))
    {
      return 0u;
    }
  }
  else if (NMEA_GGA_AppendLiteral(destination, capacity, length,
                                  NMEA_GGA_NO_FIX_NO_COUNT) == 0u)
  {
    return 0u;
  }

  return NMEA_GGA_AppendLiteral(destination, capacity, length, NMEA_GGA_NO_FIX_SUFFIX);
}

/**
  * @brief  Renders the error line for a GGA that cannot be decoded.
  * @return NMEA_GGA_INVALID, with *length set when the line was produced and
  *         left at 0 when it does not fit the buffer
  */
static NmeaGgaStatus NMEA_GGA_RenderInvalidLine(char *destination, uint16_t capacity,
                                                uint16_t *length)
{
  uint16_t written = 0u;

  if (NMEA_GGA_AppendLiteral(destination, capacity, &written, NMEA_GGA_INVALID_TEXT) == 0u)
  {
    destination[0] = '\0';
    return NMEA_GGA_INVALID;
  }

  *length = written;
  return NMEA_GGA_INVALID;
}

/**
  * @brief  Appends an unsigned decimal number.
  */
static uint8_t NMEA_GGA_AppendNumber(char *destination, uint16_t capacity, uint16_t *length,
                                     uint32_t value)
{
  char digits[10];
  uint8_t digit_count = 0u;

  do
  {
    digits[digit_count] = (char)('0' + (value % 10u));
    value /= 10u;
    digit_count++;
  } while (value != 0u);

  while (digit_count > 0u)
  {
    digit_count--;
    if (NMEA_GGA_Append(destination, capacity, length, &digits[digit_count], 1u) == 0u)
    {
      return 0u;
    }
  }

  return 1u;
}

/**
  * @brief  Appends a coordinate as signed decimal degrees with six decimals.
  */
static uint8_t NMEA_GGA_AppendDegrees(char *destination, uint16_t capacity, uint16_t *length,
                                      uint32_t micro_degrees, uint8_t is_negative)
{
  char digits[10];
  char fraction[6];
  uint8_t digit_count = 0u;
  uint32_t whole = micro_degrees / 1000000u;
  uint32_t remainder = micro_degrees % 1000000u;
  uint8_t i;

  /* A zero coordinate prints without a sign instead of "-0.000000". */
  if ((is_negative != 0u) && (micro_degrees != 0u))
  {
    if (NMEA_GGA_Append(destination, capacity, length, "-", 1u) == 0u)
    {
      return 0u;
    }
  }

  do
  {
    digits[digit_count] = (char)('0' + (whole % 10u));
    whole /= 10u;
    digit_count++;
  } while (whole != 0u);

  while (digit_count > 0u)
  {
    digit_count--;
    if (NMEA_GGA_Append(destination, capacity, length, &digits[digit_count], 1u) == 0u)
    {
      return 0u;
    }
  }

  if (NMEA_GGA_Append(destination, capacity, length, ".", 1u) == 0u)
  {
    return 0u;
  }

  for (i = 0u; i < 6u; i++)
  {
    fraction[5u - i] = (char)('0' + (remainder % 10u));
    remainder /= 10u;
  }

  return NMEA_GGA_Append(destination, capacity, length, fraction, 6u);
}
