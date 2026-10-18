#include "api/metadata_internal.h"
#include <string.h>

static bool catalogue(uint64_t value, const MediaPolicy *policy, bool *matches)
{
    static const char *const names[] = {"free", "premium", "", "shuffle", "commercial"};
    if (value >= sizeof(names) / sizeof(names[0])) return false;
    if (value == 2 || strcmp(policy->catalogue, names[value]) == 0) *matches = true;
    return true;
}

static bool country_list(MetadataSpan span, const MediaPolicy *policy, bool *matches)
{
    if (span.bytes > 512 || (span.bytes & 1) != 0) return false;
    *matches = false;
    for (size_t i = 0; i < span.bytes; i += 2) {
        if (span.data[i] < 'A' || span.data[i] > 'Z' ||
            span.data[i + 1] < 'A' || span.data[i + 1] > 'Z') return false;
        if (span.data[i] == (uint8_t)policy->country[0] &&
            span.data[i + 1] == (uint8_t)policy->country[1]) *matches = true;
    }
    return true;
}

MediaResult metadata_restriction(MetadataSpan span, const MediaPolicy *policy, bool *blocked)
{
    MetadataReader r = {span, MEDIA_OK, 0};
    uint32_t tag; pb_wire_type_t wire; uint64_t seen = 0;
    unsigned products = 0, countries = 0; bool matches = false, deny = false;
    while (metadata_next(&r, &tag, &wire)) {
        uint64_t value; MetadataSpan part;
        if (tag == 1) {
            if (wire == PB_WT_STRING) {
                if (!metadata_span(&r, wire, &part)) break;
                MetadataReader packed = {part, MEDIA_OK, 0};
                while (packed.span.bytes != 0) {
                    if (++products > 32) return MEDIA_LIMIT;
                    if (!metadata_uint(&packed, PB_WT_VARINT, &value)) return packed.result;
                    if (!catalogue(value, policy, &matches)) return MEDIA_RESTRICTED;
                }
            } else {
                if (++products > 32) return MEDIA_LIMIT;
                if (!metadata_uint(&r, wire, &value)) break;
                if (!catalogue(value, policy, &matches)) return MEDIA_RESTRICTED;
            }
        } else if (tag == 5) {
            if (++products > 32) return MEDIA_LIMIT;
            if (!metadata_span(&r, wire, &part)) break;
            if (part.bytes == 0 || part.bytes > 64) return MEDIA_MALFORMED;
            if (metadata_equal(part, policy->catalogue)) matches = true;
        } else if (tag == 2 || tag == 3) {
            bool found;
            if (++countries != 1 || !metadata_span(&r, wire, &part)) return MEDIA_MALFORMED;
            if (!country_list(part, policy, &found)) return MEDIA_MALFORMED;
            deny = tag == 2 ? !found : found;
        } else if (tag == 4) {
            if (!metadata_once(&r, &seen, tag) || !metadata_uint(&r, wire, &value)) break;
            if (value != 0) return MEDIA_RESTRICTED;
        } else return MEDIA_RESTRICTED;
    }
    if (r.result != MEDIA_OK) return r.result;
    if (countries != 1) return MEDIA_MALFORMED;
    *blocked = (products == 0 || matches) && deny;
    return MEDIA_OK;
}

static bool leap(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static MediaResult date_ms(MetadataSpan span, uint64_t *milliseconds)
{
    MetadataReader r = {span, MEDIA_OK, 0};
    uint32_t tag; pb_wire_type_t wire; uint64_t seen = 0;
    int fields[5] = {0, 1, 1, 0, 0};
    while (metadata_next(&r, &tag, &wire)) {
        uint64_t value;
        if (tag < 1 || tag > 5 || !metadata_once(&r, &seen, tag) ||
            !metadata_uint(&r, wire, &value)) return MEDIA_MALFORMED;
        if (value > 19998 || (value & 1) != 0) return MEDIA_MALFORMED;
        fields[tag - 1] = (int)(value / 2);
    }
    if (r.result != MEDIA_OK) return r.result;
    int year = fields[0], month = fields[1], day = fields[2];
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > lengths[month - 1] + (month == 2 && leap(year) ? 1 : 0) ||
        fields[3] > 23 || fields[4] > 59) return MEDIA_MALFORMED;
    /* Gregorian UTC calendar, bounded above so arithmetic cannot overflow. */
    int64_t previous = year - 1;
    int64_t days = previous * 365 + previous / 4 - previous / 100 + previous / 400 - 719162;
    for (int m = 1; m < month; ++m) days += lengths[m - 1] + (m == 2 && leap(year) ? 1 : 0);
    days += day - 1;
    /* Policy clocks are nonnegative Unix times; all earlier dates are live. */
    *milliseconds = days < 0 ? 0 : (uint64_t)((days * 24 + fields[3]) * 60 + fields[4]) * 60000U;
    return MEDIA_OK;
}

MediaResult metadata_availability(MetadataSpan span, const MediaPolicy *policy, bool *available)
{
    MetadataReader r = {span, MEDIA_OK, 0};
    uint32_t tag; pb_wire_type_t wire; bool matches = false, has_date = false;
    unsigned products = 0; uint64_t start = 0;
    while (metadata_next(&r, &tag, &wire)) {
        MetadataSpan part;
        if (tag == 1) {
            if (++products > 32) return MEDIA_LIMIT;
            if (!metadata_span(&r, wire, &part)) break;
            if (part.bytes == 0 || part.bytes > 64) return MEDIA_MALFORMED;
            if (metadata_equal(part, policy->catalogue)) matches = true;
        } else if (tag == 2) {
            if (has_date || !metadata_span(&r, wire, &part)) return MEDIA_MALFORMED;
            has_date = true;
            MediaResult result = date_ms(part, &start);
            if (result != MEDIA_OK) return result;
        } else return MEDIA_RESTRICTED;
    }
    if (r.result != MEDIA_OK) return r.result;
    if (!has_date) return MEDIA_MALFORMED;
    *available = (products == 0 || matches) && policy->utc_ms >= start;
    return MEDIA_OK;
}
