// 文件扩展名过滤和目录排除
#pragma once

#include <string>
#include <unordered_set>

class ScanFilter {
public:
    // 设置扩展名过滤：分号分隔，如 L".cpp;.h;.txt"，传 nullptr 或空串清空过滤
    void SetExtensions(const wchar_t* extensions);

    // 设置排除目录：分号分隔，如 L".git;node_modules;bin"，传 nullptr 或空串清空排除列表
    void SetExcludeDirs(const wchar_t* dirs);

    // 文件名是否通过扩展名过滤（无过滤时全部通过）
    bool MatchFile(const std::wstring& fileName) const;

    // 目录名是否在排除列表中
    bool ExcludeDir(const std::wstring& dirName) const;

    // 清空所有过滤规则
    void Clear();

    // 当前过滤规则数量
    size_t ExtCount() const;
    size_t ExcludeCount() const;

private:
    // 解析分号分隔字符串到 set，自动将 "cpp" / "*.cpp" 规范化为 ".cpp"
    static void Parse(const wchar_t* str, std::unordered_set<std::wstring>& out);

    std::unordered_set<std::wstring> m_extensions;
    std::unordered_set<std::wstring> m_excludeDirs;
};
