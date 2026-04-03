/**
 * @file form_parser.h
 * @brief URL-encoded form data parser
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of form fields
 */
#define FORM_MAX_FIELDS     32

/**
 * @brief Maximum key/value length
 */
#define FORM_MAX_KEY_LEN    64
#define FORM_MAX_VAL_LEN    512

/**
 * @brief Form field structure
 */
typedef struct {
    char key[FORM_MAX_KEY_LEN];
    char value[FORM_MAX_VAL_LEN];
} form_field_t;

/**
 * @brief Parse URL-encoded form data
 * 
 * Parses application/x-www-form-urlencoded data into key-value pairs.
 * Handles URL decoding automatically.
 * 
 * Example input: "ssid=MyWiFi&password=secret123"
 * 
 * @param data      Raw form data
 * @param len       Data length (0 for strlen)
 * @param fields    Output array of fields
 * @param max_fields    Size of fields array
 * @return Number of fields parsed, or -1 on error
 */
int form_parser_parse(const char *data, size_t len,
                       form_field_t *fields, int max_fields);

/**
 * @brief Get field value by key
 * 
 * @param fields    Parsed fields array
 * @param count     Number of fields
 * @param key       Key to search for
 * @return Pointer to value, or NULL if not found
 */
const char* form_parser_get_value(const form_field_t *fields, int count,
                                   const char *key);

/**
 * @brief Check if field exists
 * 
 * @param fields    Parsed fields array
 * @param count     Number of fields
 * @param key       Key to search for
 * @return true if key exists
 */
bool form_parser_has_key(const form_field_t *fields, int count,
                          const char *key);

#ifdef __cplusplus
}
#endif
