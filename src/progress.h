/**
 * @file progress.h
 * @brief Простий, але приємний індикатор прогресу для CLI (рядок прогресу + спінер, ETA).
 */
#ifndef PROGRESS_H
#define PROGRESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "colors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Налаштування відображення прогресу.
 */
typedef struct progress_opts_s {
    bool use_colors;  /* Використовувати ANSI-кольори */
    bool use_unicode; /* Використовувати Юнікод-символи (блоки, спінер) */
    int width;        /* Ширина лінії прогресу (в символах), 0 = авто (40) */
    int throttle_ms;  /* Мінімальний інтервал між оновленнями у мс (типово 80) */
    FILE *stream;     /* Потік виводу (типово stderr) */
} progress_opts_t;

/**
 * Стан прогресу.
 */
typedef struct progress_s {
    uint64_t total;      /* Загальна кількість кроків (може бути 0, якщо невідомо) */
    uint64_t done;       /* Виконано кроків */
    double started_at;   /* Час старту (секунди з епохи) */
    double last_draw;    /* Час останнього оновлення */
    progress_opts_t opt; /* Опції відображення */
    int spinner_idx;     /* Позиція спінера */
    bool finished;       /* Чи завершено */
} progress_t;

/**
 * Створити індикатор прогресу.
 *
 * @param p     Структура прогресу (вихід).
 * @param total Загальна кількість кроків (0, якщо невідомо; тоді показує лише спінер).
 * @param opt   Необов'язкові опції (NULL = типові значення).
 */
void progress_init (progress_t *p, uint64_t total, const progress_opts_t *opt);

/**
 * Оновити прогрес (збільшити виконане) і, за потреби, перемалювати індикатор.
 *
 * @param p     Прогрес.
 * @param delta Приріст виконаних кроків.
 */
void progress_add (progress_t *p, uint64_t delta);

/**
 * Примусово перемалювати індикатор прогресу (ігноруючи throttle).
 *
 * @param p Прогрес.
 */
void progress_draw (progress_t *p);

/**
 * Позначити завершення і закрити рядок прогресу (перенести рядок).
 *
 * @param p Прогрес.
 */
void progress_finish (progress_t *p);

#ifdef __cplusplus
}
#endif

#endif /* PROGRESS_H */
