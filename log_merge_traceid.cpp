#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <vector>

using namespace std;

// merge多个日志文件，输出到某目录下，以trace-id命名
// 同时输出一个traceid_log_start_end.txt 文件，包含HH:mm:ss.000000的时间信息
// 时间信息的差值由log_duration_compute.py脚本处理

typedef pair<string, string> LogItem;
map<string, vector<LogItem>> traces;

// 时间戳格式正则
const std::regex timestamp_regex("\\d+:\\d+:\\d+\\.\\d+");

bool isValidTimestamp(const std::string &s) {
  return std::regex_match(s, timestamp_regex);
}

std::chrono::time_point<std::chrono::system_clock>
parseTimestamp(const std::string &str) {

  std::istringstream iss(str);

  int h;
  iss >> h;
  std::chrono::hours hour(h);

  iss.ignore(1); // 忽略分隔符':'

  int m;
  iss >> m;
  std::chrono::minutes min(m);

  iss.ignore(1); // 忽略分隔符':'

  // 解析秒
  int s;
  iss >> s;
  std::chrono::seconds sec(s);

  iss.ignore(1); // 忽略点号

  // 解析微秒
  int ms;
  iss >> ms;
  std::chrono::microseconds msec(ms);

  return std::chrono::time_point<std::chrono::system_clock>(hour + min + sec +
                                                            msec);
}

void extractLogs(string filename, string patternStr) {
  ifstream file(filename);
  // regex pattern("\\[Y[0-9A-Z\\-]*\\]");
  regex pattern(patternStr);

  string line;
  while (getline(file, line)) {
    smatch match;
    if (regex_search(line, match, pattern)) {
      string timestamp = line.substr(12, 15);
      string traceId = match[1].str();
      traces[traceId].push_back(make_pair(timestamp, line));
    }
  }

  file.close();
}

bool mkdir_if_not_exist(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st) == 0) {
    // 目录已存在
    return true;
  } else if (mkdir(path.c_str(), 0755) == 0) {
    // 目录创建成功
    return true;
  } else {
    // 目录创建失败
    return false;
  }
}

void writeTraces(const string &outputDir) {
  if (false == mkdir_if_not_exist(outputDir)) {
    return; // 创建失败
  }

  for (auto it = traces.begin(); it != traces.end(); ++it) {
    string traceId = it->first;
    auto &logs = it->second;

    sort(logs.begin(), logs.end());

    int last_index = logs.size() - 1;
    for (auto log = logs.rbegin(); log != logs.rend(); ++log) {
      if (isValidTimestamp(log->first)) {
        break;
      }
      last_index--;
    }

    string time_diff;

    if (last_index >= 0) {
      auto t_begin = parseTimestamp(logs[0].first);
      auto t_end = parseTimestamp(logs[last_index].first);
      // 计算差值(以微秒为单位)
      auto diff =
          std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_begin)
              .count();
      time_diff = std::to_string(diff);
    }

    string fileName = outputDir + "/" + time_diff + "_" +
                      std::to_string(logs.size()) + "_" + traceId + ".log";
    ofstream outFile(fileName);
    for (auto log : logs) {
      outFile << log.second << endl;
    }

    outFile.close();
  }
}

void outputStartEnd() {
  ofstream outFile("competition/log/log_start_end.txt");
  for (auto it = traces.begin(); it != traces.end(); ++it) {
    vector<LogItem> &logs = it->second;
    outFile << it->first << ", " << logs[0].first << ", "
            << logs[logs.size() - 1].first << endl;
  }
  outFile.close();
}

void extractByTraceId() {
  string patternStr = "\\[(Y[0-9A-Z\\-]*)\\]"; // trace-id
  extractLogs("competition/log/observer.log", patternStr);
  extractLogs("competition/log/rootservice.log", patternStr);
  extractLogs("competition/log/election.log", patternStr);

  writeTraces("competition/log/mergelog-traceid");
  return;
}

void extractByFuncName() {
  string patternStr = "\\[\\w+\\] (\\w+)"; // func-name
  extractLogs("competition/log/observer.log", patternStr);
  extractLogs("competition/log/rootservice.log", patternStr);
  extractLogs("competition/log/election.log", patternStr);

  writeTraces("competition/log/mergelog-funcname");
  return;
}

void extractByFileName() {
  string patternStr = "\\[\\w+\\] \\w+ \\(([a-zA-Z0-9_]+\\.cpp:\\d+)\\)";
  extractLogs("competition/log/observer.log", patternStr);
  extractLogs("competition/log/rootservice.log", patternStr);
  extractLogs("competition/log/election.log", patternStr);

  writeTraces("competition/log/mergelog-filename");
  return;
}

std::string filter_filename(const std::string& filename) {
  std::string result;

  for (char c : filename) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || 
        c == '?' || c == '"' || c == '<' || c == '>' ||
        c == '|') {
      continue;
    }
    result += c; 
  }

  return result;
}

void extractByPattern(const string &patternStr) {
  extractLogs("competition/log/observer.log", patternStr);
  extractLogs("competition/log/rootservice.log", patternStr);
  extractLogs("competition/log/election.log", patternStr);

  string filenameStr = filter_filename(patternStr);
  writeTraces("competition/log/mergelog-" + filenameStr);
  return;
}

int main() {
  // string patternStr = "\\[(Y[0-9A-Z\\-]*)\\]"; // trace-id
  // string patternStr = "\\[\\w+\\] (\\w+)"; // func-name
  // string patternStr = "\\[\\w+\\] \\w+ \\(([a-zA-Z0-9_]+\\.cpp:\\d+)\\)"; //filename
  
  // extractByTraceId();
  // extractByFileName();
  // extractByFuncName();
  extractByPattern("(PRINT TIME HERE)"); 
  // extractByPattern("()");  // pattern 加个括号就行
  outputStartEnd();
  return 0;
}