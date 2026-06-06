// 文件过滤实现：扩展名匹配 + 目录排除
#include "Filter.h"

// 设置扩展名过滤规则
// 参数 extensions: 分号分隔的扩展名列表，如 L".cpp;.h;.txt"
//                  传 nullptr 或空串清空过滤规则
void ScanFilter::SetExtensions(const wchar_t* extensions) {
    m_extensions.clear();
    Parse(extensions, m_extensions);
}

// 设置排除目录规则
// 参数 dirs: 分号分隔的目录名列表，如 L".git;node_modules;bin"
//            传 nullptr 或空串清空排除列表
void ScanFilter::SetExcludeDirs(const wchar_t* dirs) {
    m_excludeDirs.clear();
    Parse(dirs, m_excludeDirs);
}

// 判断文件是否通过扩展名过滤
// 参数 fileName: 文件名（含扩展名）
// 返回: true 表示通过过滤，false 表示被过滤掉
//       如果没有设置扩展名过滤，则所有文件都通过
bool ScanFilter::MatchFile(const std::wstring& fileName) const {
    // 没有设置扩展名过滤，全部通过
    if (m_extensions.empty()) return true;
    
    // 查找最后一个点（扩展名分隔符）
    size_t dot = fileName.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    
    // 提取扩展名（含点）
    std::wstring ext = fileName.substr(dot);
    
    // 检查扩展名是否在允许列表中
    return m_extensions.count(ext) > 0;
}

// 判断目录是否在排除列表中
// 参数 dirName: 目录名
// 返回: true 表示需要排除，false 表示不排除
bool ScanFilter::ExcludeDir(const std::wstring& dirName) const {
    return m_excludeDirs.count(dirName) > 0;
}

// 清空所有过滤规则
void ScanFilter::Clear() {
    m_extensions.clear();
    m_excludeDirs.clear();
}

// 获取当前扩展名过滤规则数量
size_t ScanFilter::ExtCount() const { 
    return m_extensions.size(); 
}

// 获取当前排除目录规则数量
size_t ScanFilter::ExcludeCount() const { 
    return m_excludeDirs.size(); 
}

// 解析分号分隔的字符串到 set
// 参数 str: 输入字符串，如 L".cpp;.h;*.txt"
//       out: 输出集合，存储解析后的规则
// 规范化处理：
//   - 自动去除前导的 '*' 字符
//   - 自动为没有前导点的扩展名添加点（如 "cpp" -> ".cpp"）
//   - 忽略空字符串
void ScanFilter::Parse(const wchar_t* str, std::unordered_set<std::wstring>& out) {
    // 空指针或空字符串，直接返回
    if (!str || !*str) return;
    
    std::wstring s(str);
    size_t pos = 0;
    
    // 逐个解析分号分隔的部分
    while (pos < s.length()) {
        // 查找下一个分号
        size_t next = s.find(L';', pos);
        
        // 提取当前部分
        std::wstring part = (next == std::wstring::npos) ? s.substr(pos) : s.substr(pos, next - pos);
        
        // 去除前导的 '*' 字符（支持 *.cpp 格式）
        while (!part.empty() && part[0] == L'*') {
            part = part.substr(1);
        }
        
        // 如果没有前导点，自动添加（支持 cpp 格式）
        if (!part.empty() && part[0] != L'.') {
            part = L"." + part;
        }
        
        // 添加到集合（非空字符串）
        if (!part.empty()) {
            out.insert(part);
        }
        
        // 如果没有更多分号，退出循环
        if (next == std::wstring::npos) break;
        
        // 移动到下一个位置
        pos = next + 1;
    }
}