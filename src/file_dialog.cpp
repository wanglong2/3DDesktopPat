#include "file_dialog.h"

#include "portable-file-dialogs.h"

namespace crose
{
    std::vector<std::string> openFileDialog(const std::string &title, const std::string &initialDirectory,
                                            const std::vector<std::string> &filters)
    {
        return pfd::open_file(title, initialDirectory, filters).result();
    }

    std::string saveFileDialog(const std::string &title, const std::string &defaultName,
                               const std::vector<std::string> &filters)
    {
        return pfd::save_file(title, defaultName, filters).result();
    }
}
