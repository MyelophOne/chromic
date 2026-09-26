#pragma once

#include <optional>
#include <string>

class Config {
 public:
  Config();

  const std::wstring& command_line() const { return command_line_; }
  const std::wstring& launch_on_startup() const { return launch_on_startup_; }
  const std::wstring& launch_on_exit() const { return launch_on_exit_; }
  const std::optional<std::wstring>& data_dir() const { return data_dir_; }
  const std::optional<std::wstring>& cache_dir() const { return cache_dir_; }
  bool ignore_policies() const { return ignore_policies_; }
  bool keep_last_tab() const { return keep_last_tab_; }
  bool double_click_close() const { return double_click_close_; }

 private:
  static std::optional<std::wstring> LoadDirectory(std::wstring_view key,
                                                   std::wstring_view fallback);

  std::wstring command_line_;
  std::wstring launch_on_startup_;
  std::wstring launch_on_exit_;
  std::optional<std::wstring> data_dir_;
  std::optional<std::wstring> cache_dir_;
  bool ignore_policies_;
  bool keep_last_tab_;
  bool double_click_close_;
};

const Config& GetConfig();
