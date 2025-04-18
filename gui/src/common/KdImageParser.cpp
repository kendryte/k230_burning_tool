#include "KdImageParser.h"

#include <public/canaan-burn.h>

#include "common/BurnLibrary.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>

int parseKdImage(QFile &imageFile, kd_img_hdr_t& hdr, QList<kd_img_part_t> &parts) {
    parts.clear();

    // Check if the file is open
    if (!imageFile.isOpen()) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 imageFile not open").arg(__func__));
        return -1;
    }

    // Seek to the start of the file (SEEK_SET is the default for Qt's seek)
    if (!imageFile.seek(0)) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 failed to seek to the beginning of the file").arg(__func__));
        return -1;
    }

    // Read the 512-byte image header into a QByteArray
    QByteArray headerData = imageFile.read(512);
    if (headerData.size() != 512) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 failed to read full image header!").arg(__func__));
        return -1;
    }

    // Copy the data from QByteArray into the header struct
    std::memcpy(&hdr, headerData.data(), sizeof(kd_img_hdr_t));

    if(KDIMG_HADER_MAGIC != hdr.img_hdr_magic) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 invalid image header magic, %2 != %3!").arg(__func__).arg(KDIMG_HADER_MAGIC).arg(hdr.img_hdr_magic));
        return -1;
    }

    int sizePartsContent = hdr.part_tbl_num * sizeof(kd_img_part_t);
    QByteArray part_table_content = imageFile.read(sizePartsContent);

    if(part_table_content.size() != sizePartsContent) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 failed to read full image part table!").arg(__func__));
        return -1;
    }

    uint32_t calc_crc32 = crc32(0, reinterpret_cast<const unsigned char*>(part_table_content.data()), part_table_content.size());

    if(calc_crc32 != hdr.part_tbl_crc32) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 invalid part table checksum, %2 != %3!").arg(__func__).arg(calc_crc32).arg(hdr.part_tbl_crc32));
        return -1;
    }

    for (int i = 0; i < sizePartsContent; i += sizeof(kd_img_part_t)) {
        kd_img_part_t part;
        QByteArray partData = part_table_content.mid(i, sizeof(kd_img_part_t));

        // Copy the part data into the struct
        std::memcpy(&part, partData.data(), sizeof(kd_img_part_t));

        if(KDIMG_PART_MAGIC != part.part_magic) {
            BurnLibrary::instance()->localLog(QStringLiteral("%1 invalid part header magic, %2 != %3!").arg(__func__).arg(KDIMG_PART_MAGIC).arg(part.part_magic));
            return -1;
        }

        // Add the part to the list of parts
        parts.append(part);
    }

    // Seek to the start of the file (SEEK_SET is the default for Qt's seek)
    if (!imageFile.seek(0)) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 failed to seek to the beginning of the file").arg(__func__));
        return -1;
    }

    return 0;  // Success
}

bool compareKdImage(struct kd_img_hdr_t& current_hdr, QList<struct kd_img_part_t> & current_parts,
        struct kd_img_hdr_t& last_hdr, QList<struct kd_img_part_t> & last_parts)
{
    if(0x00 != std::memcmp(&current_hdr, &last_hdr, sizeof(struct kd_img_hdr_t))) {
        return false;
    }

    if(current_parts.size() != last_parts.size()) {
        return false;
    }

    for(int i = 0; i < current_parts.size(); i++) {
        if(0x00 != std::memcmp(&current_parts[i], &last_parts[i], sizeof(struct kd_img_part_t))) {
            return false;
        }
    }

    return true;
}

void logKdImgPart(const kd_img_part_t &part)
{
    QString logMessage = QStringLiteral(
        "kd_img_part_t:\n"
        "  part_magic: 0x%1\n"
        "  part_offset: %2\n"
        "  part_size: %3\n"
        "  part_erase_size: %4\n"
        "  part_max_size: %5\n"
        "  part_flag: 0x%6\n"
        "  part_content_offset: %7\n"
        "  part_content_sha256: %8\n"
        "  part_name: %9"
    ).arg(QString::number(part.part_magic, 16).toUpper())
     .arg(part.part_offset)
     .arg(part.part_size)
     .arg(part.part_erase_size)
     .arg(part.part_max_size)
     .arg(QString::number(part.part_flag, 16).toUpper())
     .arg(part.part_content_offset)
     .arg(QByteArray(reinterpret_cast<const char*>(part.part_content_sha256), 32).toHex())
     .arg(QString::fromUtf8(part.part_name));

    BurnLibrary::instance()->localLog(logMessage);
}

bool extractKdImageToBurnImageItemList(QFile &imageFile, QList<struct kd_img_part_t> &parts, QList<struct kd_img_part_t> &lastParts, QList<struct BurnImageItem> &list, QList<struct BurnImageItem> &lastList)
{
    constexpr qint64 ChunkSize = 64 * 1024 * 1024;

    if (!imageFile.isOpen()) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 imageFile not open").arg(__func__));
        return false;
    }

    list.clear();

    const QString basePath = QDir::tempPath();
    QDir tempDir(basePath);
    QString newFolderName = "BurnImageItems";
    QString lastFolderName = "BurnImageItemsLast";

    QString newFolderPath = tempDir.filePath(newFolderName);
    QString lastFolderPath = tempDir.filePath(lastFolderName);

    // If lastFolder exists, remove it (from previous run)
    QDir lastTempDir(lastFolderPath);
    if (lastTempDir.exists()) {
        lastTempDir.removeRecursively();
    }

    // If current BurnImageItems exists, rename it to BurnImageItemsLast
    QDir currentTempDir(newFolderPath);
    if (currentTempDir.exists()) {
        if (!tempDir.rename(newFolderName, lastFolderName)) {
            BurnLibrary::instance()->localLog(QStringLiteral("Failed to rename existing BurnImageItems to BurnImageItemsLast"));
            return false;
        }

        // Adjust lastList file paths after renaming
        for (BurnImageItem &item : lastList) {
            QFileInfo fi(item.fileName);
            item.fileName = QDir(lastFolderPath).filePath(fi.fileName());
        }
    }

    // Create a fresh BurnImageItems directory
    QDir newTempDir(newFolderPath);
    if (!newTempDir.exists()) {
        if (!newTempDir.mkpath(".")) {
            BurnLibrary::instance()->localLog(QStringLiteral("Failed to create new BurnImageItems folder"));
            return false;
        }
    }

    for (const kd_img_part_t &part : parts) {
        QElapsedTimer timer;
        timer.start();

        logKdImgPart(part);

        if (part.part_magic != KDIMG_PART_MAGIC) {
            BurnLibrary::instance()->localLog(QStringLiteral("Invalid part magic: %1").arg(part.part_magic));
            return false;
        }

        QByteArray expectedHash = QByteArray(reinterpret_cast<const char*>(part.part_content_sha256), 32);
        QString partNameStr = QString(part.part_name);
        QString newTempFileName = newTempDir.filePath(QString("%1_0x%2.bin").arg(partNameStr).arg(QString::number(part.part_offset, 16)));

        bool reused = false;

        // Try to reuse from lastParts and lastList (coming from BurnImageItemsLast)
        for (int i = 0; i < lastParts.size(); ++i) {
            const kd_img_part_t &lastPart = lastParts[i];
            const BurnImageItem &lastItem = lastList[i];

            QByteArray lastHash = QByteArray(reinterpret_cast<const char*>(lastPart.part_content_sha256), 32);
            if (lastHash == expectedHash && QString(lastPart.part_name) == partNameStr) {
                QString oldFilePath = lastItem.fileName;

                // Copy the reused file into the new folder
                if (QFile::copy(oldFilePath, newTempFileName)) {
                    BurnLibrary::instance()->localLog(QStringLiteral("Reused and copied part: %1").arg(partNameStr));

                    BurnImageItem reusedItem;
                    reusedItem.partName = partNameStr;
                    reusedItem.partOffset = part.part_offset;
                    reusedItem.partSize = part.part_max_size;
                    reusedItem.partEraseSize = part.part_erase_size;
                    reusedItem.partFlag = part.part_flag;
                    reusedItem.fileName = newTempFileName;
                    reusedItem.fileSize = part.part_size;
                    list.append(reusedItem);
                    reused = true;
                } else {
                    BurnLibrary::instance()->localLog(QStringLiteral("Failed to copy reused file: %1").arg(oldFilePath));
                    return false;
                }
                break;
            }
        }

        if (reused) {
            continue;
        }

        // Not reused — extract from image
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QFile tempFile(newTempFileName);

        if (!tempFile.open(QIODevice::WriteOnly)) {
            BurnLibrary::instance()->localLog(QStringLiteral("Failed to create temp file: %1").arg(newTempFileName));
            return false;
        }

        qint64 remainingSize = part.part_content_size;
        qint64 currentOffset = part.part_content_offset;

        while (remainingSize > 0) {
            qint64 bytesToRead = qMin(ChunkSize, remainingSize);

            if (!imageFile.seek(currentOffset)) {
                BurnLibrary::instance()->localLog(QStringLiteral("Failed to seek to offset: %1").arg(currentOffset));
                tempFile.close();
                tempFile.remove();
                return false;
            }

            QByteArray chunkData = imageFile.read(bytesToRead);
            if (chunkData.size() != bytesToRead) {
                BurnLibrary::instance()->localLog(QStringLiteral("Failed to read chunk at offset: %1").arg(currentOffset));
                tempFile.close();
                tempFile.remove();
                return false;
            }

            hash.addData(chunkData);

            if (tempFile.write(chunkData) != bytesToRead) {
                BurnLibrary::instance()->localLog(QStringLiteral("Failed to write chunk to temp file: %1").arg(newTempFileName));
                tempFile.close();
                tempFile.remove();
                return false;
            }

            currentOffset += bytesToRead;
            remainingSize -= bytesToRead;
        }

        // Padding if necessary
        if (part.part_content_size < part.part_size) {
            uint32_t padding = part.part_size - part.part_content_size;
            if (padding > 4096) {
                BurnLibrary::instance()->localLog(QStringLiteral("Error align part size, %1").arg(padding));
            } else {
                BurnLibrary::instance()->localLog(QStringLiteral("Padding part by %1 bytes").arg(padding));
                QByteArray paddingData(padding, 0xFF);
                tempFile.write(paddingData);
            }
        }

        tempFile.close();

        QByteArray calculatedHash = hash.result();
        if (calculatedHash != expectedHash) {
            BurnLibrary::instance()->localLog(QStringLiteral("SHA-256 mismatch for part: %1").arg(partNameStr));
            BurnLibrary::instance()->localLog(QStringLiteral("Calculated: %1").arg(calculatedHash.toHex()));
            BurnLibrary::instance()->localLog(QStringLiteral("Expected:   %1").arg(expectedHash.toHex()));
        }

        BurnImageItem item;
        item.partName = partNameStr;
        item.partOffset = part.part_offset;
        item.partSize = part.part_max_size;
        item.partEraseSize = part.part_erase_size;
        item.partFlag = part.part_flag;
        item.fileName = newTempFileName;
        item.fileSize = part.part_size;
        list.append(item);

        BurnLibrary::instance()->localLog(QStringLiteral("use time %1 ms").arg(timer.elapsed()));
    }

    // Cleanup BurnImageItemsLast after use
    if (lastTempDir.exists()) {
        lastTempDir.removeRecursively();
        BurnLibrary::instance()->localLog("Cleaned up BurnImageItemsLast after reuse.");
    }

    return !list.isEmpty();
}
