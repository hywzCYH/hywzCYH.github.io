// sqp_calc.cpp
// 编译：g++ -O3 -std=c++17 -pthread sqp_calc.cpp -o sqp_calc
//   macOS/iOS(ssh) 或 Linux 都可以，也可以 clang++
//
// 用法示例：
//   ./sqp_calc --prefix HYY --team HYWZ \
//              --start 0 --end 100000000 \
//              --threads 8 --block 100000 \
//              --e8 672 --sqp 1000 \
//              --model qp0.txt --out hits.txt
//
// 模型文件格式：任意包含 1124 个浮点数（逗号或空格分隔，可以是 JSON 数组，
// 也可以是 JS 的 const model = [...]），本程序会自动提取括号内数值。

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

// ============ 常量 ============
constexpr int N = 256;
constexpr int M = 128;
constexpr int K = 64;
constexpr int SKILL_CNT = 40;
constexpr int XP_SIZE = 1124;   // qp0 系数长度
constexpr int X_LEN = 46;

// ============ 配置 ============
struct Config {
    std::string prefix;
    std::string team;
    long long start = 0;
    long long end = 10000000;
    int threads = 8;
    double th_e8 = 672.0;
    double th_sqp = -1e18;
    long long block = 100000;
    std::string modelPath;
    std::string outPath = "hits.txt";
} cfg;

// ============ 模型 ============
std::vector<double> qp0;

// ============ 名字对象 ============
struct Name {
    uint8_t val_base[N];
    uint8_t val[N];
    uint8_t name_base[M];
    int q_len = 0;
    int skill[SKILL_CNT];
    int freq[SKILL_CNT];
    int p = 0, q = 0;

    void load_team(const std::string& team);
    void load_name(const std::string& sname);
    uint8_t m();
    int gen();
    void calc_props(int props[8]);
    void calc_skills();
};

void Name::load_team(const std::string& team) {
    std::vector<int> bytes;
    bytes.reserve(team.size());
    for (unsigned char c : team) bytes.push_back((int)c - 256);  // 与 JS 一致，减 256
    int t_len = (int)bytes.size() + 1;
    int s = 0;
    for (int i = 0; i < N; i++) val_base[i] = (uint8_t)i;
    for (int i = 0; i < N; i++) {
        if (i % t_len != 0) s += bytes[i % t_len - 1];
        s += (int)val_base[i];
        s = ((s % 256) + 256) % 256;
        std::swap(val_base[s], val_base[i]);
    }
}

void Name::load_name(const std::string& sname) {
    std::vector<int> bytes;
    bytes.reserve(sname.size());
    for (unsigned char c : sname) bytes.push_back((int)c);
    int n_len = (int)bytes.size() + 1;
    q_len = 0;
    std::memcpy(val, val_base, N);
    int s;
    for (int a = 0; a < 2; a++) {
        s = 0;
        for (int i = 0; i < N; i++) {
            if (i % n_len != 0) s += bytes[i % n_len - 1];
            s += (int)val[i];
            s = ((s % 256) + 256) % 256;
            std::swap(val[s], val[i]);
        }
    }
    for (int i = 0; i < N; i++) {
        int m_val = ((int)val[i] * 181 + 160) & 0xFF;
        if (m_val >= 89 && m_val < 217) {
            if (q_len < M) name_base[q_len] = (uint8_t)(m_val & 63);
            q_len++;
        }
    }
    for (int i = q_len; i < M; i++) name_base[i] = 0;  // 等价于 JS 里 undefined 的处理
}

uint8_t Name::m() {
    p++;
    if (p >= N) p = 0;
    q = (q + (int)val[p]) & 0xFF;
    std::swap(val[p], val[q]);
    return val[((int)val[p] + (int)val[q]) & 255];
}

int Name::gen() {
    uint8_t u = m();
    uint8_t v = m();
    return (((int)u << 8) | (int)v) % SKILL_CNT;
}

static inline int med3(int a, int b, int c) {
    int t;
    if (a > b) { t = a; a = b; b = t; }
    if (b > c) { t = b; b = c; c = t; }
    if (a > b) { t = a; a = b; b = t; }
    return b;
}

void Name::calc_props(int props[8]) {
    int r[10];
    for (int i = 0; i < 10; i++) r[i] = name_base[i];
    std::sort(r, r + 10);
    int pc = 0;
    for (int p = 10; p < 31; p += 3) {
        props[pc++] = med3(name_base[p], name_base[p+1], name_base[p+2]);
    }
    props[pc++] = 154 + r[3] + r[4] + r[5] + r[6];  // HP
}

void Name::calc_skills() {
    for (int i = 0; i < SKILL_CNT; i++) skill[i] = i;
    for (int i = 0; i < SKILL_CNT; i++) freq[i] = 0;
    p = 0; q = 0;
    int s = 0;
    for (int zz = 0; zz < 2; zz++) {
        for (int i = 0; i < SKILL_CNT; i++) {
            s = (s + gen() + skill[i]) % SKILL_CNT;
            std::swap(skill[s], skill[i]);
        }
    }
    int last = -1;
    for (int i = 0, j = 0; i < K; i += 4, j++) {
        int a = name_base[K + i];
        int b = name_base[K + i + 1];
        int c = name_base[K + i + 2];
        int d = name_base[K + i + 3];
        int mn = std::min(std::min(a, b), std::min(c, d));
        if (mn > 10 && skill[j] < 35) {
            freq[j] = mn - 10;
            if (skill[j] < 25) last = j;
        }
    }
    if (last != -1) freq[last] <<= 1;
    if (freq[14] && last != 14)
        freq[14] += std::min(std::min(name_base[60], name_base[61]), freq[14]);
    if (freq[15] && last != 15)
        freq[15] += std::min(std::min(name_base[62], name_base[63]), freq[15]);
}

// ============ 模型加载 ============
static std::vector<double> load_model_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        fprintf(stderr, "无法打开模型文件: %s\n", path.c_str());
        exit(1);
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    // 只提取 [] 之间的内容，兼容 const model = [...] / [...]
    size_t l = content.find('[');
    size_t r = content.rfind(']');
    if (l != std::string::npos && r != std::string::npos && r > l)
        content = content.substr(l + 1, r - l - 1);

    std::vector<double> v;
    v.reserve(2000);
    std::string num;
    for (char c : content) {
        if (c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' ||
            (c >= '0' && c <= '9')) {
            num += c;
        } else {
            if (!num.empty()) {
                try { v.push_back(std::stod(num)); } catch (...) {}
                num.clear();
            }
        }
    }
    if (!num.empty()) {
        try { v.push_back(std::stod(num)); } catch (...) {}
    }
    return v;
}

// ============ 多线程调度 ============
std::atomic<long long> g_nextStart{0};
std::atomic<long long> g_hitCount{0};
std::atomic<long long> g_done{0};
std::mutex g_outMutex;
FILE* g_outFile = nullptr;

static void worker_thread(long long totalEnd, double th_e8, double th_sqp) {
    Name name_main, name_shadow;
    name_main.load_team(cfg.team);
    name_shadow.load_team(cfg.team);

    std::string nameBuf;
    nameBuf.reserve(cfg.prefix.size() + 24);

    while (true) {
        long long s = g_nextStart.fetch_add(cfg.block);
        if (s > totalEnd) break;
        long long e = std::min(s + cfg.block - 1, totalEnd);

        for (long long n = s; n <= e; n++) {
            nameBuf = cfg.prefix;
            nameBuf += std::to_string(n);
            name_main.load_name(nameBuf);

            // ---- 八围 ----
            int props[8];
            name_main.calc_props(props);
            double hp = (double)props[7];
            double e8 = hp / 3.0
                      + (props[0] + 36) + (props[1] + 36) + (props[2] + 36)
                      + (props[3] + 36) + (props[4] + 36) + (props[5] + 36)
                      + (props[6] + 36);
            if (e8 < th_e8) continue;

            // ---- 技能 ----
            name_main.calc_skills();

            double x[X_LEN];
            x[0] = hp;
            for (int i = 0; i < 7; i++) x[i + 1] = (double)props[i] + 36.0;
            for (int i = 8; i < X_LEN; i++) x[i] = 0.0;

            double zd = 1.0, kill = 1.0;
            for (int k = 0; k < 16; k++) {
                int skId = name_main.skill[k];
                int f = name_main.freq[k];
                if (skId >= 35 || f == 0) continue;
                double fv = (double)f;
                if (skId == 9 || skId == 16)      { x[skId+8] = zd*fv;   zd   *= (1.0 - fv*0.00234375); }
                else if (skId == 18)              { x[skId+8] = zd*fv;   zd   *= (1.0 - fv*0.002734375); }
                else if (skId == 19 || skId == 23){ x[skId+8] = zd*fv;   zd   *= (1.0 - fv*0.0046875); }
                else if (skId == 20 || skId == 22){ x[skId+8] = zd*fv;   zd   *= (1.0 - fv*0.00546875); }
                else if (skId < 25)               { x[skId+8] = zd*fv;   zd   *= (1.0 - fv*0.0078125); }
                else if (skId == 31 || skId == 32){ x[skId+8] = kill*fv; kill *= (1.0 - fv*0.0078125); }
                else                              { x[skId+8] = fv; }
            }

            // ---- 护盾 x[37] 非线性 ----
            if (x[37] <= 70.0) x[37] = x[37] * x[37] / 70.0;
            else               x[37] = x[37] * 2.0 - 70.0;

            // ---- shadow ----
            if (x[32] > 0.0) {
                std::string shadowName = nameBuf + "?shadow";
                name_shadow.load_name(shadowName);
                int sp[8];
                name_shadow.calc_props(sp);
                double shadowi = (double)sp[0]*2.8 + (double)sp[1]*0.6
                               + (double)sp[2]*2.5 + (double)sp[3]*1.2
                               + (double)sp[4] + (double)sp[5]
                               - 1.2*(double)sp[6] + 0.8*(double)sp[7];
                x[43] = shadowi * x[32] / 100.0;
            } else {
                x[43] = 0.0;
            }
            if (x[42] > 0.0) x[44] = 1.0;
            if (x[37] > 0.0) { x[45] = x[26]; x[26] = 0.0; }

            // ---- 构造 xp 向量 ----
            double xp[1125];
            xp[1] = 1.0;
            int cnt = 2;
            for (int i = 0; i < X_LEN; i++) xp[cnt++] = x[i];
            for (int i = 0; i < X_LEN; i++) {
                for (int j = i; j < X_LEN; j++) {
                    if ((i == 26 && (j == 37 || j == 45)) ||
                        (i == 42 && j == 44) ||
                        (i == 44 && j == 44)) continue;
                    xp[cnt++] = x[i] * x[j];
                }
            }

            // ---- SQP 点积 ----
            double sqp = 0.0;
            for (int i = 0; i < XP_SIZE; i++) sqp += qp0[i] * xp[i + 1];

            if (sqp >= th_sqp) {
                g_hitCount.fetch_add(1);
                std::lock_guard<std::mutex> lk(g_outMutex);
                fprintf(g_outFile, "%s@%s SQP=%.0f e8=%.2f\n",
                        nameBuf.c_str(), cfg.team.c_str(), sqp, e8);
            }
        }
        g_done.fetch_add(e - s + 1);
    }
}

// ============ 参数解析 ============
static void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "参数 %s 需要值\n", name);
                exit(1);
            }
            return argv[++i];
        };
        if      (a == "--prefix")  cfg.prefix    = next("--prefix");
        else if (a == "--team")    cfg.team      = next("--team");
        else if (a == "--start")   cfg.start     = std::stoll(next("--start"));
        else if (a == "--end")     cfg.end       = std::stoll(next("--end"));
        else if (a == "--threads") cfg.threads   = std::stoi(next("--threads"));
        else if (a == "--e8")      cfg.th_e8     = std::stod(next("--e8"));
        else if (a == "--sqp")     cfg.th_sqp    = std::stod(next("--sqp"));
        else if (a == "--block")   cfg.block     = std::stoll(next("--block"));
        else if (a == "--model")   cfg.modelPath = next("--model");
        else if (a == "--out")     cfg.outPath   = next("--out");
        else {
            fprintf(stderr, "未知参数: %s\n", a.c_str());
            exit(1);
        }
    }
    if (cfg.team.empty())      { fprintf(stderr, "--team 必填\n");  exit(1); }
    if (cfg.modelPath.empty()) { fprintf(stderr, "--model 必填\n"); exit(1); }
    if (cfg.threads < 1)       cfg.threads = 1;
    if (cfg.block < 1000)      cfg.block = 1000;
}

// ============ 主函数 ============
int main(int argc, char** argv) {
    parse_args(argc, argv);

    qp0 = load_model_file(cfg.modelPath);
    if ((int)qp0.size() < XP_SIZE) {
        fprintf(stderr, "模型系数不足 %d 个，实际 %zu\n", XP_SIZE, qp0.size());
        return 1;
    }
    qp0.resize(XP_SIZE);

    g_outFile = fopen(cfg.outPath.c_str(), "w");
    if (!g_outFile) { perror("fopen out"); return 1; }
    setvbuf(g_outFile, nullptr, _IOFBF, 1 << 20);

    long long total = cfg.end - cfg.start + 1;
    fprintf(stderr,
        "==== 开始 ====\n"
        "前缀: %s  战队: %s\n"
        "区间: [%lld, %lld]  共 %lld 号\n"
        "线程: %d  每块: %lld\n"
        "阈值: e8 ≥ %.2f   SQP ≥ %.0f\n"
        "模型: %s  (%zu 系数)\n\n",
        cfg.prefix.c_str(), cfg.team.c_str(),
        cfg.start, cfg.end, total,
        cfg.threads, cfg.block,
        cfg.th_e8, cfg.th_sqp,
        cfg.modelPath.c_str(), qp0.size());

    g_nextStart.store(cfg.start);
    auto t0 = std::chrono::steady_clock::now();

    // 启动 worker
    std::vector<std::thread> workers;
    workers.reserve(cfg.threads);
    for (int t = 0; t < cfg.threads; t++)
        workers.emplace_back(worker_thread, cfg.end, cfg.th_e8, cfg.th_sqp);

    // 进度上报
    bool finished = false;
    std::thread reporter([&]() {
        while (!finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (finished) break;
            long long done = g_done.load();
            auto now = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(now - t0).count();
            double rate = done / std::max(sec, 1e-6);
            double eta = rate > 0 ? (total - done) / rate : 0.0;
            fprintf(stderr,
                "进度: %lld / %lld (%.2f%%) | 命中 %lld | 已用 %.1fs | 速度 %.1fw/s | 预计 %.1fs\n",
                done, total, 100.0 * done / total, g_hitCount.load(),
                sec, rate / 10000.0, eta);
        }
    });

    for (auto& th : workers) th.join();
    finished = true;
    if (reporter.joinable()) reporter.join();

    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    double rate = total / sec;
    long long hit = g_hitCount.load();

    fprintf(stderr,
        "\n==== 完成 ====\n"
        "总号数: %lld\n"
        "命中数: %lld\n"
        "耗时:   %.3f 秒\n"
        "速度:   %.2f 万/秒 (%.2f 亿/秒)\n",
        total, hit, sec, rate / 10000.0, rate / 1e8);

    fclose(g_outFile);
    return 0;
}
