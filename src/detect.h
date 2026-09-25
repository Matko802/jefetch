#ifndef JEFETCH_DETECT_H
#define JEFETCH_DETECT_H

#include <stddef.h>

char *detect_read_file(const char *path);
char **detect_read_file_lines(const char *path, size_t *n);
void detect_free_lines(char **lines, size_t n);

typedef struct {
    char *key;
    char *val;
} DetectKV;

DetectKV *detect_parse_kv_file(const char *path, size_t *n);
void detect_free_kv(DetectKV *kv, size_t n);
void detect_unquote(char *v);
char *detect_getenv(const char *name);
char *detect_run_capture(const char *cmd, const char *const *args);
char **detect_run_capture_lines(const char *cmd, const char *const *args, size_t *n);
char *detect_run_capture_timeout(const char *cmd, const char *const *args,
                                 unsigned timeout_ms);
char *detect_scan_proc_comm(const char *const *names, size_t n);

typedef struct {
    unsigned pid;
    unsigned ppid;
    char comm[256];
    char exe_path[1024];
    char *cmdline;
} ProcInfo;

int detect_proc_by_comm(const char *const *names, size_t n, ProcInfo *out);
void detect_proc_free(ProcInfo *p);

typedef struct {
    unsigned long long uptime_secs;
    unsigned long long boot_time_secs;
} UptimeInfo;

void detect_uptime(UptimeInfo *out);

typedef struct {
    char sysname[64];
    char release[128];
    char version[256];
} KernelInfo;

void detect_kernel(KernelInfo *out);

typedef struct {
    unsigned long long mem_total;
    unsigned long long mem_used;
    unsigned long long mem_free;
    unsigned long long mem_available;
    unsigned long long mem_buffers;
    unsigned long long mem_cached;
    unsigned long long swap_total;
    unsigned long long swap_used;
    unsigned long long swap_free_val;
} MemoryInfo;

void detect_memory(MemoryInfo *out);

typedef struct {
    char name[128];
} DeInfo;

void detect_de(DeInfo *out);

typedef struct {
    char user_name[256];
    char user_name_part[256];
    char host_name[256];
    char host_name_part[256];
} UserInfo;

void detect_user(UserInfo *out);

typedef struct {
    char user[64];
    char tty[64];
    char host[256];
} LoggedUser;

LoggedUser *detect_users(size_t *n);
void detect_users_free(LoggedUser *u, size_t n);
typedef struct {
    char name[128];
    char manufacturer[128];
    char model[128];
    char technology[64];
    unsigned capacity_percent;
    char status[64];
    double energy_now;
    double energy_full;
    double temp_c;
    unsigned long long voltage_mv;
} BatteryInfo;

BatteryInfo *detect_battery(size_t *n);
void detect_battery_free(BatteryInfo *b, size_t n);

typedef struct {
    char name[128];
    unsigned long long value;
    unsigned long long max;
    unsigned percentage;
} BrightnessInfo;

BrightnessInfo *detect_brightness(size_t *n);
void detect_brightness_free(BrightnessInfo *b, size_t n);

typedef struct {
    char name[256];
    char vendor[256];
    char version[128];
    char date[128];
} BoardInfo;

void detect_board(BoardInfo *out);
void detect_board_product_name(char *out, size_t n);

typedef struct {
    char name[128];
    char version[64];
} LoginManagerInfo;

void detect_lm(LoginManagerInfo *out);

typedef struct {
    char name[64];
    char version[128];
} InitSystemInfo;

void detect_initsystem(InitSystemInfo *out);

typedef struct {
    char name[128];
    char version[64];
    char session_type[16];
} WmInfo;

void detect_wm(WmInfo *out);

typedef struct {
    char **servers;
    size_t nservers;
    char domain[256];
} DnsInfo;

int detect_dns(DnsInfo *out);
void detect_dns_free(DnsInfo *d);
typedef struct {
    char name[256];
    char version[64];
    char version_id[64];
    char id[64];
    char id_like[128];
    char pretty_name[256];
    char arch[32];
    char build_id[128];
    char codename[128];
    char variant[128];
    char variant_id[128];
} OsInfo;

void detect_os(OsInfo *out);
void detect_arch(char *out, size_t n);

typedef struct {
    char mountpoint[1024];
    char mount_from[1024];
    char filesystem[64];
    unsigned long long total;
    unsigned long long used;
    unsigned long long available;
    char options[1024];
    char name[256];
} DiskInfo;

DiskInfo *detect_disk(const char *const *folders, size_t nfolders, size_t *n);
void detect_disk_free(DiskInfo *d, size_t n);

typedef struct {
    char process_path[1024];
    char shell_path[1024];
    char shell_name[128];
    char shell_version[128];
    char shell_base_name[128];
} ShellInfo;

void detect_shell(ShellInfo *out);

typedef struct {
    char gtk_theme[256];
    char icon_theme[256];
    char cursor_theme[256];
    char font[256];
    unsigned font_size;
    char color_scheme[128];
    char desktop[128];
} GtkThemeInfo;

void detect_theme(GtkThemeInfo *out);

typedef struct {
    char *name;
    size_t count;
} PkgAmount;

typedef struct {
    PkgAmount *amounts;
    size_t n;
} PackagesInfo;

void detect_packages(PackagesInfo *out);
void detect_packages_free(PackagesInfo *p);

typedef struct {
    char name[256];
    char version[128];
    char font[256];
    char exe[1024];
} TerminalInfo;

void detect_terminal(TerminalInfo *out);

typedef struct {
    char name[128];
    char make[128];
    char model[256];
    unsigned width;
    unsigned height;
    unsigned refresh_mhz;
    int scale;
    int logical_width;
    int logical_height;
} WlOutput;

unsigned wl_refresh_hz(const WlOutput *o);
double wl_scale_factor(const WlOutput *o);
WlOutput *wl_query_outputs(size_t *n);
void wl_outputs_free(WlOutput *o, size_t n);

typedef struct {
    unsigned width;
    unsigned height;
    unsigned refresh_rate;
    unsigned size_in;
    char dtype[16];
    char name[128];
    char model[256];
    double scale;
} DisplayInfo;

void detect_display(DisplayInfo **out, size_t *n);
void detect_display_free(DisplayInfo *d, size_t n);
void detect_format_scale(double scale, char *out, size_t n);
typedef struct {
    char model[256];
    char vendor[128];
    size_t packages;
    size_t physical_cores;
    size_t logical_cores;
    unsigned long long freq_max_mhz;
    unsigned long long freq_cur_mhz;
    int has_pe_cores;
    size_t pe_cores;
    int has_ee_cores;
    size_t ee_cores;
} CpuInfo;

void detect_cpu(CpuInfo *out);
void detect_cpu_march(char *out, size_t n);
unsigned long long detect_numa_nodes(void);
typedef struct {
    char name[64];
    char **ipv4;
    unsigned char *prefix4;
    size_t n4;
    char **ipv6;
    unsigned char *prefix6;
    size_t n6;
    char mac[32];
    unsigned long long mtu;
    unsigned long long speed;
    char flags[64];
} IpInfo;

IpInfo *detect_localip(size_t *n);
void detect_localip_free(IpInfo *p, size_t n);
int detect_default_iface(char *out, size_t n);
int detect_is_virtual(const char *name);
int detect_outbound_src_ip(char *out, size_t n);
int detect_hostname_hint(char *out, size_t n);

typedef struct {
    char vendor[32];
    char vendor_name[32];
    char model[256];
    char driver[128];
    char device_id[32];
    char dtype[16];
} GpuInfo;

GpuInfo *detect_gpu(size_t *n);
void detect_gpu_free(GpuInfo *g, size_t n);
typedef struct {
    char name[64];
    char ssid[128];
    unsigned signal_quality;
    char protocol[16];
    char security[64];
} WifiInfo;

WifiInfo *detect_wifi(size_t *n);
void detect_wifi_free(WifiInfo *w, size_t n);

int detect_publicip(char *out, size_t n, unsigned timeout_ms);

#endif
