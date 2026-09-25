#ifndef JEFETCH_PA_H
#define JEFETCH_PA_H

#include <stddef.h>
#include <stdint.h>

typedef struct PaClient PaClient;
typedef struct PaRecord PaRecord;

PaClient *pa_connect(const char *app, char *err, size_t errn);
int pa_default_monitor(PaClient *c, char *out, size_t outn, char *err, size_t errn);
PaRecord *pa_record(PaClient *c, const char *device, unsigned rate, unsigned channels,
                    char *err, size_t errn);
void pa_client_free(PaClient *c);
long pa_read_chunk(PaRecord *r, uint8_t *out, size_t cap, const volatile int *stop,
                   char *err, size_t errn);
void pa_record_free(PaRecord *r);

#endif
