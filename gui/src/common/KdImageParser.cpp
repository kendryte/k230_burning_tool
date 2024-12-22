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
        "  part_max_size: %4\n"
        "  part_flag: 0x%5\n"
        "  part_content_offset: %6\n"
        "  part_content_sha256: %7\n"
        "  part_name: %8"
    ).arg(QString::number(part.part_magic, 16).toUpper())
     .arg(part.part_offset)
     .arg(part.part_size)
     .arg(part.part_max_size)
     .arg(QString::number(part.part_flag, 16).toUpper())
     .arg(part.part_content_offset)
     .arg(QByteArray(reinterpret_cast<const char*>(part.part_content_sha256), 32).toHex())
     .arg(QString::fromUtf8(part.part_name));

    BurnLibrary::instance()->localLog(logMessage);
}
bool extractKdImageToBurnImageItemList(QFile &imageFile, QList<struct kd_img_part_t> &parts, QList<struct BurnImageItem> &list)
{
    constexpr qint64 ChunkSize = 4 * 1024 * 1024; // 4 MiB

    // Check if the file is open
    if (!imageFile.isOpen()) {
        BurnLibrary::instance()->localLog(QStringLiteral("%1 imageFile not open").arg(__func__));
        return false;
    }

    // Reset the list to ensure it starts clean
    list.clear();

    // Temporary folder for storing extracted files
    QDir tempDir(QDir::tempPath() + "/BurnImageItems");
    if (!tempDir.exists()) {
        tempDir.mkpath(".");
    }

    // Iterate over the parts
    for (const kd_img_part_t &part : parts) {
        // Create an instance of QElapsedTimer
        QElapsedTimer timer;

        // Start the timer
        timer.start();

        logKdImgPart(part);

        // Validate the part magic
        if (part.part_magic != KDIMG_PART_MAGIC) {
            BurnLibrary::instance()->localLog(QStringLiteral("Invalid part magic: %1").arg(part.part_magic));
            return false;
        }

        // Prepare the SHA-256 calculation
        QCryptographicHash hash(QCryptographicHash::Sha256);

        // Save the file data to the temporary folder
        QString tempFileName = tempDir.filePath(QString("%1_0x%2.bin").arg(QString(part.part_name)).arg(QString::number(part.part_offset, 16)));
        QFile tempFile(tempFileName);

        // Check if the file already exists and remove it
        if (tempFile.exists()) {
            if (!tempFile.remove()) {
                BurnLibrary::instance()->localLog(QStringLiteral("Failed to remove existing temp file: %1").arg(tempFileName));
                return false;
            }
        }

        if (!tempFile.open(QIODevice::WriteOnly)) {
            BurnLibrary::instance()->localLog(QStringLiteral("Failed to create temp file: %1").arg(tempFileName));
            return false;
        }

        // Extract data in chunks
        qint64 remainingSize = part.part_content_size;
        qint64 currentOffset = part.part_content_offset;

        while (remainingSize > 0) {
            qint64 bytesToRead = qMin(ChunkSize, remainingSize);

            // Explicitly flush and sync the file stream before reading
            if (!imageFile.seek(currentOffset)) {
                BurnLibrary::instance()->localLog(QStringLiteral("Failed to seek to content offset: %1").arg(currentOffset));
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

            // Update the SHA-256 hash
            hash.addData(chunkData);

            // Write the chunk to the temporary file
            if (tempFile.write(chunkData) != bytesToRead) {
                BurnLibrary::instance()->localLog(QStringLiteral("Failed to write chunk to temp file: %1").arg(tempFileName));
                tempFile.close();
                tempFile.remove();
                return false;
            }

            currentOffset += bytesToRead;
            remainingSize -= bytesToRead;
        }

        if(part.part_content_size < part.part_size) {
            uint32_t padding = part.part_size - part.part_content_size;

            if(padding > 4096) {
                BurnLibrary::instance()->localLog(QStringLiteral("Error align part size, %1").arg(padding));
            } else {
                BurnLibrary::instance()->localLog(QStringLiteral("Padding part by %1 bytes to align size").arg(padding));

                QByteArray paddingData(padding, 0);
                tempFile.write(paddingData);
            }
        }

        tempFile.close();

        // Finalize the SHA-256 hash
        QByteArray calculatedHash = hash.result();
        QByteArray expectedHash = QByteArray(reinterpret_cast<const char*>(part.part_content_sha256), 32);

        // Compare the hashes
        if (calculatedHash != expectedHash) {
            BurnLibrary::instance()->localLog(QStringLiteral("SHA-256 mismatch for part: %1").arg(QString(part.part_name)));
            BurnLibrary::instance()->localLog(QStringLiteral("Calculated SHA-256: %1").arg(calculatedHash.toHex()));
            BurnLibrary::instance()->localLog(QStringLiteral("Expected SHA-256:   %1").arg(expectedHash.toHex()));
            // tempFile.remove();
            // return false;
        }

        // Populate the BurnImageItem
        BurnImageItem item;
        item.partName = QString(part.part_name);
        item.partOffset = part.part_offset;
        item.partSize = part.part_max_size;
        item.fileName = tempFileName;
        item.fileSize = part.part_size;

        // Append to the list
        list.append(item);

        // Get the elapsed time in milliseconds
        qint64 elapsed = timer.elapsed();
        BurnLibrary::instance()->localLog(QStringLiteral("use time %1 ms").arg(elapsed));
    }

    return !list.isEmpty();
}
