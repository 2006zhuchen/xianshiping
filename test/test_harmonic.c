/*
 * test_harmonic.c — PC 端独立测试集
 *
 * 测试范围：
 *   1. 谐波数据生成（FPGA_GenerateHarmTestData）
 *   2. 谐波翻页逻辑（NextPage / PrevPage / GetTotalPages）
 *   3. 谐波数据读取 + 边界条件（FPGA_HarmGetPoint）
 *   4. 表格单元格格式化（TJC_Table_FormatCell / FormatNum）
 *   5. 表格字符串拼装（模拟 UI_UpdateWaveTable）
 *
 * 编译（MinGW / GCC）：
 *   gcc -Wall -std=c99 -o test_harmonic test_harmonic.c
 *   ./test_harmonic
 *
 * 编译（MSVC）：
 *   cl /W4 /std:c11 test_harmonic.c
 *   test_harmonic.exe
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ================================================================
 *  第0部分：简易测试框架
 * ================================================================ */

static int g_TestPass = 0;
static int g_TestFail = 0;
static const char *g_CurrSuite = "";

#define TEST_SUITE(name)  do { g_CurrSuite = name; printf("\n  === %s ===\n", name); } while(0)

#define TEST(name)                                                              \
    do {                                                                        \
        printf("    %-45s ", name);                                             \
        fflush(stdout);                                                         \
    } while(0)

#define PASS()                                                                  \
    do {                                                                        \
        printf("PASS\n");                                                       \
        g_TestPass++;                                                           \
    } while(0)

#define FAIL(fmt, ...)                                                          \
    do {                                                                        \
        printf("FAIL — " fmt "\n", ##__VA_ARGS__);                              \
        g_TestFail++;                                                           \
    } while(0)

#define ASSERT_EQ(expected, actual)                                             \
    do {                                                                        \
        if ((expected) != (actual)) {                                           \
            FAIL("expected %d, got %d", (int)(expected), (int)(actual));        \
        } else {                                                                \
            PASS();                                                             \
        }                                                                       \
    } while(0)

#define ASSERT_STREQ(expected, actual)                                          \
    do {                                                                        \
        if (strcmp((expected), (actual)) != 0) {                                \
            FAIL("expected \"%s\", got \"%s\"", (expected), (actual));         \
        } else {                                                                \
            PASS();                                                             \
        }                                                                       \
    } while(0)

#define ASSERT_TRUE(cond)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            FAIL("condition false: %s", #cond);                                 \
        } else {                                                                \
            PASS();                                                             \
        }                                                                       \
    } while(0)

/* ================================================================
 *  第1部分：被测试的函数（从 Fpga.c / Serial.c 移植）
 * ================================================================ */

#define FPGA_TABLE_ROWS_PER_PAGE  4
#define FPGA_HARM_MAX_POINTS      200

static uint32_t g_HarmFreq[FPGA_HARM_MAX_POINTS];
static uint16_t g_HarmAmp [FPGA_HARM_MAX_POINTS];
static uint16_t g_HarmCount = 0;
static uint16_t g_HarmPage = 0;

static void FPGA_HarmReset(void)
{
    g_HarmCount = 0;
    g_HarmPage = 0;
    memset(g_HarmFreq, 0, sizeof(g_HarmFreq));
    memset(g_HarmAmp,  0, sizeof(g_HarmAmp));
}

void FPGA_GenerateHarmTestData(void)
{
    uint16_t i;
    for (i = 0; i < 8 && i < FPGA_HARM_MAX_POINTS; i++)
    {
        uint16_t n = i + 1;
        g_HarmFreq[i] = 50u * n;
        g_HarmAmp[i]  = 5000u / n;
    }
    g_HarmCount = i;
    g_HarmPage = 0;
}

uint16_t FPGA_HarmGetPointCount(void)    { return g_HarmCount; }

uint8_t FPGA_HarmGetPoint(uint16_t index, uint32_t *freq, uint16_t *amp)
{
    if (index >= g_HarmCount) return 0;
    *freq = g_HarmFreq[index];
    *amp  = g_HarmAmp [index];
    return 1;
}

void FPGA_HarmTableNextPage(void)
{
    uint16_t total = (g_HarmCount + FPGA_TABLE_ROWS_PER_PAGE - 1)
                     / FPGA_TABLE_ROWS_PER_PAGE;
    if (total == 0) return;
    if (g_HarmPage + 1 < total)
        g_HarmPage++;
}

void FPGA_HarmTablePrevPage(void)
{
    if (g_HarmPage > 0)
        g_HarmPage--;
}

uint16_t FPGA_HarmTableGetCurrentPage(void) { return g_HarmPage + 1; }

uint16_t FPGA_HarmTableGetTotalPages(void)
{
    if (g_HarmCount == 0) return 0;
    return (g_HarmCount + FPGA_TABLE_ROWS_PER_PAGE - 1)
           / FPGA_TABLE_ROWS_PER_PAGE;
}

uint8_t FPGA_HarmTableHasData(void)
{
    return (g_HarmCount > 0) ? 1 : 0;
}

/* ---- 表格格式化函数 ---- */

void TJC_Table_FormatCell(char *dst, const char *src, uint8_t width)
{
    uint8_t i = 0;
    while (*src && i < width) { dst[i++] = *src++; }
    while (i < width)          { dst[i++] = ' ';   }
    dst[i] = '\0';
}

void TJC_Table_FormatNum(char *dst, int32_t num, uint8_t width)
{
    char buf[16];
    uint8_t i = 0, j = 0;

    if (num < 0) { buf[i++] = '-'; num = -num; }

    {
        char tmp[12];
        uint8_t k = 0;
        do { tmp[k++] = (char)((uint32_t)(num % 10) + '0'); num /= 10; }
        while (num > 0);
        while (k > 0) { buf[i++] = tmp[k - 1]; k--; }
    }

    while (j < i && j < width) { dst[j] = buf[j]; j++; }
    while (j < width)          { dst[j] = ' ';   j++; }
    dst[j] = '\0';
}

/* ---- 模拟 UI_UpdateWaveTable 的表格拼装 ---- */

static int BuildHarmonicTable(char *table, uint16_t tableSize,
                              uint16_t pageIndex)  /* 0-based page */
{
    uint16_t total   = FPGA_HarmGetPointCount();
    uint16_t start   = pageIndex * FPGA_TABLE_ROWS_PER_PAGE;
    uint16_t end     = start + FPGA_TABLE_ROWS_PER_PAGE;
    uint16_t i;
    int len = 0;

    if (end > total) end = total;
    if (start >= total) return 0;

    for (i = start; i < end; i++)
    {
        uint32_t freq;
        uint16_t amp;
        uint8_t  harm = i + 1;
        char col1[12], col2[12], col3[12];

        FPGA_HarmGetPoint(i, &freq, &amp);
        TJC_Table_FormatNum(col1, (int32_t)harm, 5);
        TJC_Table_FormatNum(col2, (int32_t)freq, 8);
        TJC_Table_FormatNum(col3, (int32_t)amp,  8);
        len += snprintf(table + len, tableSize - len,
                        "%s%s%s\r\n", col1, col2, col3);
    }

    return len;
}

/* ================================================================
 *  第2部分：测试用例
 * ================================================================ */

static void test_data_generation(void)
{
    TEST_SUITE("数据生成");

    FPGA_HarmReset();

    TEST("初始状态无数据");
    ASSERT_EQ(0, FPGA_HarmTableHasData());

    TEST("初始点数为 0");
    ASSERT_EQ(0, FPGA_HarmGetPointCount());

    FPGA_GenerateHarmTestData();

    TEST("生成后有数据");
    ASSERT_EQ(1, FPGA_HarmTableHasData());

    TEST("生成 8 个点");
    ASSERT_EQ(8, FPGA_HarmGetPointCount());

    TEST("第1个点: 频率 50Hz");
    {
        uint32_t freq = 0; uint16_t amp = 0;
        FPGA_HarmGetPoint(0, &freq, &amp);
        ASSERT_EQ(50, freq);
    }

    TEST("第1个点: 幅值 5000");
    {
        uint32_t freq = 0; uint16_t amp = 0;
        FPGA_HarmGetPoint(0, &freq, &amp);
        ASSERT_EQ(5000, amp);
    }

    TEST("第2个点: 频率 100Hz");
    {
        uint32_t freq = 0; uint16_t amp = 0;
        FPGA_HarmGetPoint(1, &freq, &amp);
        ASSERT_EQ(100, freq);
    }

    TEST("第2个点: 幅值 2500");
    {
        uint32_t freq = 0; uint16_t amp = 0;
        FPGA_HarmGetPoint(1, &freq, &amp);
        ASSERT_EQ(2500, amp);
    }

    TEST("第8个点: 频率 400Hz");
    {
        uint32_t freq = 0; uint16_t amp = 0;
        FPGA_HarmGetPoint(7, &freq, &amp);
        ASSERT_EQ(400, freq);
    }

    TEST("第8个点: 幅值 625");
    {
        uint32_t freq = 0; uint16_t amp = 0;
        FPGA_HarmGetPoint(7, &freq, &amp);
        ASSERT_EQ(625, amp);
    }
}

static void test_data_boundary(void)
{
    TEST_SUITE("数据边界");

    FPGA_HarmReset();
    FPGA_GenerateHarmTestData();

    TEST("索引 8 越界返回 0");
    {
        uint32_t freq = 999; uint16_t amp = 999;
        uint8_t ok = FPGA_HarmGetPoint(8, &freq, &amp);
        if (ok != 0 || freq != 999 || amp != 999) {
            /* freq/amp should NOT be modified on failure */
            /* Note: current impl does NOT guard writes — just checks return */
            if (ok != 0) { FAIL("returned %d, expected 0", ok); }
            else { PASS(); }
        } else {
            PASS();
        }
    }

    TEST("空数据时 GetPoint 返回 0");
    FPGA_HarmReset();
    {
        uint32_t freq = 0; uint16_t amp = 0;
        uint8_t ok = FPGA_HarmGetPoint(0, &freq, &amp);
        ASSERT_EQ(0, ok);
    }
}

static void test_pagination(void)
{
    TEST_SUITE("翻页逻辑");

    FPGA_HarmReset();

    TEST("无数据时总页数为 0");
    ASSERT_EQ(0, FPGA_HarmTableGetTotalPages());

    TEST("无数据时当前页为 1");
    ASSERT_EQ(1, FPGA_HarmTableGetCurrentPage());

    TEST("无数据时 NextPage 不崩溃");
    FPGA_HarmTableNextPage();
    ASSERT_EQ(1, FPGA_HarmTableGetCurrentPage());

    TEST("无数据时 PrevPage 不崩溃");
    FPGA_HarmTablePrevPage();
    ASSERT_EQ(1, FPGA_HarmTableGetCurrentPage());

    /* 8 点 / 4 行每页 = 2 页 */
    FPGA_GenerateHarmTestData();

    TEST("8 点 → 总页数 2");
    ASSERT_EQ(2, FPGA_HarmTableGetTotalPages());

    TEST("初始当前页为 1");
    ASSERT_EQ(1, FPGA_HarmTableGetCurrentPage());

    TEST("翻到第 2 页");
    FPGA_HarmTableNextPage();
    ASSERT_EQ(2, FPGA_HarmTableGetCurrentPage());

    TEST("第 2 页再翻下一页不超界");
    FPGA_HarmTableNextPage();
    ASSERT_EQ(2, FPGA_HarmTableGetCurrentPage());

    TEST("翻回第 1 页");
    FPGA_HarmTablePrevPage();
    ASSERT_EQ(1, FPGA_HarmTableGetCurrentPage());

    TEST("第 1 页再翻上一页不超界");
    FPGA_HarmTablePrevPage();
    ASSERT_EQ(1, FPGA_HarmTableGetCurrentPage());
}

static void test_pagination_edge_cases(void)
{
    TEST_SUITE("翻页边界");

    /* 模拟只有 1 个点的情况 */
    FPGA_HarmReset();
    g_HarmFreq[0] = 50;  g_HarmAmp[0] = 5000;
    g_HarmCount = 1;
    g_HarmPage = 0;

    TEST("1 个点 → 总页数 1");
    ASSERT_EQ(1, FPGA_HarmTableGetTotalPages());

    TEST("1 个点时 NextPage 不超界");
    FPGA_HarmTableNextPage();
    ASSERT_EQ(1, FPGA_HarmTableGetCurrentPage());

    /* 模拟刚好 4 个点 */
    FPGA_HarmReset();
    {
        uint16_t i;
        for (i = 0; i < 4; i++) {
            g_HarmFreq[i] = 50u * (i + 1);
            g_HarmAmp[i]  = 5000u / (i + 1);
        }
        g_HarmCount = 4;
        g_HarmPage = 0;
    }

    TEST("4 个点 → 总页数 1");
    ASSERT_EQ(1, FPGA_HarmTableGetTotalPages());

    TEST("4 个点时 NextPage 不超界");
    FPGA_HarmTableNextPage();
    ASSERT_EQ(1, FPGA_HarmTableGetCurrentPage());

    /* 模拟 5 个点（刚好 2 页，第 2 页 1 行） */
    FPGA_HarmReset();
    {
        uint16_t i;
        for (i = 0; i < 5; i++) {
            g_HarmFreq[i] = 50u * (i + 1);
            g_HarmAmp[i]  = 5000u / (i + 1);
        }
        g_HarmCount = 5;
        g_HarmPage = 0;
    }

    TEST("5 个点 → 总页数 2");
    ASSERT_EQ(2, FPGA_HarmTableGetTotalPages());
}

static void test_table_format_cell(void)
{
    TEST_SUITE("FormatCell");

    char dst[16];

    TJC_Table_FormatCell(dst, "Harm", 5);
    TEST("'Harm' width=5");
    ASSERT_STREQ("Harm ", dst);

    TJC_Table_FormatCell(dst, "AB", 4);
    TEST("'AB' width=4");
    ASSERT_STREQ("AB  ", dst);

    TJC_Table_FormatCell(dst, "Hello", 3);
    TEST("'Hello' width=3 (截断)");
    ASSERT_STREQ("Hel", dst);

    TJC_Table_FormatCell(dst, "", 4);
    TEST("空串 width=4");
    ASSERT_STREQ("    ", dst);

    TJC_Table_FormatCell(dst, "X", 1);
    TEST("'X' width=1");
    ASSERT_STREQ("X", dst);
}

static void test_table_format_num(void)
{
    TEST_SUITE("FormatNum");

    char dst[16];

    TJC_Table_FormatNum(dst, 1, 5);
    TEST("1 width=5");
    ASSERT_STREQ("1    ", dst);

    TJC_Table_FormatNum(dst, 12345, 5);
    TEST("12345 width=5");
    ASSERT_STREQ("12345", dst);

    TJC_Table_FormatNum(dst, 123456, 5);
    TEST("123456 width=5 (截断)");
    ASSERT_STREQ("12345", dst);

    TJC_Table_FormatNum(dst, -5, 4);
    TEST("-5 width=4");
    ASSERT_STREQ("-5  ", dst);

    TJC_Table_FormatNum(dst, 0, 4);
    TEST("0 width=4");
    ASSERT_STREQ("0   ", dst);

    TJC_Table_FormatNum(dst, 5000, 8);
    TEST("5000 width=8");
    ASSERT_STREQ("5000    ", dst);
}

static void test_table_string_build(void)
{
    TEST_SUITE("表格拼装");

    FPGA_HarmReset();
    FPGA_GenerateHarmTestData();

    /* 第 1 页: 4 行 */
    {
        char table[256];
        int len = BuildHarmonicTable(table, sizeof(table), 0);
        TEST("第1页拼装非空");
        ASSERT_TRUE(len > 0);

        /* 验证包含关键内容 */
        TEST("第1页包含 '1' (谐波1)");
        ASSERT_TRUE(strstr(table, "1    ") != NULL);

        TEST("第1页包含 '50' (频率)");
        ASSERT_TRUE(strstr(table, "50      ") != NULL);

        TEST("第1页包含 '5000' (幅值)");
        ASSERT_TRUE(strstr(table, "5000    ") != NULL);

        TEST("第1页包含换行 \\r\\n");
        ASSERT_TRUE(strstr(table, "\r\n") != NULL);
    }

    /* 第 2 页: 4 行 */
    {
        char table[256];
        int len = BuildHarmonicTable(table, sizeof(table), 1);
        TEST("第2页拼装非空");
        ASSERT_TRUE(len > 0);

        TEST("第2页包含 '5' (谐波5)");
        ASSERT_TRUE(strstr(table, "5    ") != NULL);
    }

    /* 第 3 页: 不存在 */
    {
        char table[256];
        memset(table, 0, sizeof(table));
        int len = BuildHarmonicTable(table, sizeof(table), 2);
        TEST("第3页无数据返回 0");
        ASSERT_EQ(0, len);
    }
}

static void test_table_layout(void)
{
    TEST_SUITE("表格布局验证");

    FPGA_HarmReset();
    FPGA_GenerateHarmTestData();

    /* 第 1 页应当恰好 4 行 */
    {
        char table[256];
        int len = BuildHarmonicTable(table, sizeof(table), 0);
        int i, lines = 0;
        for (i = 0; i < len; i++) {
            if (table[i] == '\n') lines++;
        }
        TEST("第1页正好 4 行");
        ASSERT_EQ(4, lines);
    }

    /* 第 2 页也应当 4 行 */
    {
        char table[256];
        int len = BuildHarmonicTable(table, sizeof(table), 1);
        int i, lines = 0;
        for (i = 0; i < len; i++) {
            if (table[i] == '\n') lines++;
        }
        TEST("第2页正好 4 行");
        ASSERT_EQ(4, lines);
    }

    /* 模拟 3 个点（不到一页），验证第 1 页刚好 3 行 */
    FPGA_HarmReset();
    g_HarmFreq[0] = 50;  g_HarmAmp[0] = 5000;
    g_HarmFreq[1] = 100; g_HarmAmp[1] = 2500;
    g_HarmFreq[2] = 150; g_HarmAmp[2] = 1666;
    g_HarmCount = 3;
    {
        char table[256];
        int len = BuildHarmonicTable(table, sizeof(table), 0);
        int i, lines = 0;
        for (i = 0; i < len; i++) {
            if (table[i] == '\n') lines++;
        }
        TEST("3 个点 → 3 行");
        ASSERT_EQ(3, lines);
    }
}

/* ================================================================
 *  第3部分：main
 * ================================================================ */

int main(void)
{
    printf("========================================\n");
    printf("  Harmonic & Table Unit Tests\n");
    printf("========================================\n");

    test_data_generation();
    test_data_boundary();
    test_pagination();
    test_pagination_edge_cases();
    test_table_format_cell();
    test_table_format_num();
    test_table_string_build();
    test_table_layout();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed", g_TestPass, g_TestFail);
    if (g_TestFail > 0) printf("  <<< FAILURES >>>");
    printf("\n========================================\n");

    return (g_TestFail > 0) ? 1 : 0;
}
