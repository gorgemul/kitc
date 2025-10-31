#define _GNU_SOURCE   // getline
#define _XOPEN_SOURCE // strptime
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/param.h> // MAX
#include <sys/wait.h>
#include <sys/time.h> // gettimeofday

#define ANSI_COLOR_YELLOW     "\x1b[33m"
#define ANSI_COLOR_RESET      "\x1b[0m"
#define EPOCH_SECCOND_LEN     10
#define EPOCH_MILLISECOND_LEN 13

extern unsigned char config_txt[];
extern unsigned int  config_txt_len;

void fatal(char *format, ...)
{
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        fputc('\n', stderr);
        exit(EXIT_FAILURE);
}

bool is_flag(const char *arg)
{
        return strlen(arg) >= 2 && arg[0] == '-' && (arg[1] != ' ' && !(arg[1] >= '0' && arg[1] <= '9'));
}

long str_to_long(const char *s, int base)
{
        char *endptr = NULL;
        long l = strtol(s, &endptr, base);
        if (endptr == s || *endptr != '\0') {
                if (base == 2)
                        fatal("ERROR: invalid binary format: %s", s);
                else if (base == 10)
                        fatal("ERROR: invalid decimal format: %s", s);
                else if (base == 16)
                        fatal("ERROR: invalid hex format: %s", s);
                else
                        fatal("ERROR: not supported base: %d for %s", base, s);
        }
        return l;
}

void write_to_clipboard(char *content)
{
#ifdef __linux__
        FILE *pipe = popen("xclip -selection clipboard", "w");
#elif __APPLE__
        FILE *pipe = popen("pbcopy", "w");
#else
#error currently only support linux and macos
#endif
        if (!pipe) {
                fprintf(stderr, "ERROR: write_to_clipboard popen fail\n");
                exit(1);
        }
        fprintf(pipe, "%s", content);
        if ((pclose(pipe)) == -1) {
                printf("ERROR: write_to_clipboard pclose fail\n");
                exit(1);
        }
}

int get_config_file_line()
{
        int count = 1;
        for (unsigned int i = 0; i < config_txt_len; ++i) {
                if (config_txt[i] == '\n') count++;
        }
        return count;
}

char *decimal_to_binary(const char *s)
{
        long decimal = str_to_long(s, 10);
        bool is_neg = false;
        char *ret = malloc(128);
        if (!ret)
                fatal("ERROR: decimal_to_binary ret malloc fail");
        if (decimal == 0) {
                strcpy(ret, "0 (length: 1)");
                return ret;
        } else if (decimal < 0) {
                is_neg = true;
                decimal = -decimal;
        }
        char bits[64];
        int len = 0;
        while (decimal > 0) {
                bits[len++] = (decimal & 1) == 1 ? '1' : '0';
                decimal >>= 1;
        }
        // reverse array
        for (int head = 0; head < len / 2; ++head) {
                char temp = bits[head];
                int tail = len - 1 - head;
                bits[head] = bits[tail];
                bits[tail] = temp;
        }
        // two's complement
        if (is_neg) {
                char buf[64] = {0};
                int extend_len;
                if (len < 16)
                        extend_len = 16;
                else if (len < 32)
                        extend_len = 32;
                else
                        extend_len = 64;
                int padding = extend_len - len;
                memset(buf, '0', padding);
                memcpy(buf + padding, bits, len);
                len = extend_len;
                // bit flip
                for (int i = 0; i < len; ++i)
                        buf[i] = buf[i] == '0' ? '1' : '0';
                // add 1
                for (int i = len - 1; i >= 0; --i) {
                        if (buf[i] == '0') {
                                buf[i] = '1';
                                break;
                        }
                        buf[i] = '0';
                }
                memcpy(bits, buf, 64);
        }
        // format output
        char *p = ret;
        int first_group = len % 8 == 0 ? 8 : len % 8;
        for (int i = 0; i < len; i++) {
                *p++ = bits[i];
                int n_written = i + 1;
                if (n_written == first_group || (n_written > first_group && (n_written - first_group) % 8 == 0))
                        *p++ = ' ';
        }
        sprintf(p, "(length: %d)", len);
        return ret;
}

char *decimal_to_hex(const char *s)
{
        long decimal = str_to_long(s, 10);
        bool is_neg = false;
        char *hex = malloc(128);
        if (hex == NULL)
                fatal("ERROR: decimal_to_hex ret malloc fail");
        if (decimal < 0) {
                is_neg = true;
                decimal = -decimal;
        }
        sprintf(hex, is_neg ? "-0x%lx" : "0x%lx", decimal);
        return hex;
}

// has 0b or 0B prefix, only support positive binary
long binary_to_decimal(const char *s)
{
        return str_to_long(s + 2, 2); // trim the prefix
}

char *binary_to_hex(const char *s)
{
        long decimal = str_to_long(s + 2, 2); // trim the prefix
        char *hex = malloc(128);
        if (hex == NULL)
                fatal("ERROR: binary_to_hex ret malloc fail");
        sprintf(hex, "0x%lx", decimal);
        return hex;
}

// has 0x or 0X prefix, only support positive hex
long hex_to_decimal(const char *s)
{
        return str_to_long(s + 2, 16); // trim the prefix
}

char *hex_to_binary(const char *s)
{
        long decimal = str_to_long(s + 2, 16); // trim the prefix
        char decimal_str[64];
        sprintf(decimal_str, "%ld", decimal);
        return decimal_to_binary(decimal_str);
}

void print_n_char(char c, size_t n)
{
        for (size_t i = 0; i < n; ++i)
                putc(c, stdout);
}

struct ConfigItem {
        char *name;
        char *key;
        char *value;
};

struct Config {
        int len;
        struct ConfigItem *items;
};

struct App {
        bool config_print;
        char *config_query_name;

        bool db_print_batch;
        bool db_print_load;
        bool db_print_sniff_and_shake;
        bool db_print_dump;
        bool db_print_wash;
        char *db_query_line;
        char *db_query_time;

        char *timestamp;            // could be unix timestamp 1761272902 and formatted time "2025-09-12 12:30:21"
        char *ssh_with_config_name; // ssh with config's name
        char *number;
        char **calculator_args;     // use NULL termitor otherwise need to track it's length
};

struct Config *config_init()
{
        struct Config *config = calloc(1, sizeof(struct Config));
        if (config == NULL)
                fatal("ERROR: config_init config calloc fail");
        struct ConfigItem *items = calloc(get_config_file_line(), sizeof(struct ConfigItem));
        if (items == NULL)
                fatal("ERROR: config_init items calloc fail");
        config->items = items;
        unsigned int advance_len = 0;
        char *line_start = (char*)config_txt;
        while (advance_len < config_txt_len) {
                char *line_end = strchr(line_start, '\n');
                int line_len;
                if (line_end)
                        line_len = line_end - line_start;
                else // last line
                        line_len = (char*)(config_txt + config_txt_len) - line_start;
                char *line = calloc(1, line_len + 1);
                if (line == NULL)
                        fatal("ERROR: config_init line calloc fail");
                memcpy(line, line_start, line_len);
                line_start = line_end + 1;
                advance_len += (line_len + 1);
                int first = 0;
                while (line[first] == ' ' || line[first] == '\t')
                        first++;
                if (line[first] == '#') {
                        free(line);
                        continue;
                }
                char *elements[3] = {NULL};
                int element_count = 0; // config item should have three elements([name] [key] [value])
                char *start = NULL;
                char *end = NULL;
                for (int i = 0; i < line_len; ++i) {
                        switch (line[i]) {
                        case '[':
                                if (start != NULL)
                                        fatal("ERROR: invalid line in config file: %s", line);
                                start = line + i;
                                break;
                        case ']':
                                if (start == NULL)
                                        fatal("ERROR: invalid line in config file: %s", line);
                                end = line + i;
                                size_t len = end - start;
                                char *element = calloc(len, sizeof(char));
                                if (element == NULL)
                                        fatal("ERROR: config_init element calloc fail");
                                memcpy(element, start+1, len-1);
                                if (element_count >= 3)
                                        fatal("ERROR: invalid line in config file: %s", line);
                                elements[element_count++] = element;
                                start = NULL;
                                break;
                        default:
                                break;
                        }
                }
                if (element_count != 3)
                        fatal("ERROR: invalid line in config file: %s", line);
                config->items[config->len].name = elements[0];
                config->items[config->len].key = elements[1];
                config->items[config->len].value = elements[2];
                config->len++;
                free(line);
        }
        return config;
}

void config_print(struct Config *config)
{
        if (config->len == 0) {
                printf("ERROR: no config was found, should be installed with config.txt(format: [name] [key] [value]).\n");
                return;
        }
        size_t column_len[3] = {
                strlen(config->items[0].name),
                strlen(config->items[0].key),
                strlen(config->items[0].value)
        };
        for (int i = 1; i < config->len; ++i) {
                column_len[0] = MAX(column_len[0], strlen(config->items[i].name));
                column_len[1] = MAX(column_len[1], strlen(config->items[i].key));
                column_len[2] = MAX(column_len[2], strlen(config->items[i].value));
        }
        // for left && right padding
        column_len[0] += 4;
        column_len[1] += 4;
        column_len[2] += 4;
        char *header[3] = { "name", "key", "value" };
        char *vertical_separator = "¦";
        size_t width = column_len[0] + column_len[1] + column_len[2] + 4; // 4 "|"
        print_n_char('-', width);
        printf("\n");
        // print header
        for (int i = 0; i < 3; ++i) {
                size_t left_padding = (column_len[i] - strlen(header[i])) / 2;
                size_t right_apdding = column_len[i] - left_padding - strlen(header[i]);
                printf("%s", vertical_separator);
                print_n_char(' ', left_padding);
                printf("%s", ANSI_COLOR_YELLOW); 
                printf("%s", header[i]);
                printf("%s", ANSI_COLOR_RESET); 
                print_n_char(' ', right_apdding);
                if (i == 2)
                        printf("%s\n", vertical_separator);
        }
        // print content
        for (int i = 0; i < config->len; ++i) {
                print_n_char('-', width);
                printf("\n");
                for (int j = 0; j < 3; ++j) {
                        char *element;
                        if (j == 0)
                                element = config->items[i].name;
                        else if (j == 1)
                                element = config->items[i].key;
                        else
                                element = config->items[i].value;
                        size_t element_len = strlen(element);
                        size_t left_padding = (column_len[j] - element_len) / 2;
                        size_t right_padding = column_len[j] - left_padding - element_len;
                        printf("%s", vertical_separator);
                        print_n_char(' ', left_padding);
                        if (j == 2)
                                print_n_char('*', element_len);
                        else
                                printf("%s", element);
                        print_n_char(' ', right_padding);
                        if (j == 2)
                                printf("%s\n", vertical_separator);
                }
                if (i == config->len - 1) {
                        print_n_char('-', width);
                        printf("\n");
                }
        }
}

void config_destroy(struct Config *config)
{
        for (int i = 0; i < config->len; ++i) {
                free(config->items[i].name);
                free(config->items[i].key);
                free(config->items[i].value);
        }
        free(config->items);
        free(config);
}


struct App *app_init(int argc, char **argv)
{
        struct App *app = calloc(1, sizeof(*app));
        if (app == NULL)
                fatal("ERROR: app_init app calloc fail");
        // first loop search for help flag
        for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                        printf("OPTIONS:\n");
                        printf("    -h,   --help,             show this help message\n");
                        printf("    -c,   --config            show config, name-key-value format\n");
                        printf("    -cv,  --config_vlaue      get config value by config name(-c to query)\n");
                        printf("    -qb,  --query_batch       show query for batch\n");
                        printf("    -ql,  --query_load        show query for load\n");
                        printf("    -qss, --query_sniff_shake show query for sniff and shake\n");
                        printf("    -qd,  --query_dump        show query for dump\n");
                        printf("    -qw,  --query_wash        show qeury for wash\n");
                        printf("    -qln, --query_line        1|2|3|4, default is 3\n");
                        printf("    -qt,  --query_time        2025-09-12 12:30:21, default is now\n");
                        printf("    -t,   --timestamp         1757651421 -> 2025-09-12 12:30:21, vice versa\n");
                        printf("    -ssh, --ssh               ssh with config name(-c to query)\n");
                        printf("    -n,   --number            decimal, binary(0b or 0B prefix), hex(0x or 0X prefix) transfer to one another\n");
                        printf("    -C,   --calc              wrapper caulucator above bc, in zsh when use multiply('*') need to be quoted, so support replace 'x' for '*', 2x3 <==> 2*3 \n");
                        exit(EXIT_SUCCESS);
                }
        }
        // next loop do the init work
        int i = 1;
        while (i < argc) {
                char *flag = argv[i++];
                if (flag[0] != '-')
                        fatal("ERROR: invalid flag: %s", flag);
                if (strcmp(flag, "-c") == 0 || strcmp(flag, "--config") == 0) {
                        app->config_print = true;
                } else if (strcmp(flag, "-qb") == 0 || strcmp(flag, "--query_batch") == 0) {
                        app->db_print_batch = true;
                } else if (strcmp(flag, "-ql") == 0 || strcmp(flag, "--query_load") == 0) {
                        app->db_print_load = true;
                } else if (strcmp(flag, "-qss") == 0 || strcmp(flag, "--query_sniff_shake") == 0) {
                        app->db_print_sniff_and_shake = true;
                } else if (strcmp(flag, "-qd") == 0 || strcmp(flag, "--query_dump") == 0) {
                        app->db_print_dump = true;
                } else if (strcmp(flag, "-qw") == 0 || strcmp(flag, "--query_wash") == 0) {
                        app->db_print_wash = true;
                } else if (strcmp(flag, "-C") == 0 || strcmp(flag, "--calc") == 0) {
                        if (i == argc)
                                fatal("ERROR: flag(%s) not provide value", flag);
                        char *first_arg = argv[i++];
                        if (is_flag(first_arg))
                                fatal("ERROR: flag(%s) not provide value", flag);
                        char **calculator_args = calloc(1, sizeof(*calculator_args) * 128);
                        if (!calculator_args)
                                fatal("ERROR: app_init calculator_args calloc fail");
                        int parameters_len = 0;
                        calculator_args[parameters_len++] = strdup(first_arg);
                        for (; i < argc && !is_flag(argv[i]); ++i) {
                                calculator_args[parameters_len++] = strdup(argv[i]);
                        }
                        // support 'x' -> '*'
                        for (int j = 0; j < parameters_len; ++j) {
                                char *x = NULL;
                                do {
                                        x = strchr(calculator_args[j], 'x');
                                        if (x != NULL)
                                                *x = '*';
                                } while (x != NULL);
                        }
                        app->calculator_args = calculator_args;
                } else {
                        if (i == argc)
                                fatal("ERROR: flag(%s) not provide value", flag);
                        if (strcmp(flag, "-cv") == 0 || strcmp(flag, "--config_value") == 0) {
                                app->config_query_name = strdup(argv[i++]);
                        } else if (strcmp(flag, "-qln") == 0 || strcmp(flag, "--query_line") == 0) {
                                app->db_query_line = strdup(argv[i++]);
                        } else if (strcmp(flag, "-qt") == 0 || strcmp(flag, "--query_time") == 0) {
                                app->db_query_time = strdup(argv[i++]);
                        } else if (strcmp(flag, "-t") == 0 || strcmp(flag, "--timestamp") == 0) {
                                app->timestamp = strdup(argv[i++]);
                        } else if (strcmp(flag, "-ssh") == 0 || strcmp(flag, "--ssh") == 0) {
                                app->ssh_with_config_name = strdup(argv[i++]);
                        } else if (strcmp(flag, "-n") == 0 || strcmp(flag, "--number") == 0) {
                                app->number = strdup(argv[i++]);
                        }
                }
        }
        if (!app->db_query_line) app->db_query_line = strdup("3");
        if (!app->db_query_time) {
                char now_string[32] = {0};
                time_t now = time(NULL);
                struct tm *time = localtime(&now);
                strftime(now_string, sizeof(now_string), "%Y-%m-%d %H:%M:%S", time);
                app->db_query_time = strdup(now_string);
        }
        return app;
}

void app_run(struct App *app)
{
        if (app->config_print) {
                struct Config *config = config_init();
                config_print(config);
                config_destroy(config);
                return;
        }
        if (app->config_query_name) {
                struct Config *config = config_init();
                for (int i = 0; i < config->len; ++i) {
                        if (strcmp(app->config_query_name, config->items[i].name) == 0) {
                                printf("%s\n", config->items[i].value);
                                write_to_clipboard(config->items[i].value);
                                return;
                        }
                }
                printf("ERROR: name '%s' not found in config.", app->config_query_name);
                if (config->len > 0) {
                        printf("\nExist config: ");
                        for (int i = 0; i < config->len; ++i)
                                printf("[%s]", config->items[i].name);
                        putc('\n', stdout);
                } else {
                        printf(" No config was found, should be installed with config.txt\n");
                }
                return;
        }
        if (app->db_print_batch) {
                char *query = "SELECT equipment_code, is_end, created_at FROM production_batch order by id desc;";
                printf("%s\n", query);
                write_to_clipboard(query);
                return;
        }
        if (app->db_print_load) {
                char query[256];
                sprintf(query, "SELECT cid, load, alarm_at FROM alarm WHERE equipment_code = '%s线' AND load = true AND alarm_at < '%s' ORDER BY id DESC LIMIT 100;", app->db_query_line, app->db_query_time);
                printf("%s\n", query);
                write_to_clipboard(query);
                return;
        }
        if (app->db_print_sniff_and_shake) {
                char query1[256];
                char query2[256];
                sprintf(query1, "SELECT cid, sniff, alarm_at FROM alarm WHERE equipment_code = '%s线' and sniff = true AND alarm_at < '%s' ORDER BY id DESC LIMIT 100;", app->db_query_line, app->db_query_time);
                sprintf(query2, "SELECT cid, shake, alarm_at FROM alarm WHERE equipment_code = '%s线' AND shake = true AND alarm_at < '%s' ORDER BY id DESC LIMIT 100;", app->db_query_line, app->db_query_time);
                printf("%s\n%s\n", query1, query2);
                write_to_clipboard(query1);
                return;
        }
        if (app->db_print_dump) {
                char query[256];
                sprintf(query, "SELECT cid, dump, alarm_at, package_type FROM alarm WHERE equipment_code = '%s线' AND dump = true AND alarm_at < '%s' ORDER BY id DESC LIMIT 100;", app->db_query_line, app->db_query_time);
                printf("%s\n", query);
                write_to_clipboard(query);
        }
        if (app->db_print_wash) {
                char query[256];
                sprintf(query, "SELECT cid, wash, alarm_at, package_type FROM alarm WHERE equipment_code = '%s线' AND wash = true AND alarm_at < '%s' ORDER BY id DESC LIMIT 100;", app->db_query_line, app->db_query_time);
                printf("%s\n", query);
                write_to_clipboard(query);
                return;
        }
        if (app->timestamp) {
                bool is_epoch = true;
                size_t len = strlen(app->timestamp);
                for (size_t i = 0; i < len; ++i) {
                        if (app->timestamp[i] == '-') {
                                is_epoch = false;
                                break;
                        }
                }
                if (is_epoch && len != EPOCH_SECCOND_LEN && len != EPOCH_MILLISECOND_LEN)
                        fatal("ERROR: invalid timestamp length, can only be 10(second) or 13(millisecond)");
                if (is_epoch) {
                        time_t epoch = (time_t)str_to_long(app->timestamp, 10);
                        struct tm *broken_down_time = localtime(&epoch);
                        char format_time[64];
                        strftime(format_time, sizeof(format_time), "%Y-%m-%d %H:%M:%S", broken_down_time);
                        printf("%s\n", format_time);
                        write_to_clipboard(format_time);
                } else {
                        struct tm broken_down_time;
                        char *rc = strptime(app->timestamp, "%Y-%m-%d %H:%M:%S", &broken_down_time);
                        if (rc == NULL || *rc != '\0')
                                fatal("ERROR: format time only support YYYY-MM-DD hh:mm:ss format(2025-11-12 11:33:22)");
                        time_t epoch_second = mktime(&broken_down_time);
                        char epoch_second_string[EPOCH_SECCOND_LEN+1] = {0};
                        sprintf(epoch_second_string, "%ld", epoch_second);
                        printf("%s\n", epoch_second_string);
                        write_to_clipboard(epoch_second_string);
                }
                return;
        }
        if (app->number) {
                char first_bit = app->number[0];
                char second_bit = app->number[1];
                long decimal;
                char *binary = NULL;
                char *hex = NULL;
                if (first_bit == '0') {
                        if (second_bit == 'b' || second_bit == 'B') {
                                // binary
                                decimal = binary_to_decimal(app->number);
                                hex = binary_to_hex(app->number);
                                printf("decimal -- %ld\n", decimal);
                                printf("binary  -- %s\n", app->number);
                                printf("hex     -- %s\n", hex);
                                free(hex);
                        } else if (second_bit == 'x' || second_bit == 'X') {
                                // hex
                                decimal = hex_to_decimal(app->number);
                                binary = hex_to_binary(app->number);
                                printf("decimal -- %ld\n", decimal);
                                printf("binary  -- %s\n", binary);
                                printf("hex     -- %s\n", app->number);
                                free(binary);
                        } else {
                                fatal("unknown format: %s, only support decimal, binary(0b or 0B) and hex(0x or 0X)", app->number);
                        }
                } else {
                        // decimal
                        binary = decimal_to_binary(app->number);
                        hex = decimal_to_hex(app->number);
                        printf("decimal -- %s\n", app->number);
                        printf("binary  -- %s\n", binary);
                        printf("hex     -- %s\n", hex);
                        free(binary);
                        free(hex);
                }
                return;
        }
        if (app->calculator_args) {
                size_t cmd_len = 16; // reserve length for bc <<< ""
                for (int i = 0; app->calculator_args[i] != NULL; ++i)
                        cmd_len += strlen(app->calculator_args[i]);
                char *cmd = calloc(1, sizeof(*cmd) * cmd_len);
                if (cmd == NULL)
                        fatal("ERROR: app_run app->calculator_args cmd calloc fail");
                strcat(cmd, "bc <<< \"");
                for (int i = 0; app->calculator_args[i] != NULL; ++i)
                        strcat(cmd, app->calculator_args[i]);
                strcat(cmd, "\"");
                FILE *pipe = popen(cmd, "r");
                if (pipe == NULL)
                        fatal("ERROR: app_run popen fail");
                char buf[1024];
                while (fgets(buf, sizeof(buf), pipe) != NULL) {
                        printf(buf);
                        buf[strlen(buf) - 1] = '\0'; // remove the new line character for clipboard
                        write_to_clipboard(buf);
                }
                return;
        }
        if (app->ssh_with_config_name) {
                struct timeval start;
                struct timeval end;
                struct Config *config = config_init();
                int i = 0;
                for (; i < config->len; i++) {
                        if (strcmp(app->ssh_with_config_name, config->items[i].name) == 0)
                                break;
                }
                if (i == config->len) {
                        printf("ERROR: can't find ssh config name %s. Exist ssh config: ", app->ssh_with_config_name);
                        bool once = false;
                        for (int i = 0; i < config->len; ++i) {
                                if (strstr(config->items[i].key, "ssh") != NULL) {
                                        once = true;
                                        printf("[%s]", config->items[i].name);
                                }
                        }
                        if (!once)
                                printf("NULL.");
                        putc('\n', stdout);
                        config_destroy(config);
                        return;
                }
                int max_args = 128;
                char **args = malloc(sizeof(*args) * (max_args + 2)); // 1 for ssh program and 1 for NULL terminator
                if (!args) {
                        config_destroy(config);
                        fatal("ERROR: app_run args malloc fail");
                }
                args[0] = strdup("ssh");
                int index = 1;
                char *token = strtok(config->items[i].key, " \t"); // first token is ssh, just ignore it
                while (index < max_args) {
                        token = strtok(NULL, " \t");
                        if (token == NULL) {
                                args[index] = NULL;
                                break;
                        }
                        args[index++] = strdup(token);
                }
                write_to_clipboard(config->items[i].value);
                config_destroy(config);
                gettimeofday(&start, NULL);
                int rc = fork();
                if (rc == -1)
                        fatal("ERROR: app_run fork fail");
                if (rc == 0) {
                        execvp("ssh", args);
                        fatal("ERROR: app_run execvp should never return");
                }
                wait(NULL);
                gettimeofday(&end, NULL);
                long second_diff = end.tv_sec - start.tv_sec;
                long hour = second_diff / 3600;
                long minute = second_diff / 60;
                long second = second_diff % 60;
                char buf[128];
                if (hour > 0) {
                        sprintf(buf, ANSI_COLOR_YELLOW "session last: %ld hours, %ld minutes, %ld seconds" ANSI_COLOR_RESET, hour, minute, second);
                } else if (minute > 0) {
                        sprintf(buf, ANSI_COLOR_YELLOW "session last: %ld minutes, %ld seconds" ANSI_COLOR_RESET, minute, second);
                } else {
                        sprintf(buf, ANSI_COLOR_YELLOW "session last: %ld seconds" ANSI_COLOR_RESET, second);
                }
                printf("%s\n", buf);
                return;
        }
}

void app_destroy(struct App *app)
{
        if (app->config_query_name)
                free(app->config_query_name);
        if (app->db_query_line)
                free(app->db_query_line);
        if (app->db_query_time)
                free(app->db_query_time);
        if (app->timestamp)
                free(app->timestamp);
        if (app->ssh_with_config_name)
                free(app->ssh_with_config_name);
        if (app->number)
                free(app->number);
        if (app->calculator_args) {
                for (int i = 0; app->calculator_args[i] != NULL; ++i)
                        free(app->calculator_args[i]);
                free(app->calculator_args);
        }
        free(app);
}

int main(int argc, char **argv)
{
        struct App *app = app_init(argc, argv);
        app_run(app);
        app_destroy(app);
        return 0;
}
