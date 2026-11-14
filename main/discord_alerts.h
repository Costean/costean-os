#ifndef DISCORD_ALERTS_H_
#define DISCORD_ALERTS_H_

#include <stdbool.h>
#include <stddef.h>

typedef struct GlobalState GlobalState;

void discord_alerts_start(GlobalState *gs);
void discord_alerts_reload(void);
void discord_alerts_notify_block_found(double diff, double network_diff);
void discord_alerts_notify_best_diff(double diff, double network_diff);
bool discord_alerts_send_test(void);
void discord_webhook_preview(const char *url, char *out, size_t out_len);
bool discord_webhook_is_configured(void);

#endif
