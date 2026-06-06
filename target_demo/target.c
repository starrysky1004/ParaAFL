#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    char buf[256];
    FILE* f;
    size_t len;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    len = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    /* 简单的目标程序：如果输入以特定字符串开头，触发不同路径 */
    if (len > 0) {
        /* 路径分支，增加覆盖率 */
        if (buf[0] == 'A') {
            if (len > 1 && buf[1] == 'B') {
                if (len > 2 && buf[2] == 'C') {
                    /* 潜在崩溃路径 */
                    if (len > 3 && buf[3] == '!') {
                        /* 触发SIGSEGV用于测试 */
                        char* p = NULL;
                        *p = 'x'; /* 故意触发崩溃 */
                    }
                }
            }
        }
        /* 增加更多分支 */
        if (buf[0] == 'X') {
            if (len > 1 && buf[1] == 'Y') {
                if (len > 2 && buf[2] == 'Z') {
                    /* 另一个分支 */
                    return 42;
                }
            }
        }
    }

    return 0;
}
