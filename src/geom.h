/**
 * @file geom.h
 * @brief Легковагові 2D геометричні примітиви та операції для роботи зі шляхами.
 */
#ifndef GEOM_H
#define GEOM_H

#include <stddef.h>
#include <stdint.h>

/// Одиниці вимірювання, що підтримуються для шляхів і розкладки.
typedef enum {
    GEOM_UNITS_MM = 0,
    GEOM_UNITS_IN = 1,
} geom_units_t;

/// Точка у 2D.
typedef struct {
    double x, y;
} geom_point_t;

/// Прямокутник, вирівняний по осях (межі).
typedef struct {
    double min_x, min_y, max_x, max_y;
} geom_bbox_t;

/// Шлях як динамічний масив точок.
typedef struct {
    geom_point_t *pts;
    size_t len;
    size_t cap;
} geom_path_t;

/// Колекція шляхів із спільною системою одиниць.
typedef struct {
    geom_path_t *items;
    size_t len;
    size_t cap;
    geom_units_t units;
} geom_paths_t;

// Конструктори / деструктори
/**
 * Ініціалізувати geom_paths_t з заданими одиницями.
 * @param ps    Вказівник на контейнер шляхів (не NULL).
 * @param units Система одиниць для контейнера.
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_init (geom_paths_t *ps, geom_units_t units);
/**
 * Звільнити всю пам'ять geom_paths_t та скидати його поля.
 * @param ps Вказівник на контейнер шляхів (безпечно передавати NULL).
 */
void geom_paths_free (geom_paths_t *ps);
/**
 * Глибоко скопіювати src у dst (виділяє нові буфери у dst).
 * @param src Джерельний контейнер шляхів.
 * @param dst Призначення для глибокої копії (не NULL).
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_deep_copy (const geom_paths_t *src, geom_paths_t *dst);

// Динамічні масиви
/**
 * Ініціалізувати шлях із необов'язковою початковою місткістю.
 * @param p     Шлях для ініціалізації (не NULL).
 * @param cap0  Початкова місткість у точках (0 — типово).
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_path_init (geom_path_t *p, size_t cap0);
/**
 * Гарантувати місткість шляху щонайменше на new_cap точок.
 * @param p       Шлях для резервування місткості.
 * @param new_cap Мінімальна місткість у точках.
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_path_reserve (geom_path_t *p, size_t new_cap);
/**
 * Гарантувати місткість колекції шляхів щонайменше на new_cap елементів.
 * @param ps       Колекція шляхів.
 * @param new_cap  Мінімальна кількість елементів.
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_reserve (geom_paths_t *ps, size_t new_cap);
/**
 * Додати точку (x,y) до шляху, за потреби збільшуючи місткість.
 * @param p Шлях для додавання.
 * @param x Координата X.
 * @param y Координата Y.
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_path_push (geom_path_t *p, double x, double y);
/**
 * Додати новий шлях з масиву точок до колекції шляхів.
 * @param ps   Колекція шляхів.
 * @param pts  Масив точок.
 * @param len  Кількість точок у масиві.
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_push_path (geom_paths_t *ps, const geom_point_t *pts, size_t len);

// Перетворення (запис у out)
/**
 * Зсунути всі точки на (dx,dy).
 * @param a   Вхідні шляхи.
 * @param dx  Зсув X.
 * @param dy  Зсув Y.
 * @param out Вихідні шляхи (інший об'єкт; буде ініціалізовано).
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_translate (const geom_paths_t *a, double dx, double dy, geom_paths_t *out);
/**
 * Масштабувати всі точки на (sx,sy) відносно початку координат.
 * @param a   Вхідні шляхи.
 * @param sx  Масштаб за X.
 * @param sy  Масштаб за Y.
 * @param out Вихідні шляхи (інший об'єкт; буде ініціалізовано).
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_scale (const geom_paths_t *a, double sx, double sy, geom_paths_t *out);
/**
 * Повернути всі точки на кут у радіанах навколо центру (cx,cy).
 * @param a       Вхідні шляхи.
 * @param radians Кут у радіанах (за год. стрілкою проти — CCW).
 * @param cx      Центр обертання X.
 * @param cy      Центр обертання Y.
 * @param out     Вихідні шляхи (інший об'єкт; буде ініціалізовано).
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_rotate (
    const geom_paths_t *a, double radians, double cx, double cy, geom_paths_t *out);

// Запити
/**
 * Обчислити прямокутник меж (AABB) для шляху.
 * @param p   Вхідний шлях.
 * @param out Вихідний прямокутник меж.
 * @return 0 у разі успіху; ненульове значення, якщо p NULL або порожній.
 */
int geom_bbox_of_path (const geom_path_t *p, geom_bbox_t *out);
/**
 * Обчислити AABB для всієї колекції шляхів.
 * @param ps  Вхідні шляхи.
 * @param out Вихідний прямокутник меж.
 * @return 0 у разі успіху; ненульове значення, якщо точок немає.
 */
int geom_bbox_of_paths (const geom_paths_t *ps, geom_bbox_t *out);
/**
 * Повернути сумарну довжину ламаної шляху.
 * @param p Вхідний шлях.
 * @return Загальна довжина (0.0, якщо порожній або NULL).
 */
double geom_path_length (const geom_path_t *p);
/**
 * Повернути суму довжин ламаних для всіх шляхів.
 * @param ps Вхідні шляхи.
 * @return Сума довжин (0.0, якщо порожньо або NULL).
 */
double geom_paths_length (const geom_paths_t *ps);

// Одиниці
/**
 * Встановити систему одиниць для наявного geom_paths_t без конвертації координат.
 * @param ps    Колекція шляхів для оновлення.
 * @param units Нова система одиниць (без зміни координат).
 * @return 0 у разі успіху; ненульове значення, якщо ps NULL.
 */
int geom_paths_set_units (geom_paths_t *ps, geom_units_t units);
/**
 * Конвертувати координати у цільову систему одиниць та записати у out.
 * @param a   Вхідні шляхи.
 * @param to  Цільова система одиниць.
 * @param out Вихідні шляхи (інший об'єкт; буде ініціалізовано).
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_convert (const geom_paths_t *a, geom_units_t to, geom_paths_t *out);

// Нормалізація
/**
 * Нормалізувати шляхи (напр., усунути вироджені точки, уніфікувати структуру) у out.
 * @param a   Вхідні шляхи.
 * @param out Вихідні шляхи (інший об'єкт; буде ініціалізовано).
 * @return 0 у разі успіху; ненульове значення при збої виділення пам'яті.
 */
int geom_paths_normalize (const geom_paths_t *a, geom_paths_t *out);

// Рівність і хешування
/**
 * Перевірка рівності з плаваючою точкою з толерансом.
 * @param a   Перша точка.
 * @param b   Друга точка.
 * @param tol Абсолютний допуск.
 * @return 1 якщо рівні в межах tol; 0 інакше.
 */
int geom_point_eq (const geom_point_t *a, const geom_point_t *b, double tol);
/**
 * Обчислити хеш колекції шляхів, квантувавши до мікроміліметра.
 * @param ps Вхідні шляхи.
 * @return 64-бітне хеш-значення.
 */
uint64_t geom_paths_hash_micro_mm (const geom_paths_t *ps);

#endif /* GEOM_H */
