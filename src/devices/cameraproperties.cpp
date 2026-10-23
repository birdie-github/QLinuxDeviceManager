#include "cameraproperties.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QVector>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
namespace {
constexpr int reportLimit = 1024 * 1024;
constexpr int modeLimit = 2048;
constexpr unsigned enumerationLimit = 128;
constexpr int queryLimit = 4096;
constexpr int budgetMs = 2000;
struct Descriptor {
    int fd;
    explicit Descriptor(int value) : fd(value)
    {
    }
    ~Descriptor()
    {
        if (fd >= 0)
            close(fd);
    }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
};

Attribute failure(int error)
{
    ReadState state = ReadState::Error;
    if (error == EACCES || error == EPERM)
        state = ReadState::PermissionDenied;
    else if (error == ENODEV || error == ENOENT || error == ENXIO)
        state = ReadState::Removed;
    else if (error == ENOTTY || error == EOPNOTSUPP || error == EINVAL)
        state = ReadState::Unsupported;
    return {state, {}, error};
}

QJsonObject failed(int error)
{
    return {{"error", error}};
}

bool isValidDriverText(const __u8 *value, size_t size)
{
    const char *data = reinterpret_cast<const char *>(value);
    const size_t length = strnlen(data, size);
    if (length == size)
        return false;
    const QByteArray bytes(data, static_cast<int>(length));
    return QString::fromUtf8(bytes).toUtf8() == bytes;
}

QString driverText(const __u8 *value, size_t size)
{
    const char *data = reinterpret_cast<const char *>(value);
    const size_t length = strnlen(data, size);
    return length == size ? QString() : QString::fromUtf8(data, static_cast<int>(length));
}

QJsonArray encodeFraction(const v4l2_fract &value)
{
    return {static_cast<qint64>(value.numerator), static_cast<qint64>(value.denominator)};
}

bool isValidFraction(const v4l2_fract &value)
{
    return value.numerator && value.denominator;
}

bool fractionLessOrEqual(const v4l2_fract &a, const v4l2_fract &b)
{
    return static_cast<quint64>(a.numerator) * b.denominator <=
           static_cast<quint64>(b.numerator) * a.denominator;
}

class V4l2Query {
public:
    explicit V4l2Query(int fd) : fd_(fd)
    {
        timer_.start();
    }
    template <typename T> bool readIoctl(unsigned long request, T &value)
    {
        if (++calls_ > queryLimit) {
            error = EOVERFLOW;
            return false;
        }
        for (;;) {
            if (timer_.elapsed() >= budgetMs) {
                error = ETIMEDOUT;
                return false;
            }
            // Unregistered fd: documented libv4l2 passthrough, no converter setup,
            // emulated formats, software controls or capture initialization.
            if (v4l2_ioctl(fd_, request, &value) == 0) {
                error = 0;
                return true;
            }
            error = errno;
            if (error != EINTR)
                return false;
        }
    }
    int error = 0;

private:
    int fd_;
    int calls_ = 0;
    QElapsedTimer timer_;
};

QJsonObject enumerateFrameIntervals(V4l2Query &query, __u32 format, __u32 width, __u32 height)
{
    QJsonArray discrete;
    for (unsigned index = 0; index <= enumerationLimit; ++index) {
        v4l2_frmivalenum value{};
        value.index = index;
        value.pixel_format = format;
        value.width = width;
        value.height = height;
        if (!query.readIoctl(VIDIOC_ENUM_FRAMEINTERVALS, value)) {
            if (query.error == EINVAL && index > 0)
                return {{"kind", "discrete"}, {"values", discrete}};
            return failed(query.error);
        }
        if (value.index != index || value.pixel_format != format || value.width != width ||
            value.height != height)
            return failed(EPROTO);
        if (index == enumerationLimit)
            return failed(EOVERFLOW);
        if (value.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            if (!isValidFraction(value.discrete))
                return failed(EPROTO);
            discrete.append(encodeFraction(value.discrete));
        } else if (index == 0 &&
                   (value.type == V4L2_FRMIVAL_TYPE_STEPWISE || value.type == V4L2_FRMIVAL_TYPE_CONTINUOUS)) {
            const auto &range = value.stepwise;
            if (!isValidFraction(range.min) || !isValidFraction(range.max) ||
                !fractionLessOrEqual(range.min, range.max) ||
                (value.type == V4L2_FRMIVAL_TYPE_STEPWISE && !isValidFraction(range.step)))
                return failed(EPROTO);
            return {{"kind", value.type == V4L2_FRMIVAL_TYPE_CONTINUOUS ? "continuous" : "stepwise"},
                    {"min", encodeFraction(range.min)},
                    {"max", encodeFraction(range.max)},
                    {"step", encodeFraction(range.step)}};
        } else
            return failed(EPROTO);
    }
    return failed(EOVERFLOW);
}

// EINVAL ends a nonempty enumeration. At index zero it remains a reported error.
QVector<v4l2_fmtdesc> enumerateFormats(V4l2Query &query, __u32 type, int &collectionError)
{
    QVector<v4l2_fmtdesc> formats;
    for (unsigned index = 0; index <= enumerationLimit; ++index) {
        v4l2_fmtdesc format{};
        format.type = type;
        format.index = index;
        if (!query.readIoctl(VIDIOC_ENUM_FMT, format)) {
            if (query.error != EINVAL || index == 0)
                collectionError = query.error;
            break;
        }
        if (!isValidDriverText(format.description, sizeof(format.description))) {
            collectionError = EPROTO;
            break;
        }
        if (format.type != type || format.index != index || !format.pixelformat) {
            collectionError = EPROTO;
            break;
        }
        if (index == enumerationLimit) {
            collectionError = EOVERFLOW;
            break;
        }
        formats.append(format);
    }
    return formats;
}

// A size-query failure belongs to this format; malformed replies affect the report.
QVector<QJsonObject> enumerateFrameSizes(V4l2Query &query, __u32 pixelFormat, const QJsonObject &base,
                                         int &collectionError)
{
    QVector<QJsonObject> sizes;
    for (unsigned index = 0; index <= enumerationLimit; ++index) {
        v4l2_frmsizeenum size{};
        size.index = index;
        size.pixel_format = pixelFormat;
        if (!query.readIoctl(VIDIOC_ENUM_FRAMESIZES, size)) {
            if (query.error != EINVAL || index == 0) {
                QJsonObject row = base;
                row["size_error"] = query.error;
                sizes.append(row);
            }
            break;
        }
        if (index == enumerationLimit) {
            collectionError = EOVERFLOW;
            break;
        }
        if (size.index != index || size.pixel_format != pixelFormat) {
            collectionError = EPROTO;
            break;
        }
        QJsonObject row = base;
        if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            if (!size.discrete.width || !size.discrete.height) {
                collectionError = EPROTO;
                break;
            }
            row["size_kind"] = "discrete";
            row["width"] = static_cast<qint64>(size.discrete.width);
            row["height"] = static_cast<qint64>(size.discrete.height);
        } else if (index == 0 &&
                   (size.type == V4L2_FRMSIZE_TYPE_STEPWISE || size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS)) {
            const auto &range = size.stepwise;
            if (!range.min_width || !range.min_height || range.min_width > range.max_width ||
                range.min_height > range.max_height || !range.step_width || !range.step_height ||
                (size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS &&
                 (range.step_width != 1 || range.step_height != 1))) {
                collectionError = EPROTO;
                break;
            }
            row["size_kind"] = size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS ? "continuous" : "stepwise";
            row["width"] = static_cast<qint64>(range.min_width);
            row["height"] = static_cast<qint64>(range.min_height);
            row["max_width"] = static_cast<qint64>(range.max_width);
            row["max_height"] = static_cast<qint64>(range.max_height);
            row["step_width"] = static_cast<qint64>(range.step_width);
            row["step_height"] = static_cast<qint64>(range.step_height);
        } else {
            collectionError = EPROTO;
            break;
        }
        sizes.append(row);
        if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE)
            break;
    }
    return sizes;
}

// Preserve partial modes and the original error precedence across queues and formats.
QJsonArray enumerateModes(V4l2Query &query, __u32 caps, int &collectionError)
{
    QJsonArray modes;
    for (__u32 type : {V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE}) {
        if (!(caps &
              (type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? V4L2_CAP_VIDEO_CAPTURE : V4L2_CAP_VIDEO_CAPTURE_MPLANE)))
            continue;
        const auto formats = enumerateFormats(query, type, collectionError);
        for (const auto &format : formats) {
            QJsonObject base{{"queue", type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? "single" : "multi"},
                             {"format", static_cast<qint64>(format.pixelformat)},
                             {"description", driverText(format.description, sizeof(format.description))},
                             {"flags", static_cast<qint64>(format.flags)}};
            const auto sizes = enumerateFrameSizes(query, format.pixelformat, base, collectionError);
            for (QJsonObject row : sizes) {
                // Ranges stay ranges: never imply one rate list applies to every size.
                if (row.value("size_kind") == "discrete")
                    row["intervals"] = enumerateFrameIntervals(
                        query, format.pixelformat, static_cast<__u32>(row.value("width").toInteger()),
                        static_cast<__u32>(row.value("height").toInteger()));
                if (modes.size() == modeLimit) {
                    collectionError = EOVERFLOW;
                    break;
                }
                modes.append(row);
            }
            if (query.error == ETIMEDOUT || query.error == EOVERFLOW)
                collectionError = query.error;
            if (modes.size() == modeLimit)
                collectionError = EOVERFLOW;
            if (query.error == ETIMEDOUT || query.error == EOVERFLOW || modes.size() == modeLimit)
                break;
        }
        if (query.error == ETIMEDOUT || query.error == EOVERFLOW || modes.size() == modeLimit)
            break;
    }
    return modes;
}

QJsonObject queryDevice(int directory, const QString &path, const QString &node)
{
    struct stat selected{};
    if (fstat(directory, &selected) != 0)
        return failed(errno);
    if (!S_ISDIR(selected.st_mode))
        return failed(EPROTO);
    Descriptor devFile(openat(directory, "dev", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (devFile.fd < 0)
        return failed(errno);
    char dev[65]{};
    const ssize_t length = read(devFile.fd, dev, 64);
    if (length <= 0 || length == 64)
        return failed(length < 0 ? errno : EPROTO);
    const QStringList numbers = QString::fromLatin1(dev, static_cast<int>(length)).trimmed().split(':');
    bool majorOk = false, minorOk = false;
    const uint deviceMajor = numbers.value(0).toUInt(&majorOk),
               deviceMinor = numbers.value(1).toUInt(&minorOk);
    if (numbers.size() != 2 || !majorOk || !minorOk)
        return failed(EPROTO);
    const QByteArray name = QFile::encodeName(node);
    Descriptor device(open(name.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    if (device.fd < 0)
        return failed(errno);
    struct stat opened{};
    if (fstat(device.fd, &opened) != 0)
        return failed(errno);
    if (!S_ISCHR(opened.st_mode) || major(opened.st_rdev) != deviceMajor ||
        minor(opened.st_rdev) != deviceMinor)
        return failed(ENODEV);
    const QString binding = QString("/sys/dev/char/%1:%2").arg(deviceMajor).arg(deviceMinor);
    const auto sameDevice = [&] {
        const QByteArray sysPath = QFile::encodeName(path);
        struct stat current{}, nodeNow{};
        return stat(sysPath.constData(), &current) == 0 && current.st_dev == selected.st_dev &&
               current.st_ino == selected.st_ino && lstat(name.constData(), &nodeNow) == 0 &&
               S_ISCHR(nodeNow.st_mode) && nodeNow.st_rdev == opened.st_rdev &&
               nodeNow.st_ino == opened.st_ino && nodeNow.st_dev == opened.st_dev &&
               QFileInfo(binding).canonicalFilePath() == path;
    };
    if (!sameDevice())
        return failed(ENODEV);
    V4l2Query query(device.fd);
    v4l2_capability capability{};
    if (!query.readIoctl(VIDIOC_QUERYCAP, capability))
        return failed(query.error);
    if (!isValidDriverText(capability.card, sizeof(capability.card)) ||
        !isValidDriverText(capability.driver, sizeof(capability.driver)) ||
        !isValidDriverText(capability.bus_info, sizeof(capability.bus_info)))
        return failed(EPROTO);
    const __u32 caps =
        capability.capabilities & V4L2_CAP_DEVICE_CAPS ? capability.device_caps : capability.capabilities;
    if (!(caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) ||
        (caps & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE)))
        return {{"applicable", false}};
    QJsonObject result{{"applicable", true},
                       {"card", driverText(capability.card, sizeof(capability.card))},
                       {"driver", driverText(capability.driver, sizeof(capability.driver))},
                       {"bus", driverText(capability.bus_info, sizeof(capability.bus_info))}};
    int collectionError = 0;
    const QJsonArray modes = enumerateModes(query, caps, collectionError);
    if (!sameDevice())
        return failed(ENODEV);
    result["modes"] = modes;
    result["error"] = collectionError;
    return result;
}

struct HelperReply {
    QByteArray bytes;
    int error = 0;
};

// Own process lifetime and bounded transport independently of the camera report schema.
HelperReply runCameraHelper(int directory, const QString &devicePath, const QString &node)
{
    // One properties worker serializes requests. Never accumulate stuck helpers.
    static pid_t pending = -1;
    if (pending > 0) {
        const pid_t reaped = waitpid(pending, nullptr, WNOHANG);
        if (reaped == 0 || (reaped < 0 && errno == EINTR)) {
            return {{}, EAGAIN};
        }
        pending = -1;
    }
    int pipes[2];
    if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) != 0) {
        return {{}, errno};
    }
    Descriptor input(pipes[0]), output(pipes[1]);
    Descriptor inherited(fcntl(directory, F_DUPFD_CLOEXEC, 64));
    if (inherited.fd < 0) {
        return {{}, errno};
    }
    // Only stdout is nonblocking in the parent; helper writes use a blocking pipe.
    if (fcntl(output.fd, F_SETFL, 0) < 0) {
        return {{}, errno};
    }
    const QByteArray executable = QFile::encodeName(QCoreApplication::applicationFilePath());
    const QByteArray path = QFile::encodeName(devicePath), dev = QFile::encodeName(node),
                     fd = QByteArray::number(inherited.fd);
    char *arguments[] = {const_cast<char *>(executable.constData()), const_cast<char *>("--camera-query"),
                         const_cast<char *>(path.constData()),       const_cast<char *>(dev.constData()),
                         const_cast<char *>(fd.constData()),         nullptr};
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        return {{}, error};
    }
    error = posix_spawn_file_actions_adddup2(&actions, inherited.fd, inherited.fd);
    if (!error)
        error = posix_spawn_file_actions_adddup2(&actions, output.fd, STDOUT_FILENO);
    if (!error && input.fd != STDOUT_FILENO)
        error = posix_spawn_file_actions_addclose(&actions, input.fd);
    if (!error && output.fd != STDOUT_FILENO)
        error = posix_spawn_file_actions_addclose(&actions, output.fd);
    pid_t child = -1;
    if (!error)
        error = posix_spawn(&child, executable.constData(), &actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error) {
        return {{}, error};
    }
    close(output.fd);
    output.fd = -1;
    QElapsedTimer timer;
    timer.start();
    QByteArray bytes;
    bool eof = false;
    int exitStatus = 0;
    bool exited = false;
    while (!eof || !exited) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            error = ECANCELED;
            break;
        }
        if (timer.elapsed() >= budgetMs) {
            error = ETIMEDOUT;
            break;
        }
        char buffer[4096];
        const ssize_t count = read(input.fd, buffer, sizeof(buffer));
        if (count > 0) {
            bytes.append(buffer, static_cast<int>(count));
            if (bytes.size() > reportLimit) {
                error = EOVERFLOW;
                break;
            }
        } else if (count == 0)
            eof = true;
        else if (errno != EAGAIN && errno != EINTR) {
            error = errno;
            break;
        }
        if (!exited) {
            const pid_t reaped = waitpid(child, &exitStatus, WNOHANG);
            if (reaped == child)
                exited = true;
            else if (reaped < 0 && errno != EINTR) {
                error = errno;
                break;
            }
        }
        if (!eof && count <= 0) {
            pollfd wait{input.fd, POLLIN, 0};
            poll(&wait, 1, 25);
        } else if (eof && !exited) {
            poll(nullptr, 0, 10);
        }
    }
    if (error) {
        if (!exited) {
            kill(child, SIGKILL);
            if (waitpid(child, nullptr, WNOHANG) != child)
                pending = child;
        }
        return {{}, error};
    }
    if (!WIFEXITED(exitStatus) || WEXITSTATUS(exitStatus) != 0) {
        return {{}, EPROTO};
    }
    return {bytes, 0};
}
} // namespace

int runCameraQueryHelper(int directory, const QString &path, const QString &node)
{
    // If the GUI dies, do not leave an ordinary helper running independently.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() == 1)
        return 1;
    QByteArray bytes = QJsonDocument(queryDevice(directory, path, node)).toJson(QJsonDocument::Compact);
    if (bytes.size() > reportLimit)
        bytes = QJsonDocument(failed(EOVERFLOW)).toJson(QJsonDocument::Compact);
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count =
            write(STDOUT_FILENO, bytes.constData() + offset, static_cast<size_t>(bytes.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return 1;
        offset += count;
    }
    return 0;
}

CameraProperties collectCameraProperties(int directory, const Device &device, const QString &node)
{
    CameraProperties result;
    result.applicable = true;
    result.node = node;
    if (node.isEmpty()) {
        result.state = {};
        return result;
    }
    if (!node.startsWith("/dev/") || node.size() > 4096 || device.path.size() > 4096 ||
        node.contains(QChar::Null)) {
        result.state = failure(EPROTO);
        return result;
    }
    const HelperReply reply = runCameraHelper(directory, device.path, node);
    if (reply.error) {
        result.state = failure(reply.error);
        return result;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(reply.bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        result.state = failure(EPROTO);
        return result;
    }
    result.report = document.object();
    if (result.report.contains("applicable"))
        result.applicable = result.report.value("applicable").toBool();
    const int reportError = result.report.value("error").toInt();
    result.state = reportError ? failure(reportError) : Attribute{ReadState::Available, {}, 0};
    return result;
}
