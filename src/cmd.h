/**
 * @file cmd.h
 * @brief Фасади виконання підкоманд `print`, `device`, `config`, `fonts`, `version`.
 * @defgroup cmd Команди
 * @ingroup cli
 */
#ifndef CPLOT_CMD_H
#define CPLOT_CMD_H

#include "config.h"
#include "args.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Уніфікований тип коду результату команд.
 */
typedef int cmd_result_t;

/**
 * @brief Встановлює потік виводу для результатів (stdout за замовчуванням).
 * @param out Потік виводу (наприклад, для превʼю байтів PNG/SVG).
 */
void cmd_set_output (FILE *out);

/**
 * @brief Виконання друку на пристрій AxiDraw.
 * @param in_chars Вхідний текст.
 * @param in_len Довжина вхідного тексту.
 * @param markdown Чи інтерпретувати вхід як Markdown.
 * @param font_family Назва шрифтної родини Hershey.
 * @param font_size_pt Розмір шрифту у пунктах.
 * @param device_model Модель пристрою (профіль руху).
 * @param paper_w_mm Ширина паперу у мм.
 * @param paper_h_mm Висота паперу у мм.
 * @param margin_top_mm Верхнє поле у мм.
 * @param margin_right_mm Праве поле у мм.
 * @param margin_bottom_mm Нижнє поле у мм.
 * @param margin_left_mm Ліве поле у мм.
 * @param orientation Орієнтація сторінки.
 * @param fit_page Масштабувати вміст під сторінку.
 * @param dry_run Режим без фізичних дій.
 * @param verbose Детальні журнали.
 * @return 0 — успіх, інакше — код помилки.
 */
cmd_result_t cmd_print_execute (
    const char *in_chars,
    size_t in_len,
    bool markdown,
    const char *font_family,
    double font_size_pt,
    const char *device_model,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    bool fit_page,
    motion_profile_t motion_profile,
    bool dry_run,
    bool verbose);

/**
 * @brief Генерує превʼю векторного/растрового зображення без друку на пристрій.
 * @param in_chars Вхідний текст.
 * @param in_len Довжина вхідного тексту.
 * @param markdown Чи інтерпретувати вхід як Markdown.
 * @param font_family Назва шрифтної родини Hershey.
 * @param font_size_pt Розмір шрифту у пунктах.
 * @param device_model Модель пристрою (профіль руху).
 * @param paper_w_mm Ширина паперу у мм.
 * @param paper_h_mm Висота паперу у мм.
 * @param margin_top_mm Верхнє поле у мм.
 * @param margin_right_mm Праве поле у мм.
 * @param margin_bottom_mm Нижнє поле у мм.
 * @param margin_left_mm Ліве поле у мм.
 * @param orientation Орієнтація сторінки.
 * @param fit_page Масштабувати вміст під сторінку (як булеве ціле).
 * @param preview_png Якщо 1 — вивід PNG, інакше SVG.
 * @param verbose Детальні журнали.
 * @param out_bytes [out] Буфер згенерованих байтів.
 * @param out_len [out] Довжина буфера в байтах.
 * @return 0 — успіх, інакше — код помилки.
 */
cmd_result_t cmd_print_preview (
    const char *in_chars,
    size_t in_len,
    bool markdown,
    const char *font_family,
    double font_size_pt,
    const char *device_model,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    int fit_page,
    int preview_png,
    bool verbose,
    uint8_t **out_bytes,
    size_t *out_len);

/**
 * @brief Друк версії застосунку.
 * @param verbose Увімкнути докладні журнали.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_version_execute (bool verbose);

/**
 * @brief Друк інформації про доступні шрифти.
 * @param verbose Увімкнути докладні журнали.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_fonts_execute (bool verbose);

/**
 * @brief Друк переліку шрифтів або лише родин шрифтів.
 * @param families_only Якщо true — лише родини; false — усі шрифти.
 * @param verbose Увімкнути докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_font_list_execute (bool families_only, bool verbose);

/**
 * @brief Загальний обробник підкоманди `config`.
 * @param action Дія: show/reset/set (1/2/3).
 * @param set_pairs Парам-рядок для `--set`.
 * @param inout_cfg Конфіг для читання/запису (NULL — авто-завантаження).
 * @param verbose Увімкнути докладні журнали.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t
cmd_config_execute (int action, const char *set_pairs, config_t *inout_cfg, bool verbose);

/**
 * @brief Друк конфігурації.
 * @param cfg Конфіг (NULL — авто-завантаження).
 * @param verbose Докладні журнали.
 * @return 0 — успіх.
 */
cmd_result_t cmd_config_show (const config_t *cfg, bool verbose);

/**
 * @brief Скидання конфігурації до типових значень.
 * @param inout_cfg Конфіг для запису (NULL — авто-завантаження/збереження).
 * @param verbose Докладні журнали.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_config_reset (config_t *inout_cfg, bool verbose);

/**
 * @brief Застосування пар ключ=значення до конфігурації.
 * @param set_pairs Кома-розділений список key=value.
 * @param inout_cfg Конфіг (NULL — авто-завантаження/збереження).
 * @param verbose Докладні журнали.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_config_set (const char *set_pairs, config_t *inout_cfg, bool verbose);

/**
 * @brief Перелік доступних пристроїв AxiDraw.
 * @param model Модель профілю (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_list (const char *model, bool verbose);

/**
 * @brief Друк активного профілю руху/параметрів.
 * @param alias Псевдонім/порт пристрою (опційно).
 * @param model Очікувана модель профілю (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_profile (const char *alias, const char *model, bool verbose);

/**
 * @brief Підняти перо.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_pen_up (const char *alias, const char *model, bool verbose);

/**
 * @brief Опустити перо.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_pen_down (const char *alias, const char *model, bool verbose);

/**
 * @brief Перемкнути стан пера.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_pen_toggle (const char *alias, const char *model, bool verbose);

/**
 * @brief Увімкнути мотори.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_motors_on (const char *alias, const char *model, bool verbose);

/**
 * @brief Вимкнути мотори.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_motors_off (const char *alias, const char *model, bool verbose);

/**
 * @brief Аварійна зупинка.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_abort (const char *alias, const char *model, bool verbose);

/**
 * @brief Поїздка в home.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_home (const char *alias, const char *model, bool verbose);

/**
 * @brief Ручний зсув на вказану відстань по осях.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param dx_mm Зсув X (мм).
 * @param dy_mm Зсув Y (мм).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t
cmd_device_jog (const char *alias, const char *model, double dx_mm, double dy_mm, bool verbose);

/**
 * @brief Друк версії прошивки EBB/пристрою.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_version (const char *alias, const char *model, bool verbose);

/**
 * @brief Звіт про статус пристрою.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_status (const char *alias, const char *model, bool verbose);

/**
 * @brief Поточна позиція пристрою.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_position (const char *alias, const char *model, bool verbose);

/**
 * @brief Мʼякий скидання контролера.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_reset (const char *alias, const char *model, bool verbose);

/**
 * @brief Перезавантаження контролера.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_device_reboot (const char *alias, const char *model, bool verbose);

#ifdef __cplusplus
}
#endif

#endif
