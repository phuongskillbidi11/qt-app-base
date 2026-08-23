#include "platform.h"

#include <QStringList>

namespace Platform {

namespace {
// This base's own defaults for a resource-constrained embedded target -- see spec.md D3 of
// .plans/2026-08-22-resource-check. A consuming application with heavier needs should
// compare SystemResources against its own thresholds instead of calling isResourceLow().
constexpr qint64 kLowRamBytes = 64LL * 1024 * 1024;
constexpr qint64 kLowDiskBytes = 20LL * 1024 * 1024;
}  // namespace

bool parseMemInfo(const QString &procMeminfoContent, qint64 *availableBytesOut,
                  qint64 *totalBytesOut) {
    qint64 availableKb = -1;
    qint64 freeKb = -1;
    qint64 totalKb = -1;
    const QStringList lines = procMeminfoContent.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("MemAvailable:"))) {
            availableKb = line.mid(13).trimmed().split(QLatin1Char(' ')).first().toLongLong();
        } else if (line.startsWith(QLatin1String("MemFree:"))) {
            freeKb = line.mid(8).trimmed().split(QLatin1Char(' ')).first().toLongLong();
        } else if (line.startsWith(QLatin1String("MemTotal:"))) {
            totalKb = line.mid(9).trimmed().split(QLatin1Char(' ')).first().toLongLong();
        }
    }
    if (totalKb < 0 || (availableKb < 0 && freeKb < 0)) {
        return false;
    }
    if (availableBytesOut) {
        *availableBytesOut = (availableKb >= 0 ? availableKb : freeKb) * 1024;
    }
    if (totalBytesOut) {
        *totalBytesOut = totalKb * 1024;
    }
    return true;
}

bool isResourceLow(const SystemResources &resources) {
    if (!resources.valid) {
        return false;   // nothing to warn about if the query itself failed
    }
    return resources.availableRamBytes < kLowRamBytes
        || resources.availableDiskBytes < kLowDiskBytes;
}

QString parseCpuInfo(const QString &procCpuinfoContent) {
    QString hardware;
    QString model;
    QString modelName;
    QString implementer;
    QString part;
    const QStringList lines = procCpuinfoContent.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            continue;
        }
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("Hardware") && hardware.isEmpty()) {
            hardware = value;
        } else if (key == QLatin1String("Model") && model.isEmpty()) {
            model = value;
        } else if (key == QLatin1String("model name") && modelName.isEmpty()) {
            modelName = value;
        } else if (key == QLatin1String("CPU implementer") && implementer.isEmpty()) {
            implementer = value;
        } else if (key == QLatin1String("CPU part") && part.isEmpty()) {
            part = value;
        }
    }
    if (!hardware.isEmpty()) {
        return hardware;
    }
    if (!model.isEmpty()) {
        return model;
    }
    if (!modelName.isEmpty()) {
        return modelName;
    }
    if (!implementer.isEmpty() || !part.isEmpty()) {
        return QString("unknown (implementer %1, part %2)")
            .arg(implementer.isEmpty() ? QStringLiteral("?") : implementer,
                 part.isEmpty() ? QStringLiteral("?") : part);
    }
    return QStringLiteral("unknown");
}

}  // namespace Platform
