/**
 * @file form_parser.c
 * @brief URL-encoded form data parser implementation
 */

#include "form_parser.h"
#include <string.h>
#include <ctype.h>

/**
 * @brief URL decode a single character from hex
 */
static char hex_decode(const char *hex)
{
    char result = 0;
    for (int i = 0; i < 2; i++) {
        char c = hex[i];
        result *= 16;
        if (c >= '0' && c <= '9') {
            result += c - '0';
        } else if (c >= 'A' && c <= 'F') {
            result += c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            result += c - 'a' + 10;
        }
    }
    return result;
}

/**
 * @brief URL decode a string
 */
static size_t url_decode(const char *src, size_t src_len, char *dst, size_t dst_len)
{
    size_t i = 0, j = 0;
    
    while (i < src_len && j < dst_len - 1) {
        if (src[i] == '%' && i + 2 < src_len) {
            dst[j++] = hex_decode(&src[i + 1]);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    
    dst[j] = '\0';
    return j;
}

int form_parser_parse(const char *data, size_t len,
                       form_field_t *fields, int max_fields)
{
    if (data == NULL || fields == NULL || max_fields <= 0) {
        return -1;
    }
    
    if (len == 0) {
        len = strlen(data);
    }
    
    int field_count = 0;
    size_t pos = 0;
    
    memset(fields, 0, sizeof(form_field_t) * max_fields);
    
    while (pos < len && field_count < max_fields) {
        /* Find end of this key=value pair */
        size_t end = pos;
        while (end < len && data[end] != '&') {
            end++;
        }
        
        /* Find '=' separator */
        size_t sep = pos;
        while (sep < end && data[sep] != '=') {
            sep++;
        }
        
        /* Extract and decode key */
        size_t key_len = (sep < end) ? (sep - pos) : (end - pos);
        url_decode(&data[pos], key_len, 
                   fields[field_count].key, 
                   sizeof(fields[field_count].key));
        
        /* Extract and decode value (if present) */
        if (sep < end) {
            size_t val_len = end - sep - 1;
            url_decode(&data[sep + 1], val_len,
                       fields[field_count].value,
                       sizeof(fields[field_count].value));
        } else {
            fields[field_count].value[0] = '\0';
        }
        
        field_count++;
        
        /* Move to next pair */
        pos = end + 1;
    }
    
    return field_count;
}

const char* form_parser_get_value(const form_field_t *fields, int count,
                                   const char *key)
{
    if (fields == NULL || key == NULL) {
        return NULL;
    }
    
    for (int i = 0; i < count; i++) {
        if (strcmp(fields[i].key, key) == 0) {
            return fields[i].value;
        }
    }
    
    return NULL;
}

bool form_parser_has_key(const form_field_t *fields, int count,
                          const char *key)
{
    return (form_parser_get_value(fields, count, key) != NULL);
}
