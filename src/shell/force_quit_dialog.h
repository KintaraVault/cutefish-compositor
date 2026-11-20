// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>
#include <vector>

#include "base/timer.h"

namespace cutefish::shell {

class Shell;
class ShellWindow;

class ForceQuitDialog
{
public:
    explicit ForceQuitDialog(Shell *shell);
    ~ForceQuitDialog();
    void show();
    void close();
    ShellWindow *window() const { return m_window.get(); }

private:
    struct Entry;
    class Content;
    std::vector<Entry> entries();
    void update();
    void request();
    void confirm();

    Shell *m_shell;
    std::unique_ptr<ShellWindow> m_window;
    Content *m_content = nullptr;
    base::Timer m_refresh;
};

} // namespace cutefish::shell
