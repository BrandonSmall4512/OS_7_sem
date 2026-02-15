#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define CYLINDERS 500
#define HEADS 4
#define SECTORS_PER_TRACK 16
#define SEEK_TIME_PER_CYLINDER 0.5  // мс
#define RPM 10000.0
#define SIMULATION_TIME 300000.0

// Время одного оборота диска в мс
#define ROTATION_TIME (60000.0 / RPM)
// Время прохождения одного сектора под головкой
#define SECTOR_TIME (ROTATION_TIME / SECTORS_PER_TRACK)

// Структура для запроса к диску
typedef struct {
    double arrival_time;    // Время поступления запроса
    int cylinder;           // Номер цилиндра
    int head;               // Номер головки
    int sector;             // Начальный сектор
    int operation;          // Тип операции (0 - чтение, 1 - запись)
    int num_sectors;        // Количество секторов
    double start_time;      // Время начала обслуживания
    double completion_time; // Время завершения обслуживания
} DiskRequest;


typedef struct {
    double min_time;
    double max_time;
    double avg_time;
    double std_dev;
    int max_queue_length;
    double total_idle_time;
    int total_requests;
} SimulationStats;

typedef struct {
    double min;
    double max;
    int count;
} HistogramBin;

double calculate_seek_time(int current_cylinder, int target_cylinder) {
    return fabs(current_cylinder - target_cylinder) * SEEK_TIME_PER_CYLINDER;
}

double calculate_rotational_latency(double current_angle, int target_sector) {
    double target_angle = target_sector * (360.0 / SECTORS_PER_TRACK);
    double angle_diff = fmod(target_angle - current_angle + 360.0, 360.0);
    return (angle_diff / 360.0) * ROTATION_TIME;
}

double calculate_transfer_time(int num_sectors, int operation) {
    double base_time = num_sectors * SECTOR_TIME;
    return (operation == 1) ? base_time * 2 : base_time;
}


DiskRequest* generate_requests(double t_max, int n, int* total_requests) {
    int max_requests = (int)(SIMULATION_TIME / (t_max * 1000)) * 2;
    DiskRequest* requests = malloc(max_requests * sizeof(DiskRequest));

    double current_time = 0;
    int count = 0;
    srand(time(NULL) + (int)(t_max * 1000));

    while (current_time < SIMULATION_TIME && count < max_requests) {
        double interval = ((double)rand() / RAND_MAX) * t_max * 1000;
        current_time += interval;
        if (current_time >= SIMULATION_TIME) break;

        requests[count].arrival_time = current_time;
        requests[count].cylinder = rand() % CYLINDERS;
        requests[count].head = rand() % HEADS;
        requests[count].sector = rand() % SECTORS_PER_TRACK;
        requests[count].operation = rand() % 2;
        requests[count].num_sectors = (rand() % n) + 1;
        requests[count].start_time = 0;
        requests[count].completion_time = 0;
        count++;
    }

    *total_requests = count;
    return requests;
}

// --- FIFO ---
void simulate_fifo(DiskRequest* requests, int total_requests, SimulationStats* stats) {
    double current_time = 0;
    int current_cylinder = 0;
    double current_angle = 0;
    int queue_length = 0;
    int max_queue_length = 0;
    double total_idle_time = 0;

    stats->min_time = 1e9;
    stats->max_time = 0;
    double sum_time = 0;
    double sum_squared_time = 0;

    int request_index = 0;

    while (request_index < total_requests) {
        while (request_index < total_requests && requests[request_index].arrival_time <= current_time) {
            queue_length++;
            request_index++;
        }

        if (queue_length > max_queue_length)
            max_queue_length = queue_length;

        if (queue_length > 0) {
            DiskRequest* req = &requests[request_index - queue_length];
            double seek_time = calculate_seek_time(current_cylinder, req->cylinder);
            double rot_latency = calculate_rotational_latency(current_angle, req->sector);
            double transfer_time = calculate_transfer_time(req->num_sectors, req->operation);
            double service_time = seek_time + rot_latency + transfer_time;

            req->start_time = current_time;
            req->completion_time = current_time + service_time;
            double response_time = fmax(0.0, req->completion_time - req->arrival_time);

            if (response_time < stats->min_time) stats->min_time = response_time;
            if (response_time > stats->max_time) stats->max_time = response_time;
            sum_time += response_time;
            sum_squared_time += response_time * response_time;

            current_time = req->completion_time;
            current_cylinder = req->cylinder;
            current_angle = fmod(current_angle + (rot_latency + transfer_time) / ROTATION_TIME * 360.0, 360.0);

            queue_length--;
        } else {
            if (request_index < total_requests) {
                double idle_time = requests[request_index].arrival_time - current_time;
                total_idle_time += idle_time;
                current_time = requests[request_index].arrival_time;
            } else break;
        }
    }

    stats->max_queue_length = max_queue_length;
    stats->total_idle_time = total_idle_time;
    stats->total_requests = total_requests;
    stats->avg_time = sum_time / total_requests;
    stats->std_dev = sqrt((sum_squared_time / total_requests) - (stats->avg_time * stats->avg_time));
}

// --- SSTF ---
void simulate_sstf(DiskRequest* requests, int total_requests, SimulationStats* stats) {
    double current_time = 0;
    int current_cylinder = 0;
    double current_angle = 0;
    int queue_length = 0;
    int max_queue_length = 0;
    double total_idle_time = 0;

    int* in_queue = calloc(total_requests, sizeof(int));
    int* processed = calloc(total_requests, sizeof(int));

    stats->min_time = 1e9;
    stats->max_time = 0;
    double sum_time = 0;
    double sum_squared_time = 0;

    int processed_count = 0;

    while (processed_count < total_requests) {
        for (int i = 0; i < total_requests; i++) {
            if (!in_queue[i] && !processed[i] && requests[i].arrival_time <= current_time) {
                in_queue[i] = 1;
                queue_length++;
            }
        }

        if (queue_length > max_queue_length)
            max_queue_length = queue_length;

        if (queue_length > 0) {
            int closest_index = -1;
            int min_distance = CYLINDERS + 1;
            for (int i = 0; i < total_requests; i++) {
                if (in_queue[i] && !processed[i]) {
                    int distance = abs(current_cylinder - requests[i].cylinder);
                    if (distance < min_distance) {
                        min_distance = distance;
                        closest_index = i;
                    }
                }
            }

            if (closest_index == -1) break;

            DiskRequest* req = &requests[closest_index];
            double seek_time = calculate_seek_time(current_cylinder, req->cylinder);
            double rot_latency = calculate_rotational_latency(current_angle, req->sector);
            double transfer_time = calculate_transfer_time(req->num_sectors, req->operation);
            double service_time = seek_time + rot_latency + transfer_time;

            req->start_time = current_time;
            req->completion_time = current_time + service_time;
            double response_time = fmax(0.0, req->completion_time - req->arrival_time);

            if (response_time < stats->min_time) stats->min_time = response_time;
            if (response_time > stats->max_time) stats->max_time = response_time;
            sum_time += response_time;
            sum_squared_time += response_time * response_time;

            current_time = req->completion_time;
            current_cylinder = req->cylinder;
            current_angle = fmod(current_angle + (rot_latency + transfer_time) / ROTATION_TIME * 360.0, 360.0);

            in_queue[closest_index] = 0;
            processed[closest_index] = 1;
            queue_length--;
            processed_count++;
        } else {
            double next_arrival = SIMULATION_TIME + 1;
            for (int i = 0; i < total_requests; i++)
                if (!processed[i] && requests[i].arrival_time < next_arrival)
                    next_arrival = requests[i].arrival_time;
            if (next_arrival <= SIMULATION_TIME) {
                total_idle_time += next_arrival - current_time;
                current_time = next_arrival;
            } else break;
        }
    }

    stats->max_queue_length = max_queue_length;
    stats->total_idle_time = total_idle_time;
    stats->total_requests = processed_count;
    stats->avg_time = sum_time / processed_count;
    stats->std_dev = sqrt((sum_squared_time / processed_count) - (stats->avg_time * stats->avg_time));

    free(in_queue);
    free(processed);
}

int compare_doubles(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

void create_histogram(DiskRequest* requests, int total_requests, const char* strategy_name) {
    if (total_requests == 0) return;

    double* service_times = malloc(total_requests * sizeof(double));
    for (int i = 0; i < total_requests; i++)
        service_times[i] = fmax(0.0, requests[i].completion_time - requests[i].arrival_time);

    double min_time = service_times[0], max_time = service_times[0], mean = 0;
    for (int i = 0; i < total_requests; i++) {
        if (service_times[i] < min_time) min_time = service_times[i];
        if (service_times[i] > max_time) max_time = service_times[i];
        mean += service_times[i];
    }
    mean /= total_requests;

    qsort(service_times, total_requests, sizeof(double), compare_doubles);
    double median = (total_requests % 2 == 0)
        ? (service_times[total_requests/2 - 1] + service_times[total_requests/2]) / 2.0
        : service_times[total_requests/2];

    int num_bins = (int)sqrt(total_requests);
    if (num_bins < 10) num_bins = 10;
    if (num_bins > 40) num_bins = 40;

    HistogramBin* bins = malloc(num_bins * sizeof(HistogramBin));
    double bin_width = (max_time - min_time) / num_bins;

    for (int i = 0; i < num_bins; i++) {
        bins[i].min = min_time + i * bin_width;
        bins[i].max = bins[i].min + bin_width;
        bins[i].count = 0;
    }

    for (int i = 0; i < total_requests; i++) {
        int idx = (int)((service_times[i] - min_time) / bin_width);
        if (idx >= num_bins) idx = num_bins - 1;
        bins[idx].count++;
    }

    int max_count = 0;
    for (int i = 0; i < num_bins; i++)
        if (bins[i].count > max_count) max_count = bins[i].count;

    printf("\n═══════════════════════════════════════════════════════════════════════\n");
    printf("Гистограмма распределения времени отклика (%s)\n", strategy_name);
    printf("(ось X — время обслуживания, мс | ось Y — количество запросов)\n");
    printf("Диапазон: [%.2f .. %.2f] мс | Среднее: %.2f | Медиана: %.2f\n",
           min_time, max_time, mean, median);
    printf("-----------------------------------------------------------------------\n");
    printf("Интервал (мс)          Кол-во    %%      | График\n");
    printf("-----------------------------------------------------------------------\n");

    for (int i = 0; i < num_bins; i++) {
        double percent = (bins[i].count * 100.0) / total_requests;
        int bar_length = (bins[i].count * 50) / (max_count ? max_count : 1);
        printf("%7.2f – %-9.2f %6d  %6.2f%% | ", bins[i].min, bins[i].max, bins[i].count, percent);
        for (int j = 0; j < bar_length; j++) printf("█");
        printf("\n");
    }

    printf("-----------------------------------------------------------------------\n");
    printf("Среднее: %.2f мс | Медиана: %.2f мс | Мин: %.2f | Макс: %.2f\n",
           mean, median, min_time, max_time);
    printf("═══════════════════════════════════════════════════════════════════════\n");

    free(service_times);
    free(bins);
}

int main() {
    const char* strategy = "SSTF";
    double t_max = 2.0;
    int n = 16;

    double t_max_values[] = {t_max, t_max/10, t_max/100};
    int num_experiments = 3;

    printf("Моделирование работы дисковой подсистемы\n");
    printf("========================================\n");
    printf("Параметры диска:\n");
    printf("- Цилиндров: %d\n", CYLINDERS);
    printf("- Головок: %d\n", HEADS);
    printf("- Секторов на дорожке: %d\n", SECTORS_PER_TRACK);
    printf("- Время поиска на цилиндр: %.1f мс\n", SEEK_TIME_PER_CYLINDER);
    printf("- Скорость вращения: %.0f об/мин\n", RPM);
    printf("- Время моделирования: %.0f мс\n", SIMULATION_TIME);
    printf("- Стратегия сравнения: %s\n", strategy);
    printf("- Параметр t_max: %.1f с\n", t_max);
    printf("- Параметр n: %d\n\n", n);

    for (int exp = 0; exp < num_experiments; exp++) {
        double current_t_max = t_max_values[exp];
        printf("Эксперимент %d: t_max = %.3f с\n", exp + 1, current_t_max);
        printf("----------------------------------------\n");

        int total_requests;
        DiskRequest* requests = generate_requests(current_t_max, n, &total_requests);
        printf("Сгенерировано запросов: %d\n", total_requests);

        SimulationStats stats_fifo = {0};
        DiskRequest* fifo_copy = malloc(total_requests * sizeof(DiskRequest));
        memcpy(fifo_copy, requests, total_requests * sizeof(DiskRequest));
        simulate_fifo(fifo_copy, total_requests, &stats_fifo);

        SimulationStats stats_sstf = {0};
        DiskRequest* sstf_copy = malloc(total_requests * sizeof(DiskRequest));
        memcpy(sstf_copy, requests, total_requests * sizeof(DiskRequest));
        simulate_sstf(sstf_copy, total_requests, &stats_sstf);

        printf("\nРезультаты FIFO:\n");
        printf("Среднее: %.2f | Макс: %.2f | Мин: %.2f | Std: %.2f | Очередь макс: %d\n",
               stats_fifo.avg_time, stats_fifo.max_time, stats_fifo.min_time,
               stats_fifo.std_dev, stats_fifo.max_queue_length);
        printf("Простой: %.2f мс | Запросов: %d\n",
               stats_fifo.total_idle_time, stats_fifo.total_requests);

        printf("\nРезультаты SSTF:\n");
        printf("Среднее: %.2f | Макс: %.2f | Мин: %.2f | Std: %.2f | Очередь макс: %d\n",
               stats_sstf.avg_time, stats_sstf.max_time, stats_sstf.min_time,
               stats_sstf.std_dev, stats_sstf.max_queue_length);
        printf("Простой: %.2f мс | Запросов: %d\n",
               stats_sstf.total_idle_time, stats_sstf.total_requests);

        printf("\n=== Гистограммы для эксперимента %d (t_max = %.3f с) ===\n", exp + 1, current_t_max);
        create_histogram(fifo_copy, total_requests, "FIFO");
        create_histogram(sstf_copy, total_requests, "SSTF");

        free(requests);
        free(fifo_copy);
        free(sstf_copy);
        printf("\n");
    }

    return 0;
}
