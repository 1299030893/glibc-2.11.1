/* 
 * CVE-2015-8779 测试程序
 * 用于验证 catopen 栈溢出漏洞是否已修复
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nl_types.h>

int main(int argc, char *argv[])
{
    printf("=== CVE-2015-8779 漏洞测试 ===\n\n");
    
    /* 测试 1: 正常路径 */
    printf("测试 1: 正常路径长度...\n");
    setenv("NLSPATH", "/usr/share/locale/%L/%N:/usr/share/locale/%L/LC_MESSAGES/%N", 1);
    nl_catd catd1 = catopen("test", NL_CAT_LOCALE);
    if (catd1 == (nl_catd) -1) {
        printf("  结果: catopen 返回错误 (预期行为)\n");
    } else {
        printf("  结果: catopen 成功\n");
        catclose(catd1);
    }
    printf("\n");
    
    /* 测试 2: 中等长度路径 */
    printf("测试 2: 中等长度路径 (1024 字节)...\n");
    char medium_path[1100];
    memset(medium_path, 'A', 1024);
    medium_path[1024] = '\0';
    
    setenv("NLSPATH", medium_path, 1);
    nl_catd catd2 = catopen("test", NL_CAT_LOCALE);
    if (catd2 == (nl_catd) -1) {
        printf("  结果: catopen 安全地返回错误 ✓\n");
    } else {
        printf("  结果: catopen 成功\n");
        catclose(catd2);
    }
    printf("\n");
    
    /* 测试 3: 超长路径（触发漏洞）*/
    printf("测试 3: 超长路径 (8192 字节 - 触发漏洞)...\n");
    printf("  如果程序崩溃，说明漏洞存在\n");
    printf("  如果程序正常返回错误，说明已修复\n");
    
    char *long_path = (char *)malloc(8192);
    if (long_path == NULL) {
        printf("  错误: 无法分配内存\n");
        return 1;
    }
    
    memset(long_path, 'A', 8191);
    long_path[8191] = '\0';
    
    /* 在路径中添加一些分隔符使其看起来像真实路径 */
    for (int i = 0; i < 8000; i += 100) {
        long_path[i] = '/';
    }
    
    setenv("NLSPATH", long_path, 1);
    
    printf("  正在调用 catopen...\n");
    nl_catd catd3 = catopen("test", NL_CAT_LOCALE);
    
    if (catd3 == (nl_catd) -1) {
        printf("  结果: catopen 安全地返回错误 ✓\n");
        printf("  ✓✓✓ 漏洞已修复! ✓✓✓\n");
    } else {
        printf("  结果: catopen 意外成功\n");
        catclose(catd3);
    }
    
    free(long_path);
    printf("\n");
    
    /* 测试 4: 极端情况 - 多个超长组件 */
    printf("测试 4: 多个超长路径组件...\n");
    char complex_path[5000];
    int offset = 0;
    
    /* 创建多个路径组件，用冒号分隔 */
    for (int i = 0; i < 5 && offset < 4900; i++) {
        int segment_len = 900;
        if (i > 0) {
            complex_path[offset++] = ':';
        }
        memset(complex_path + offset, 'B', segment_len);
        offset += segment_len;
    }
    complex_path[offset] = '\0';
    
    setenv("NLSPATH", complex_path, 1);
    nl_catd catd4 = catopen("test", NL_CAT_LOCALE);
    
    if (catd4 == (nl_catd) -1) {
        printf("  结果: catopen 安全地返回错误 ✓\n");
    } else {
        printf("  结果: catopen 成功\n");
        catclose(catd4);
    }
    printf("\n");
    
    printf("=== 测试完成 ===\n");
    printf("\n如果所有测试都正常完成而没有崩溃，说明 CVE-2015-8779 已修复。\n");
    
    return 0;
}
