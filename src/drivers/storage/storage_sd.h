#ifndef STORAGE_SD_H
#define STORAGE_SD_H

#include <stdbool.h>
#include "ff.h"

// マウント成功時 FR_OK(0)、失敗時は FatFS エラーコードを返す
int  sd_mount(void);
void sd_unmount(void);
bool sd_is_mounted(void);

// ルートディレクトリのファイル一覧を buf に改行区切りで書き込む
bool sd_list_root(char *buf, size_t buf_size);

#endif // STORAGE_SD_H
