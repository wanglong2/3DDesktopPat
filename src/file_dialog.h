#ifndef CROSE_FILE_DIALOG_H
#define CROSE_FILE_DIALOG_H

#include <string>
#include <vector>

namespace crose
{
    // 系统文件对话框。单独成 TU，避免 portable-file-dialogs 引入的 Windows 头
    // 与 raylib 的 Rectangle / CloseWindow / ShowCursor 等名称冲突。
    // filters 形如 {"配置文件", "*.json", "所有文件", "*"}。
    std::vector<std::string> openFileDialog(const std::string &title, const std::string &initialDirectory,
                                            const std::vector<std::string> &filters);

    // 返回空字符串表示用户取消。
    std::string saveFileDialog(const std::string &title, const std::string &defaultName,
                               const std::vector<std::string> &filters);
}

#endif
