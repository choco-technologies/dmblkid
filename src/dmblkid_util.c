#include "dmblkid_internal.h"
#include <errno.h>
#include <string.h>

void dmblkid_set_filesystem(dmblkid_t* result, const char* type, const char* module, bool mountable)
{
    result->usage     = dmblkid_usage_filesystem;
    result->type      = type;
    result->module    = module;
    result->mountable = mountable && module != NULL;
}

int dmblkid_set_string(char** field, const char* value)
{
    char* copy = Dmod_StrDup(value);
    if (copy == NULL)
    {
        return -ENOMEM;
    }
    Dmod_Free(*field);
    *field = copy;
    return 0;
}

void dmblkid_hex(char* out, uint64_t value, unsigned digits, bool upper)
{
    char ten = upper ? 'A' : 'a';
    for (unsigned i = 0; i < digits; i++)
    {
        unsigned nibble = (unsigned)(value >> (4u * (digits - 1u - i))) & 0xFu;
        out[i] = (char)((nibble < 10u) ? ('0' + nibble) : (ten + nibble - 10u));
    }
}

int dmblkid_set_serial(char** field, uint32_t serial)
{
    char text[10];
    dmblkid_hex(text, serial >> 16, 4, true);
    text[4] = '-';
    dmblkid_hex(text + 5, serial & 0xFFFFu, 4, true);
    text[9] = '\0';
    return dmblkid_set_string(field, text);
}

int dmblkid_set_label8(char** field, const uint8_t* label, size_t size)
{
    while (size > 0 && (label[size - 1] == ' ' || label[size - 1] == '\0'))
    {
        size--;
    }
    if (size == 0)
    {
        return 0;
    }
    char* text = Dmod_Malloc(size + 1u);
    if (text == NULL)
    {
        return -ENOMEM;
    }
    for (size_t i = 0; i < size; i++)
    {
        text[i] = (label[i] >= 0x20u && label[i] < 0x7Fu) ? (char)label[i] : '?';
    }
    text[size] = '\0';
    Dmod_Free(*field);
    *field = text;
    return 0;
}

/* Append one BMP code point as UTF-8; returns the number of bytes written (1-3). */
static size_t put_utf8(char* out, uint32_t code)
{
    if (code < 0x80u)
    {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800u)
    {
        out[0] = (char)(0xC0u | (code >> 6));
        out[1] = (char)(0x80u | (code & 0x3Fu));
        return 2;
    }
    out[0] = (char)(0xE0u | (code >> 12));
    out[1] = (char)(0x80u | ((code >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (code & 0x3Fu));
    return 3;
}

int dmblkid_set_label16(char** field, const uint8_t* label, size_t units)
{
    char* text = Dmod_Malloc(units * 3u + 1u);
    if (text == NULL)
    {
        return -ENOMEM;
    }
    size_t length = 0;
    for (size_t i = 0; i < units; i++)
    {
        uint32_t code = dmblkid_le16(label + 2u * i);
        if (code == 0)
        {
            break;
        }
        bool printable = code >= 0x20u && code != 0x7Fu && (code < 0xD800u || code > 0xDFFFu);
        length += put_utf8(text + length, printable ? code : '?');
    }
    text[length] = '\0';
    if (length == 0)
    {
        Dmod_Free(text);
        return 0;
    }
    Dmod_Free(*field);
    *field = text;
    return 0;
}
