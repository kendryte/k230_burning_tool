#pragma once

#include <QFile>
#include <QList>
#include <QThread>

#include "BurnImageItem.h"

#define KDIMG_HADER_MAGIC   (0x27CB8F93)
#define KDIMG_PART_MAGIC    (0x91DF6DA4)

struct alignas(512) kd_img_hdr_t {
    uint32_t img_hdr_magic;
    uint32_t img_hdr_crc32;
    uint32_t img_hdr_flag;

    uint32_t part_tbl_num;
    uint32_t part_tbl_crc32;

    char image_info[32];
    char chip_info[32];
    char board_info[64];
};
static_assert(sizeof(struct kd_img_hdr_t) == 512, "Size of kd_img_part_t struct is not 512 bytes!");

struct alignas(256) kd_img_part_t {
    uint32_t part_magic;
    uint32_t part_offset; // align to 4096
    uint32_t part_size; // align to 4096
    uint32_t part_erase_size;
    uint32_t part_max_size;
    uint32_t part_flag;

    uint32_t part_content_offset;
    uint32_t part_content_size;
	uint8_t  part_content_sha256[32];

    char part_name[32];
};
static_assert(sizeof(struct kd_img_part_t) == 256, "Size of kd_img_part_t struct is not 256 bytes!");

int parseKdImage(QFile &imageFile, struct kd_img_hdr_t& hdr, QList<struct kd_img_part_t> & parts);

bool compareKdImage(struct kd_img_hdr_t& current_hdr, QList<struct kd_img_part_t> & current_parts,
        struct kd_img_hdr_t& last_hdr, QList<struct kd_img_part_t> & last_parts);

bool extractKdImageToBurnImageItemList(QFile &imageFile, QList<struct kd_img_part_t> & parts, QList<struct BurnImageItem> &list);

class ExtractKdImageWorker : public QObject {
    Q_OBJECT

public:
    ExtractKdImageWorker() {}
    ~ExtractKdImageWorker() {}

public slots:
    void extractKdImage(QFile &imageFile, QList<struct kd_img_part_t> & parts, QList<struct BurnImageItem> &list) {
	    bool result = extractKdImageToBurnImageItemList(imageFile, parts, list);
        emit taskCompleted(result);
    }

signals:
    void taskCompleted(bool result);
};
