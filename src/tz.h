// Common time zones (IANA name -> POSIX rule) for the clock and night mode.
#pragma once

struct TzEntry { const char *name; const char *posix; };

static const TzEntry TZ_TABLE[] = {
  {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
  {"America/Detroit", "EST5EDT,M3.2.0,M11.1.0"},
  {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
  {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
  {"America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0"},
  {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
  {"America/Edmonton", "MST7MDT,M3.2.0,M11.1.0"},
  {"America/Phoenix", "MST7"},
  {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
  {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
  {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Pacific/Honolulu", "HST10"},
  {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"},
  {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
  {"America/Puerto_Rico", "AST4"},
  {"America/Mexico_City", "CST6"},
  {"America/Bogota", "<-05>5"},
  {"America/Lima", "<-05>5"},
  {"America/Sao_Paulo", "<-03>3"},
  {"America/Argentina/Buenos_Aires", "<-03>3"},
  {"America/Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
  {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"},
  {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
  {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Europe/Istanbul", "<+03>-3"},
  {"Europe/Moscow", "MSK-3"},
  {"Africa/Johannesburg", "SAST-2"},
  {"Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
  {"Africa/Lagos", "WAT-1"},
  {"Africa/Nairobi", "EAT-3"},
  {"Asia/Dubai", "<+04>-4"},
  {"Asia/Karachi", "PKT-5"},
  {"Asia/Kolkata", "IST-5:30"},
  {"Asia/Bangkok", "<+07>-7"},
  {"Asia/Singapore", "<+08>-8"},
  {"Asia/Hong_Kong", "HKT-8"},
  {"Asia/Shanghai", "CST-8"},
  {"Asia/Manila", "PST-8"},
  {"Asia/Seoul", "KST-9"},
  {"Asia/Tokyo", "JST-9"},
  {"Australia/Perth", "AWST-8"},
  {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
  {"Australia/Brisbane", "AEST-10"},
  {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
  {"UTC", "UTC0"},
};
static const int TZ_COUNT = sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]);

static inline const char *tzLookup(const char *name) {
  for (int i = 0; i < TZ_COUNT; i++)
    if (!strcmp(TZ_TABLE[i].name, name)) return TZ_TABLE[i].posix;
  return nullptr;
}
