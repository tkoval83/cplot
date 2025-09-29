/**
 * @file geom.h
 * @brief Геометричні типи та перетворення для контурів.
 * @defgroup geom Геометрія
 * @details
 * Модуль надає базові структури даних та операції для роботи з контурними
 * шляхами (полілініями): ініціалізацію/звільнення, побудову, трансформації,
 * обчислення межового прямокутника та довжин. Всі дані подаються у вибраних
 * одиницях виміру (міліметри або дюйми) і можуть бути конвертовані.
 */
#ifndef GEOM_H
#define GEOM_H

#include <stddef.h>
#include <stdint.h>

/**
 * Одиниці виміру геометрії.
 */
typedef enum {
    GEOM_UNITS_MM = 0, /**< Міліметри (мм). */
    GEOM_UNITS_IN = 1, /**< Дюйми (in). */
} geom_units_t;

/**
 * Точка на площині.
 */
typedef struct {
    double x; /**< X‑координата точки. */
    double y; /**< Y‑координата точки. */
} geom_point_t;

/**
 * Обмежувальний прямокутник (bounding box).
 */
typedef struct {
    double min_x; /**< Мінімальна X‑координата. */
    double min_y; /**< Мінімальна Y‑координата. */
    double max_x; /**< Максимальна X‑координата. */
    double max_y; /**< Максимальна Y‑координата. */
} geom_bbox_t;

/**
 * Один відкритий шлях (полілінія).
 */
typedef struct {
    geom_point_t *pts; /**< Масив точок шляху (довжина `len`). */
    size_t len;        /**< Кількість точок у шляху. */
    size_t cap;        /**< Ємність виділеного масиву `pts`. */
} geom_path_t;

/**
 * Набір шляхів з однаковими одиницями виміру.
 */
typedef struct {
    geom_path_t *items; /**< Масив шляхів (довжина `len`). */
    size_t len;         /**< Кількість наявних шляхів. */
    size_t cap;         /**< Ємність виділеного масиву `items`. */
    geom_units_t units; /**< Одиниці виміру для всіх шляхів. */
} geom_paths_t;

/**
 * @brief Ініціалізує контейнер шляхів.
 * @param ps [out] Контейнер, який буде очищено та ініціалізовано.
 * @param units Одиниці виміру, які привʼязуються до контейнера.
 * @return 0 — успіх; -1 — некоректні аргументи.
 */
int geom_paths_init (geom_paths_t *ps, geom_units_t units);

/**
 * @brief Вивільняє памʼять шляхів та їхніх точок.
 * @param ps Контейнер шляхів; `NULL` ігнорується.
 */
void geom_paths_free (geom_paths_t *ps);

/**
 * @brief Глибоке копіювання набору шляхів.
 * @param src Джерело для копіювання.
 * @param dst [out] Призначення; попередній вміст буде перезаписано.
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_paths_deep_copy (const geom_paths_t *src, geom_paths_t *dst);

/**
 * @brief Ініціалізує шлях із початковою ємністю.
 * @param p [out] Шлях, який буде очищено та підготовлено.
 * @param cap0 Початкова ємність масиву точок (`pts`). Може бути 0.
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_path_init (geom_path_t *p, size_t cap0);

/**
 * @brief Резервує памʼять під точки шляху.
 * @param p Шлях.
 * @param new_cap Бажана ємність; якщо <= поточної — нічого не робить.
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_path_reserve (geom_path_t *p, size_t new_cap);

/**
 * @brief Резервує памʼять під масив шляхів.
 * @param ps Контейнер шляхів.
 * @param new_cap Бажана ємність; якщо <= поточної — нічого не робить.
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_paths_reserve (geom_paths_t *ps, size_t new_cap);

/**
 * @brief Додає точку до шляху.
 * @param p Шлях.
 * @param x X‑координата.
 * @param y Y‑координата.
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_path_push (geom_path_t *p, double x, double y);

/**
 * @brief Додає новий шлях із масиву точок.
 * @param ps Набір шляхів.
 * @param pts Масив точок (може бути `NULL`, якщо `len == 0`).
 * @param len Кількість точок у масиві `pts`.
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_paths_push_path (geom_paths_t *ps, const geom_point_t *pts, size_t len);

/**
 * @brief Зсув усіх шляхів на `dx`,`dy`.
 * @param a Вхідні шляхи.
 * @param dx Зсув по X.
 * @param dy Зсув по Y.
 * @param out [out] Результат (глибока копія з урахуванням зсуву).
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_paths_translate (const geom_paths_t *a, double dx, double dy, geom_paths_t *out);

/**
 * @brief Масштабування шляхів.
 * @param a Вхідні шляхи.
 * @param sx Масштаб по осі X.
 * @param sy Масштаб по осі Y.
 * @param out [out] Результат (глибока копія з урахуванням масштабу).
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_paths_scale (const geom_paths_t *a, double sx, double sy, geom_paths_t *out);

/**
 * @brief Обертання шляхів навколо точки `(cx,cy)` на кут `radians`.
 * @param a Вхідні шляхи.
 * @param radians Кут у радіанах (додатний — проти годинникової стрілки).
 * @param cx X‑координата центру обертання.
 * @param cy Y‑координата центру обертання.
 * @param out [out] Результат (глибока копія з урахуванням обертання).
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_paths_rotate (
    const geom_paths_t *a, double radians, double cx, double cy, geom_paths_t *out);

/**
 * @brief Розрахунок обмежувального прямокутника одного шляху.
 * @param p Шлях.
 * @param out [out] Результуючий прямокутник.
 * @return 0 — успіх; -1 — некоректні аргументи або порожній шлях.
 */
int geom_bbox_of_path (const geom_path_t *p, geom_bbox_t *out);

/**
 * @brief Розрахунок обмежувального прямокутника для набору шляхів.
 * @param ps Набір шляхів.
 * @param out [out] Результуючий прямокутник.
 * @return 0 — успіх; -1 — некоректні аргументи або всі шляхи порожні.
 */
int geom_bbox_of_paths (const geom_paths_t *ps, geom_bbox_t *out);

/**
 * @brief Довжина полілінії (сума довжин сегментів).
 * @param p Шлях.
 * @return Довжина у відповідних одиницях; 0 для `NULL` або < 2 точок.
 */
double geom_path_length (const geom_path_t *p);

/**
 * @brief Сумарна довжина всіх шляхів у контейнері.
 * @param ps Набір шляхів.
 * @return Довжина у відповідних одиницях; 0 для `NULL`.
 */
double geom_paths_length (const geom_paths_t *ps);

/**
 * @brief Встановлює одиниці виміру контейнера.
 * @param ps Набір шляхів.
 * @param units Одиниці виміру.
 * @return 0 — успіх; -1 — некоректні аргументи.
 */
int geom_paths_set_units (geom_paths_t *ps, geom_units_t units);

/**
 * @brief Конвертує одиниці виміру всіх точок у контейнері.
 * @param a Вхідні шляхи.
 * @param to Цільові одиниці.
 * @param out [out] Результат (глибока копія з масштабуванням).
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_paths_convert (const geom_paths_t *a, geom_units_t to, geom_paths_t *out);

/**
 * @brief Нормалізує порядок і знімає зайві точки.
 * @details Наразі є синонімом глибокого копіювання (без змін).
 * @param a Вхідні шляхи.
 * @param out [out] Результат.
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
int geom_paths_normalize (const geom_paths_t *a, geom_paths_t *out);

/**
 * @brief Порівняння точок із допуском.
 * @param a Перша точка.
 * @param b Друга точка.
 * @param tol Допуск (максимально допустиме відхилення по X та Y).
 * @return 1 — рівні; 0 — нерівні або `NULL`.
 */
int geom_point_eq (const geom_point_t *a, const geom_point_t *b, double tol);

/**
 * @brief Хеш набору шляхів у мікро‑міліметрах.
 * @details Обчислює FNV‑подібний хеш, квантувавши координати до мікронів
 * (масштаб 1000 для мм). Корисно для кешування/порівняння контурів.
 * @param ps Набір шляхів.
 * @return 64‑бітове хеш‑значення; 0 для `NULL`.
 */
uint64_t geom_paths_hash_micro_mm (const geom_paths_t *ps);

#endif
