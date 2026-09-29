#include "KdImageParser.h"

#include <public/canaan-burn.h>

#include "common/BurnLibrary.h"

#include <QByteArray>

#include <cstring>
#include <limits>

namespace {

QString fixedString(const char *data, qsizetype size)
{
    const void *terminator = std::memchr(data, '\0', static_cast<size_t>(size));
    const qsizetype length = terminator
        ? static_cast<const char *>(terminator) - data
        : size;
    return QString::fromUtf8(data, length);
}

void logKdImgPart(const kd_img_part_t &part)
{
    const QString logMessage = QStringLiteral(
        "kd_img_part_t:\n"
        "  part_magic: 0x%1\n"
        "  part_offset: %2\n"
        "  part_size: %3\n"
        "  part_erase_size: %4\n"
        "  part_max_size: %5\n"
        "  part_flag: 0x%6\n"
        "  part_content_offset: %7\n"
        "  part_content_size: %8\n"
        "  part_content_sha256: %9\n"
        "  part_name: %10")
        .arg(QString::number(part.part_magic, 16).toUpper())
        .arg(part.part_offset)
        .arg(part.part_size)
        .arg(part.part_erase_size)
        .arg(part.part_max_size)
        .arg(QString::number(part.part_flag, 16).toUpper())
        .arg(part.part_content_offset)
        .arg(part.part_content_size)
        .arg(QByteArray(reinterpret_cast<const char *>(part.part_content_sha256),
                        sizeof(part.part_content_sha256)).toHex())
        .arg(fixedString(part.part_name, sizeof(part.part_name)));

    BurnLibrary::instance()->localLog(logMessage);
}

} // namespace

int parseKdImage(QFile &imageFile, kd_img_hdr_t &hdr,
                 QList<kd_img_part_t> &parts)
{
    parts.clear();

    if (!imageFile.isOpen()) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 imageFile not open").arg(__func__));
        return -1;
    }

    const qint64 imageSize = imageFile.size();
    if (imageSize < static_cast<qint64>(sizeof(kd_img_hdr_t)) ||
        !imageFile.seek(0)) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 invalid image size or seek failed").arg(__func__));
        return -1;
    }

    const QByteArray headerData = imageFile.read(sizeof(kd_img_hdr_t));
    if (headerData.size() != static_cast<qsizetype>(sizeof(kd_img_hdr_t))) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 failed to read full image header").arg(__func__));
        return -1;
    }

    hdr = {};
    std::memcpy(&hdr, headerData.constData(), sizeof(hdr));
    if (hdr.img_hdr_magic != KDIMG_HADER_MAGIC) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 invalid image header magic, %2 != %3")
                .arg(__func__).arg(KDIMG_HADER_MAGIC).arg(hdr.img_hdr_magic));
        return -1;
    }

    kd_img_hdr_t headerForCrc = hdr;
    const uint32_t expectedHeaderCrc = headerForCrc.img_hdr_crc32;
    headerForCrc.img_hdr_crc32 = 0;
    const uint32_t actualHeaderCrc = crc32(
        0, reinterpret_cast<const unsigned char *>(&headerForCrc),
        sizeof(headerForCrc));
    if (actualHeaderCrc != expectedHeaderCrc) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 invalid image header checksum, %2 != %3")
                .arg(__func__).arg(actualHeaderCrc).arg(expectedHeaderCrc));
        return -1;
    }

    if (!hdr.part_tbl_num) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 image contains no partitions").arg(__func__));
        return -1;
    }

    const quint64 tableSize = static_cast<quint64>(hdr.part_tbl_num) *
                              sizeof(kd_img_part_t);
    if (tableSize > std::numeric_limits<uint32_t>::max() ||
        tableSize > static_cast<quint64>(std::numeric_limits<qsizetype>::max()) ||
        tableSize > static_cast<quint64>(imageSize) - sizeof(kd_img_hdr_t)) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 invalid part table size").arg(__func__));
        return -1;
    }

    const QByteArray partTable = imageFile.read(static_cast<qint64>(tableSize));
    if (partTable.size() != static_cast<qsizetype>(tableSize)) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 failed to read full image part table").arg(__func__));
        return -1;
    }

    const uint32_t tableCrc = crc32(
        0, reinterpret_cast<const unsigned char *>(partTable.constData()),
        static_cast<uint32_t>(partTable.size()));
    if (tableCrc != hdr.part_tbl_crc32) {
        BurnLibrary::instance()->localLog(
            QStringLiteral("%1 invalid part table checksum, %2 != %3")
                .arg(__func__).arg(tableCrc).arg(hdr.part_tbl_crc32));
        return -1;
    }

    BurnLibrary::instance()->localLog(
        QStringLiteral("hdr.img_hdr_version %1").arg(hdr.img_hdr_version));

    for (quint64 offset = 0; offset < tableSize; offset += sizeof(kd_img_part_t)) {
        kd_img_part_t part{};
        const char *partData = partTable.constData() + static_cast<qsizetype>(offset);

        if (hdr.img_hdr_version >= 2) {
            std::memcpy(&part, partData, sizeof(part));
        } else {
            kd_img_part_v1_t partV1{};
            std::memcpy(&partV1, partData, sizeof(partV1));
            part.part_magic = partV1.part_magic;
            part.part_offset = partV1.part_offset;
            part.part_size = partV1.part_size;
            part.part_erase_size = partV1.part_erase_size;
            part.part_max_size = partV1.part_max_size;
            part.part_flag = partV1.part_flag;
            part.part_content_offset = partV1.part_content_offset;
            part.part_content_size = partV1.part_content_size;
            std::memcpy(part.part_content_sha256,
                        partV1.part_content_sha256,
                        sizeof(part.part_content_sha256));
            std::memcpy(part.part_name, partV1.part_name,
                        sizeof(part.part_name));
        }

        const QString partName = fixedString(part.part_name, sizeof(part.part_name));
        const bool invalidSize = !part.part_size ||
                                 part.part_content_size > part.part_size;
        const quint64 padding = invalidSize
            ? std::numeric_limits<quint64>::max()
            : static_cast<quint64>(part.part_size) - part.part_content_size;
        const quint64 contentFloor = sizeof(kd_img_hdr_t) + tableSize;
        if (part.part_magic != KDIMG_PART_MAGIC || partName.isEmpty() ||
            invalidSize || padding > 4096 ||
            (part.part_content_size && part.part_content_offset < contentFloor) ||
            part.part_content_offset > static_cast<quint64>(imageSize) ||
            part.part_content_size >
                static_cast<quint64>(imageSize) - part.part_content_offset) {
            BurnLibrary::instance()->localLog(
                QStringLiteral("%1 invalid partition metadata: %2")
                    .arg(__func__, partName));
            return -1;
        }

        parts.append(part);
    }

    imageFile.seek(0);
    return 0;
}

bool buildKdImageItemList(const QFile &imageFile,
                          const QList<kd_img_part_t> &parts,
                          QList<BurnImageItem> &list)
{
    list.clear();
    for (const kd_img_part_t &part : parts) {
        logKdImgPart(part);

        BurnImageItem item;
        item.partName = fixedString(part.part_name, sizeof(part.part_name));
        item.partOffset = part.part_offset;
        item.partSize = part.part_max_size;
        item.partEraseSize = part.part_erase_size;
        item.partFlag = part.part_flag;
        item.fileName = imageFile.fileName();
        item.fileOffset = part.part_content_offset;
        item.dataSize = part.part_content_size;
        item.fileSize = part.part_size;
        item.paddingValue = 0xff;
        item.dataSha256 = QByteArray(
            reinterpret_cast<const char *>(part.part_content_sha256),
            sizeof(part.part_content_sha256));
        list.append(item);
    }
    return !list.isEmpty();
}
