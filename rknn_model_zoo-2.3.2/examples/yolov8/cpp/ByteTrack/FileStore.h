#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * TS/JSON 文件落地工具
 *
 * 目录约定（camera= cam0..cam5，date= YYYYMMDD）
 * 正式目录与 FTP 服务器目录结构保持一致：
 *
 *   写入中（只在这里出现）：
 *       /home/admin/Videos/<camera>/<date>/temp/record/HHMMSS.ts
 *                                      temp/event/HHMMSSmmm.ts
 *                                      temp/AI_error/HHMMSSmmm.ts + .json
 *
 *   已完成（可直接上传）：
 *       /home/admin/Videos/<camera>/<date>/HHMMSS.ts          主录像（无 record 子目录）
 *                                  <date>/event/HHMMSSmmm.ts 事件片段
 *                                  <date>/AI_error/...ts     AI误报片段 + 同名 .json
 *
 * kind = record（主录像） / event（事件片段） / AI_error（AI误报片段）
 *
 * 保证：
 *   1. temp 与 ready 目录同一分区，发布使用 rename（原子），
 *      外部扫描正式目录时不会看到正在写入的文件；
 *   2. 发布前先 fsync 数据，避免断电后拿到空文件；
 *   3. 同名文件已存在于正式目录时自动改名，不覆盖已完成文件。
 */
namespace filestore {

// 录像根目录
inline constexpr const char *kVideoRoot = "/home/admin/Videos";

// 未完成文件目录名
inline constexpr const char *kTempName = "temp";

// <root>/<camera>/<date>
std::string cameraDateDir(const std::string &camera_id, const std::string &date);

// <root>/<camera>/<date>/temp/<kind>
std::string tempDir(const std::string &camera_id, const std::string &date, const std::string &kind);

// <root>/<camera>/<date>/<kind>（kind=record 时为 <root>/<camera>/<date>）
std::string readyDir(const std::string &camera_id, const std::string &date,
                     const std::string &kind);

/**
 * kind 对应的正式目录（与 FTP 服务器目录结构一致）
 *
 * 例：
 *   readyDirForKind("/home/admin/Videos/cam1/20260917", "record")
 *     -> /home/admin/Videos/cam1/20260917
 *   readyDirForKind("/home/admin/Videos/cam1/20260917", "AI_error")
 *     -> /home/admin/Videos/cam1/20260917/AI_error
 */
std::string readyDirForKind(const std::string &camera_date_dir, const std::string &kind);

/**
 * 由 temp 下的临时文件路径推出它的正式目录
 *
 * 例：
 *   .../<date>/temp/record/123456.ts   -> .../<date>
 *   .../<date>/temp/AI_error/123.json  -> .../<date>/AI_error
 *
 * 不是 temp 下的路径时，退化为该文件所在目录。
 */
std::string readyDirOfTemp(const std::string &temp_path);

// Unix ms -> YYYYMMDD
std::string dateString(uint64_t time_ms);

// 当前时间 -> YYYYMMDD
std::string dateStringNow();

/**
 * 生成不会覆盖正式目录中已完成文件的文件名（不含扩展名）
 *
 * 若 <ready_dir>/<base>.ts 或 .json 已存在，则返回 base_1 / base_2 ...
 */
std::string uniqueBaseName(const std::string &ready_dir, const std::string &base);

// 递归创建目录
bool ensureDir(const std::string &dir, std::string *err = nullptr);

// fsync 文件内容
bool syncFile(const std::string &path, std::string *err = nullptr);

// fsync 目录项（保证 rename 结果持久化）
bool syncDir(const std::string &dir, std::string *err = nullptr);

// 删除文件，失败只记录不抛出
bool removeQuiet(const std::string &path);

/**
 * 原子发布
 *
 * src 必须已经写完整（调用方负责关闭文件句柄）。
 * 同一分区用 rename；跨分区退化为 copy + fsync + 删除源文件。
 */
bool publishFile(const std::string &src, const std::string &dst, std::string *err = nullptr);

// 路径最后一段（文件名）
std::string fileName(const std::string &path);

// 路径所在目录
std::string parentDir(const std::string &path);

/**
 * 恢复上次异常退出残留在 temp 下的文件
 *
 * 扫描 <root>/<camera>/<date>/temp/<kind>/：
 *   - mtime 早于 stale_seconds 的 TS/JSON：补发到正式目录（JSON先发，TS后发）
 *   - mtime 早于 stale_seconds 的 *.part：未写完，直接清理
 *
 * stale_seconds 用于避开仍可能被写入的文件（正常运行时不会命中）。
 * 返回处理（补发+清理）的文件数量。
 */
size_t recoverTempFiles(const std::string &camera_id, int64_t stale_seconds);

} // namespace filestore
