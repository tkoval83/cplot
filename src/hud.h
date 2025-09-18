/**
 * @file hud.h
 * @brief Вивід HUD стану пристрою AxiDraw у CLI.
 */
#ifndef CPLOT_HUD_H
#define CPLOT_HUD_H

#include <stdbool.h>

#include "axidraw.h"
#include "axistate.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Скинути кеш HUD (примусити наступний рендер вивести таблицю).
 */
void hud_reset (void);

/**
 * @brief Оновити джерела даних для HUD.
 *
 * Усі вхідні вказівники мають бути життєздатними, поки HUD активний. Будь-який
 * параметр може бути NULL (у такому разі відповідні значення відображаються як
 * невідомі «--»).
 */
void hud_set_sources (
    const axidraw_device_t *device,
    const axidraw_settings_t *settings,
    const config_t *cfg,
    const char *model);

/**
 * @brief Відобразити HUD, якщо змінилися значення або задано primus.
 *
 * @param state Знімок стану (може бути NULL для автоматичного axistate_get()).
 * @param force true → рендерити незалежно від змін.
 * @return true, якщо HUD було відтворено.
 */
bool hud_render (const axistate_t *state, bool force);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_HUD_H */
