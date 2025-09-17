/**
 * @file axistate.h
 * @brief Глобальний стан AxiDraw для спостереження.
 */
#ifndef AXISTATE_H
#define AXISTATE_H

#include <stdbool.h>
#include <time.h>

#include "ebb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct axistate {
    bool valid;              /**< Чи доступні дані. */
    bool snapshot_valid;     /**< Чи містить snapshot валідні поля. */
    struct timespec ts;      /**< Час оновлення (CLOCK_REALTIME). */
    char phase[16];          /**< Фаза (before/after/after_wait/...). */
    char action[64];         /**< Назва дії. */
    int command_rc;          /**< Код повернення основної команди. */
    int wait_rc;             /**< Результат очікування FIFO (0 якщо не застосовується). */
    ebb_status_snapshot_t snapshot; /**< Останній знімок стану контролера. */
} axistate_t;

void axistate_clear (void);
void axistate_update (
    const char *phase,
    const char *action,
    int command_rc,
    int wait_rc,
    const ebb_status_snapshot_t *snapshot);
bool axistate_get (axistate_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AXISTATE_H */
