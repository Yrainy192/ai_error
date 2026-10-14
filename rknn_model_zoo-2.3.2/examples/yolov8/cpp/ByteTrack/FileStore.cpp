#include "FileStore.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace filestore {

static void setErr(std::string *err, const std::string &msg) {
    if (err) {
        *err = msg;
    }
}

std::string cameraDateDir(const std::string &camera_id, const std::string &date) {
    return std::string(kVideoRoot) + "/" + camera_id + "/" + date;
}

std::string tempDir(const std::string &camera_id, const std::string &date,
                    const std::string &kind) {
    return cameraDateDir(camera_id, date) + "/" + kTempName + "/" + kind;
}

std::string readyDir(const std::string &camera_id, const std::string &date,
                     const std::string &kind) {
    return readyDirForKind(cameraDateDir(camera_id, date), kind);
}

std::string readyDirForKind(const std::string &camera_date_dir, const std::string &kind) {
    /*
     * 与 FTP 服务器目录结构保持一致：
     *
     *   record   -> <camera_date_dir>            主录像直接放日期目录，不带 record 子目录
     *   event    -> <camera_date_dir>/event
     *   AI_error -> <camera_date_dir>/AI_error
     */

    if (kind == "record") {
        return camera_date_dir;
    }

    return camera_date_dir + "/" + kind;
}

std::string readyDirOfTemp(const std::string &temp_path) {
    fs::path kind_dir = fs::path(temp_path).parent_path(); // .../temp/<kind>

    fs::path temp_root = kind_dir.parent_path(); // .../temp

    if (temp_root.filename().string() != kTempName) {

        /*
         * 不是 temp 下的文件，退化为原目录
         */

        return kind_dir.string();
    }

    fs::path date_dir = temp_root.parent_path(); // .../<date>

    return readyDirForKind(date_dir.string(), kind_dir.filename().string());
}

static std::string dateStringFromTime(time_t sec) {
    struct tm tm_time;

    localtime_r(&sec, &tm_time);

    char buf[16] = {0};

    strftime(buf, sizeof(buf), "%Y%m%d", &tm_time);

    return std::string(buf);
}

std::string dateString(uint64_t time_ms) {
    return dateStringFromTime(static_cast<time_t>(time_ms / 1000));
}

std::string dateStringNow() { return dateStringFromTime(time(nullptr)); }

std::string uniqueBaseName(const std::string &ready_dir, const std::string &base) {
    auto taken = [&ready_dir](const std::string &name) {
        std::error_code ec;

        return fs::exists(ready_dir + "/" + name + ".ts", ec) ||
               fs::exists(ready_dir + "/" + name + ".json", ec);
    };

    if (!taken(base)) {
        return base;
    }

    for (int i = 1; i < 1000; i++) {

        std::string name = base + "_" + std::to_string(i);

        if (!taken(name)) {
            return name;
        }
    }

    return base + "_pid" + std::to_string(static_cast<long>(::getpid()));
}

bool ensureDir(const std::string &dir, std::string *err) {
    if (dir.empty()) {
        return true;
    }

    std::error_code ec;

    fs::create_directories(dir, ec);

    if (fs::is_directory(dir, ec)) {
        return true;
    }

    setErr(err, "create_directories " + dir + " failed: " + ec.message());

    return false;
}

bool syncFile(const std::string &path, std::string *err) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);

    if (fd < 0) {

        setErr(err, "open " + path + ": " + std::strerror(errno));

        return false;
    }

    bool ok = (::fsync(fd) == 0);

    if (!ok) {
        setErr(err, "fsync " + path + ": " + std::strerror(errno));
    }

    ::close(fd);

    return ok;
}

bool syncDir(const std::string &dir, std::string *err) {
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    if (fd < 0) {

        setErr(err, "open dir " + dir + ": " + std::strerror(errno));

        return false;
    }

    bool ok = (::fsync(fd) == 0);

    if (!ok) {
        setErr(err, "fsync dir " + dir + ": " + std::strerror(errno));
    }

    ::close(fd);

    return ok;
}

bool removeQuiet(const std::string &path) {
    std::error_code ec;

    return fs::remove(path, ec);
}

bool publishFile(const std::string &src, const std::string &dst, std::string *err) {
    std::error_code ec;

    if (!fs::exists(src, ec)) {

        setErr(err, "src missing " + src);

        return false;
    }

    if (fs::file_size(src, ec) == 0 || ec) {

        setErr(err, "src empty or unreadable " + src);

        return false;
    }

    if (!ensureDir(parentDir(dst), err)) {
        return false;
    }

    /*
     * 1. 数据先落盘
     */

    if (!syncFile(src, err)) {
        return false;
    }

    /*
     * 2. 同分区：rename 原子发布
     */

    fs::rename(src, dst, ec);

    if (!ec) {

        syncDir(parentDir(dst));

        return true;
    }

    /*
     * 3. 跨分区回退：copy + fsync + 删除源
     */

    std::error_code copy_ec;

    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, copy_ec);

    if (copy_ec) {

        setErr(err, "rename(" + ec.message() + ") and copy(" + copy_ec.message() +
                        ") both failed: " + src + " -> " + dst);

        return false;
    }

    if (!syncFile(dst, err)) {

        removeQuiet(dst);

        return false;
    }

    syncDir(parentDir(dst));

    removeQuiet(src);

    return true;
}

std::string fileName(const std::string &path) { return fs::path(path).filename().string(); }

std::string parentDir(const std::string &path) { return fs::path(path).parent_path().string(); }

/*
 * ============ 残留文件恢复 ============
 */

static int64_t fileMtimeSec(const std::string &path) {
    struct stat st;

    if (::stat(path.c_str(), &st) != 0) {
        return -1;
    }

    return static_cast<int64_t>(st.st_mtime);
}

static bool isStale(const std::string &path, int64_t now, int64_t stale_seconds) {
    int64_t mtime = fileMtimeSec(path);

    if (mtime < 0) {
        return false;
    }

    return (now - mtime) >= stale_seconds;
}

/*
 * 恢复一个 kind 目录：
 *   1. 清理未写完的 .part
 *   2. TS + 同名 JSON 成对补发（JSON先发，保证外部看到TS时JSON已就绪）
 *   3. 无同名TS的孤儿JSON也补发
 *
 * kind=record 的主录像没有 JSON，不做配对检查
 */
static size_t recoverKindDir(const fs::path &temp_dir, const fs::path &ready_dir,
                             const std::string &kind, int64_t now, int64_t stale_seconds) {
    std::error_code ec;

    if (!fs::is_directory(temp_dir, ec)) {
        return 0;
    }

    std::vector<std::string> files;

    for (const auto &entry : fs::directory_iterator(temp_dir, ec)) {

        if (ec) {
            break;
        }

        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }

    size_t done = 0;

    /*
     * 1. 未写完的临时文件直接清理
     */

    for (const auto &f : files) {

        fs::path p(f);

        if (p.extension() != ".part" || !isStale(f, now, stale_seconds)) {
            continue;
        }

        if (removeQuiet(f)) {

            spdlog::warn("[Recover] clean incomplete file {}", f);

            done++;
        }
    }

    /*
     * 2. TS + 同名 JSON
     */

    for (const auto &f : files) {

        fs::path p(f);

        if (p.extension() != ".ts" || !isStale(f, now, stale_seconds)) {
            continue;
        }

        std::string base = uniqueBaseName(ready_dir.string(), p.stem().string());

        std::string json = (p.parent_path() / (p.stem().string() + ".json")).string();

        std::string ts_dst = (ready_dir / (base + ".ts")).string();

        std::string json_dst = (ready_dir / (base + ".json")).string();

        std::string err;

        if (fs::is_regular_file(json, ec)) {

            if (!publishFile(json, json_dst, &err)) {

                spdlog::error("[Recover] publish json failed {} ({})", json, err);

                continue;
            }

        } else if (kind != "record") {

            spdlog::warn("[Recover] {} has no json, recover ts only", f);
        }

        if (!publishFile(f, ts_dst, &err)) {

            spdlog::error("[Recover] publish ts failed {} ({})", f, err);

            continue;
        }

        spdlog::warn("[Recover] recovered {} ", ts_dst);

        done++;
    }

    /*
     * 3. 孤儿JSON
     */

    for (const auto &f : files) {

        fs::path p(f);

        if (p.extension() != ".json" || !isStale(f, now, stale_seconds)) {
            continue;
        }

        if (!fs::exists(f, ec)) {
            continue; // 已随TS发布
        }

        if (fs::exists((p.parent_path() / (p.stem().string() + ".ts")).string(), ec)) {
            continue; // 已在第2步处理
        }

        std::string base = uniqueBaseName(ready_dir.string(), p.stem().string());

        std::string err;

        if (publishFile(f, (ready_dir / (base + ".json")).string(), &err)) {

            spdlog::warn("[Recover] recovered orphan json {}", f);

            done++;
        }
    }

    return done;
}

size_t recoverTempFiles(const std::string &camera_id, int64_t stale_seconds) {
    std::error_code ec;

    std::string cam_root = std::string(kVideoRoot) + "/" + camera_id;

    if (!fs::is_directory(cam_root, ec)) {
        return 0;
    }

    int64_t now = static_cast<int64_t>(time(nullptr));

    size_t done = 0;

    for (const auto &date_entry : fs::directory_iterator(cam_root, ec)) {

        if (ec) {
            break;
        }

        if (!date_entry.is_directory()) {
            continue;
        }

        fs::path temp_root = date_entry.path() / kTempName;

        if (!fs::is_directory(temp_root, ec)) {
            continue;
        }

        for (const auto &kind_entry : fs::directory_iterator(temp_root, ec)) {

            if (ec) {
                break;
            }

            if (!kind_entry.is_directory()) {
                continue;
            }

            std::string kind = kind_entry.path().filename().string();

            std::string ready = readyDirForKind(date_entry.path().string(), kind);

            done += recoverKindDir(kind_entry.path(), ready, kind, now, stale_seconds);
        }
    }

    if (done > 0) {
        spdlog::warn("[Recover] {} file(s) handled for {}", done, camera_id);
    }

    return done;
}

} // namespace filestore
