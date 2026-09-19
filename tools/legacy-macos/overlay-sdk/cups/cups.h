#ifndef _POWERFOX_CUPS_SHIM_
#define _POWERFOX_CUPS_SHIM_
#include_next <cups/cups.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cups_dinfo_s cups_dinfo_t;
typedef struct cups_size_s {
  char media[128];
  char source[64];
  char type[64];
  int width;
  int length;
  int bottom;
  int left;
  int right;
  int top;
} cups_size_t;
typedef int (*cups_dest_cb_t)(void* user_data, unsigned flags, cups_dest_t* dest);
enum { CUPS_DEST_FLAGS_NONE = 0, CUPS_DEST_FLAGS_MORE = 1,
       CUPS_DEST_FLAGS_ERROR = 2, CUPS_DEST_FLAGS_REMOVED = 4,
       CUPS_DEST_FLAGS_DISCONNECTED = 8 };
enum { CUPS_MEDIA_FLAGS_DEFAULT = 0, CUPS_MEDIA_FLAGS_BORDERLESS = 1,
       CUPS_MEDIA_FLAGS_DUPLEX = 2, CUPS_MEDIA_FLAGS_EXACT = 4 };
#define CUPS_MEDIA_READY "media-ready"
#define CUPS_PRINT_COLOR_MODE "print-color-mode"
#define CUPS_PRINT_COLOR_MODE_AUTO "auto"
#define CUPS_PRINT_COLOR_MODE_COLOR "color"
#define CUPS_PRINT_COLOR_MODE_MONOCHROME "monochrome"
#define CUPS_SIDES "sides"
#define CUPS_SIDES_ONE_SIDED "one-sided"
#define CUPS_SIDES_TWO_SIDED_PORTRAIT "two-sided-long-edge"
#define CUPS_SIDES_TWO_SIDED_LANDSCAPE "two-sided-short-edge"
#define CUPS_FINISHINGS "finishings"
#define CUPS_JOB_HOLD_UNTIL "job-hold-until"
#define CUPS_JOB_NAME "job-name"
#define CUPS_JOB_PRIORITY "job-priority"
#define CUPS_MEDIA "media"
#define CUPS_MEDIA_SOURCE "media-source"
#define CUPS_NUMBER_UP "number-up"
#define CUPS_ORIENTATION "orientation-requested"
#define CUPS_PRINT_QUALITY "print-quality"
#define CUPS_RESOLUTION "printer-resolution"
#define IPP_OP_GET_PRINTER_ATTRIBUTES ((ipp_op_t)0x000B)
extern int cupsCheckDestSupported(http_t* http, cups_dest_t* dest,
                                  cups_dinfo_t* info, const char* option,
                                  const char* value);
extern http_t* cupsConnectDest(cups_dest_t* dest, unsigned flags,
                               int msec, int* cancel, char* resource,
                               size_t resourcesize, cups_dest_cb_t cb,
                               void* user_data);
extern int cupsCopyDest(cups_dest_t* dest, int num_dests,
                          cups_dest_t** dests);
extern cups_dinfo_t* cupsCopyDestInfo(http_t* http, cups_dest_t* dest);
extern void cupsFreeDestInfo(cups_dinfo_t* info);
extern int cupsGetDestMediaByName(http_t* http, cups_dest_t* dest,
                                  cups_dinfo_t* info, const char* name,
                                  unsigned flags, cups_size_t* size);
extern int cupsGetDestMediaDefault(http_t* http, cups_dest_t* dest,
                                   cups_dinfo_t* info, unsigned flags,
                                   cups_size_t* size);
extern int cupsGetDestMediaCount(http_t* http, cups_dest_t* dest,
                                 cups_dinfo_t* info, unsigned flags);
extern int cupsGetDestMediaByIndex(http_t* http, cups_dest_t* dest,
                                   cups_dinfo_t* info, int index,
                                   unsigned flags, cups_size_t* size);
extern cups_dest_t* cupsFindDest(http_t* http, cups_dest_t* dests,
                                 int num_dests, const char* name,
                                 const char* instance);
extern cups_dest_t* cupsFindDestDefault(http_t* http, cups_dest_t* dests,
                                        int num_dests);
extern const char* cupsLocalizeDestMedia(http_t* http, cups_dest_t* dest,
                                         cups_dinfo_t* info, unsigned flags,
                                         cups_size_t* size);
extern int httpAddrPort(const http_addr_t* addr);
extern http_addr_t* httpGetAddress(http_t* http);
extern int ippGetCount(ipp_attribute_t* attr);
extern const char* ippGetString(ipp_attribute_t* attr, int element,
                                const char** language);
extern int cupsEnumDests(unsigned mgmt, int msec, int* cancel,
                         cups_ptype_t type, cups_ptype_t mask,
                         cups_dest_cb_t cb, void* user_data);
extern int cupsGetDestCount(cups_dest_t* dests);
extern cups_dest_t* cupsGetNamedDest(http_t* http, const char* name,
                                     const char* instance);

#ifdef __cplusplus
}
#endif
#endif
